/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/scriptengine.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonDocument>
#include <QThread>
#include <QTimer>
#include <QUndoStack>

#include "scripting/scriptworker.h"
#include "services/jahlog.h"

QString ScriptResult::toString() const
{
    if (ok) {
        if (value.isValid() && !value.isNull())
            return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
        return QString();
    }
    QString where = fileName;
    if (line > 0) where += QStringLiteral(":%1").arg(line);
    return where.isEmpty() ? error : where + QStringLiteral(": ") + error;
}

ScriptEngine::ScriptEngine(ScriptHost &host, QObject *parent)
    : QObject(parent), mHost(host)
{
    // The host carries the registry back to the modules so app.apiProblems()
    // can validate the live surface (AI_SURFACE_PROGRAM_SPEC §2.0). One
    // ScriptEngine per host, so this is never contested.
    mHost.registry = &mRegistry;
    // "AFTER THE SCRIPT", which used to be what a queued call meant and is not
    // any more (ScriptHost::afterRun). Banked here and drained once the run's
    // macro is closed and the engine is idle.
    mHost.afterRun = [this](std::function<void()> fn) {
        if (!fn) return;
        if (mRunning) { mAfterRun.push_back(std::move(fn)); return; }
        QTimer::singleShot(0, this, [fn = std::move(fn)]() { fn(); });
    };
}

ScriptEngine::~ScriptEngine()
{
    if (!mThread) return;
    // A run in flight at teardown would be blocked in a hop to THIS thread, so
    // interrupting it is the only way out; in practice evaluate() is
    // synchronous and nothing can be running here.
    if (mWorker && mWorker->engine()) mWorker->engine()->setInterrupted(true);
    QMetaObject::invokeMethod(mWorker, "teardown", Qt::BlockingQueuedConnection);
    mThread->quit();
    if (!mThread->wait(5000))
        qWarning("scripting: the script thread did not stop within 5 s");
    delete mWorker;
    mWorker = nullptr;
    delete mThread;
    mThread = nullptr;
}

void ScriptEngine::addModule(ApiModule *module)
{
    if (!module) return;
    module->setParent(this);   // C++ ownership: nothing else decides when a module dies
    mRegistry.add(module);
}

QString ScriptEngine::policyName(ScriptRunPolicy policy)
{
    return policy == ScriptRunPolicy::Live ? QStringLiteral("live") : QStringLiteral("off");
}

ScriptRunPolicy ScriptEngine::policyFromName(const QString &name, bool &ok)
{
    ok = true;
    if (name.compare(QLatin1String("live"), Qt::CaseInsensitive) == 0) return ScriptRunPolicy::Live;
    if (name.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0) return ScriptRunPolicy::Off;
    ok = false;
    return ScriptRunPolicy::Off;
}

void ScriptEngine::installApi()
{
    if (mInstalled) return;
    mInstalled = true;

    // THE SHIM SPEC IS BUILT HERE, on the UI thread, from the registry — the
    // worker never reads the registry directly, so there is no shared structure
    // for the two threads to disagree about.
    QVariantList specs;
    for (ApiModule *module : mRegistry.modules()) {
        QStringList names;
        for (const VerbInfo &verb : module->verbs()) names << verb.name;
        specs.append(QVariantMap{ { QStringLiteral("name"), module->jsName() },
                                  { QStringLiteral("verbs"), names } });
    }
    // `api` is a module to the shim generator and a special case to the
    // dispatcher; `version` is set as a constant on the worker side.
    specs.append(QVariantMap{ { QStringLiteral("name"), QStringLiteral("api") },
                              { QStringLiteral("verbs"),
                                QStringList{ QStringLiteral("help"), QStringLiteral("verbs") } } });

    mDispatcher = new VerbDispatcher(mRegistry, mHost, this);
    mWorker = new ScriptWorker(mDispatcher);
    mThread = new QThread;
    mThread->setObjectName(QStringLiteral("jah-script"));
    mWorker->moveToThread(mThread);
    connect(mWorker, &ScriptWorker::consoleLine, this, &ScriptEngine::consoleOutput);
    mThread->start();

    QMetaObject::invokeMethod(mWorker, "setup", Qt::BlockingQueuedConnection,
                              Q_ARG(QVariantList, specs),
                              Q_ARG(QString, QString::fromLatin1(ApiRegistry::apiVersion())));
}

void ScriptEngine::setVerbTracing(bool on)
{
    mRegistry.setTracing(on);
}

void ScriptEngine::stop()
{
    if (!mRunning || !mWorker || !mWorker->engine()) return;
    mStopped = true;
    // Documented thread safe, and the only control surface QJSEngine offers:
    // the run ABORTS at the next bytecode boundary, it does not pause.
    mWorker->engine()->setInterrupted(true);
}

ScriptResult ScriptEngine::evaluate(const QString &source, const QString &fileName,
                                    bool wrapUndoMacro, int timeoutMs, ScriptRunPolicy policy)
{
    ScriptResult result;
    result.fileName = fileName;

    // ONE RUN AT A TIME. Verbs that spin the event loop themselves
    // (project.open, an import, a progress dialog) can dispatch a console
    // click or an MCP request mid-run; a second run would evaluate into the
    // same worker engine from a second stack AND close the first run's undo
    // macro and database batch on its way out. Refusing is the whole guard.
    if (mRunning) {
        result.error = QStringLiteral(
            "a script is already running — wait for it to finish (or press Stop). "
            "Scripts do not nest: one run, one undo step.");
        JAH_LOG(JahLog::script, Error,
                QStringLiteral("script: %1 REFUSED — a run is already in flight").arg(fileName));
        return result;
    }

    installApi();

    // THE CONSOLE IS NOT THE AGENT (round-2 review item 5). A traced MCP run
    // can spin the event loop (project.open does), and the user's console dock
    // can run a script inside that window — pause the trace for the duration
    // of a console run and restore whatever the state was.
    struct TracePause
    {
        ApiRegistry &registry;
        bool previous;
        explicit TracePause(ApiRegistry &r, bool pause)
            : registry(r), previous(r.tracePaused())
        {
            if (pause) registry.setTracePaused(true);
        }
        ~TracePause() { registry.setTracePaused(previous); }
    } tracePause(mRegistry, fileName == QLatin1String("<console>"));

    QElapsedTimer scriptClock;
    scriptClock.start();

    // Always clear first: an interrupted PREVIOUS run leaves the flag set, and
    // a still-set flag aborts the next script before its first statement.
    mWorker->engine()->setInterrupted(false);
    mStopped = false;
    mRunning = true;
    mRunPolicy = policy;
    emit runningChanged(true);

    // ONE UNDO ENTRY PER RUN — and only if the run records something. The
    // host's undo sink opens the macro on the first command that lands, so a
    // run that only reads leaves the stack exactly as it found it. Opened and
    // closed HERE, on the UI thread, around the whole run.
    const bool useMacro = wrapUndoMacro && mHost.beginUndoMacro && mHost.endUndoMacro;
    // The host's two halves both check this, so a verb that ends and reopens
    // the run's entry at a project boundary is a NO-OP in a run that has no
    // entry (an MCP internal expression). Saved and restored rather than
    // cleared: belt and braces now that a run started inside another one is
    // refused outright (a nested evaluation must not disarm the outer run's
    // bracket, and there is no longer a way to reach one).
    const bool outerWrapsUndoMacro = mHost.runWrapsUndoMacro;
    mHost.runWrapsUndoMacro = useMacro;
    if (useMacro) {
        // Through the host's own bracket, because a verb can close the entry
        // and open the next one mid-run at a project boundary (ScriptHost::
        // endRunUndoMacro) and both ends have to agree on the name and on the
        // order of the two hooks.
        mHost.runMacroText = QStringLiteral("script: %1").arg(QFileInfo(fileName).fileName());
        mHost.beginRunUndoMacro();
    }

    // THE RUN POLICY. Off suspends the render driver's tick for the duration —
    // which is what a blocked UI thread used to do for free, and what the 54
    // frame-stepping e2e scripts and the 18 frame-counter readers depend on.
    const bool suspendDriver = (policy == ScriptRunPolicy::Off);
    if (suspendDriver && mHost.driverSuspended) mHost.driverSuspended(true);

    // WAIT BY PUMPING, never by blocking: the verb hops are events on THIS
    // thread, so a mutex wait here would deadlock on the very first one.
    bool done = false;
    QEventLoop loop;
    QMetaObject::Connection finished =
        connect(mWorker, &ScriptWorker::finished, this, [&done, &loop]() {
            done = true;
            loop.quit();
        });

    // The timeout is a plain timer now: the UI thread is free while the script
    // runs, so the watchdog THREAD the old implementation needed is gone.
    QTimer watchdog;
    bool firedTimeout = false;
    watchdog.setSingleShot(true);
    watchdog.setTimerType(Qt::PreciseTimer);
    connect(&watchdog, &QTimer::timeout, this, [this, &firedTimeout]() {
        firedTimeout = true;
        if (mWorker && mWorker->engine()) mWorker->engine()->setInterrupted(true);
    });
    if (timeoutMs > 0) watchdog.start(timeoutMs);

    QMetaObject::invokeMethod(mWorker, "runScript", Qt::QueuedConnection,
                              Q_ARG(QString, source), Q_ARG(QString, fileName));
    while (!done) {
        loop.exec();
        // A LOOP CAN BE EXITED FROM OUTSIDE: QCoreApplication::quit() exits
        // every event loop on this thread, nested ones included, and exec()
        // then returns instantly for ever after. Leaving the wait early is not
        // an option — the worker would hop into a dispatcher whose owner is
        // being torn down — so fall back to timed slices, which keep servicing
        // the hops without spinning a core.
        if (!done) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    watchdog.stop();
    disconnect(finished);

    result = mWorker->result();
    mWorker->engine()->setInterrupted(false);

    if (suspendDriver && mHost.driverSuspended) mHost.driverSuspended(false);

    if (useMacro) mHost.endRunUndoMacro();
    mHost.runWrapsUndoMacro = outerWrapsUndoMacro;

    const bool stopped = mStopped;
    mRunning = false;
    mStopped = false;
    emit runningChanged(false);

    // Everything a verb deferred "until the script finishes" — app.quit() being
    // the one that matters, because what it closes is this object's owner. Run
    // AFTER mRunning is false so a callback that itself evaluates is allowed,
    // and through the event loop so the caller's stack is gone first.
    if (!mAfterRun.empty()) {
        std::vector<std::function<void()>> banked;
        banked.swap(mAfterRun);
        QTimer::singleShot(0, this, [banked = std::move(banked)]() {
            for (const auto &fn : banked) fn();
        });
    }

    if (firedTimeout) {
        result.ok = false;
        result.timedOut = true;
        result.error = QStringLiteral(
                           "script interrupted after %1 ms (timeoutMs). Note that only "
                           "JavaScript is interruptible — a run parked inside a verb "
                           "(editor.frame, graph.bake, an import) runs to completion first.")
                           .arg(timeoutMs);
    } else if (stopped) {
        result.ok = false;
        result.error = QStringLiteral(
            "script stopped. The edits it had already made are the run's one undo step — "
            "one Ctrl+Z takes them all back.");
    }

    if (result.ok) {
        JAH_LOG(JahLog::script, Display,
                QStringLiteral("script: %1 ok in %2 ms")
                    .arg(fileName.isEmpty() ? QStringLiteral("<expression>") : fileName)
                    .arg(scriptClock.elapsed()));
    } else {
        JAH_LOG(JahLog::script, Error,
                QStringLiteral("script: %1%2 FAILED after %3 ms — %4")
                    .arg(result.fileName.isEmpty() ? QStringLiteral("<expression>")
                                                   : result.fileName,
                         result.line > 0 ? QStringLiteral(":%1").arg(result.line) : QString())
                    .arg(scriptClock.elapsed()).arg(result.error));
    }
    return result;
}
