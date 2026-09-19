/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/scriptengine.h"
#include "services/thumbnailstop.h"

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
#include "services/editgate.h"
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
    // QUEUED, NOT BLOCKING (round 2, L5). A blocking teardown would deadlock if
    // a run were somehow still in flight: the worker would be waiting on a hop
    // to THIS thread while this thread waited on the worker. Unreachable today
    // — evaluate() is synchronous, so nothing can be running here — but a
    // deadlock at shutdown is the worst way to learn that an assumption has
    // moved. Queued teardown, then quit(), then the join with a ceiling: the
    // worker processes both events in order and the wait below is the only
    // thing that blocks, and it gives up.
    QMetaObject::invokeMethod(mWorker, "teardown", Qt::QueuedConnection);
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
    // EVERY MODULE MUST BE REGISTERED BEFORE THE FIRST RUN (round 2, L3). The
    // worker's shim objects are generated once, from the registry, when the
    // engine is first used; a module added after that is in the registry — so
    // help(), the docs and api.verbs() all advertise it — and has no JS object
    // at all, which reads as "the verb silently does nothing". Loud, because
    // the symptom is not.
    if (mInstalled) {
        qWarning("scripting: module '%s' was registered AFTER the first script ran — its verbs "
                 "are documented but not callable. Register every module before the engine is "
                 "used (MainWindow does this in one place).",
                 qPrintable(module->jsName()));
        Q_ASSERT(!mInstalled);
    }
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
    // A VERB THAT LOOPS OVER THE LIBRARY HAS TO HEAR THIS TOO (THUMBS-1 fix
    // round F1). setInterrupted only aborts JAVASCRIPT at its next bytecode
    // boundary; `assets.rebuildThumbnails` is one verb call that may render
    // for minutes on the main thread, so the stop has to reach the sweep
    // itself or a stopped script keeps rendering.
    thumbrebuild::requestStop();
    // Documented thread safe, and the only control surface QJSEngine offers:
    // the run ABORTS at the next bytecode boundary, it does not pause.
    mWorker->engine()->setInterrupted(true);
}

ScriptResult ScriptEngine::evaluate(const QString &source, const QString &fileName,
                                    bool wrapUndoMacro, int timeoutMs, ScriptRunPolicy policy)
{
    ScriptResult result;
    result.fileName = fileName;

    // A NEW RUN IS A NEW INTENT: whatever stopped the last one does not stop
    // this one (services/thumbnailstop.h).
    thumbrebuild::clearStop();

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

    // THE EDITOR IS NON-EDITABLE FOR THE LENGTH OF THE RUN (owner, ledger
    // §423). The UI thread is free between the verbs, so a gizmo drag or a
    // slider is physically possible in the middle of the script's work — and
    // the run is ONE open undo macro, so a hand edit would silently join it.
    // Navigation is untouched: the camera, the panels, the tabs and the
    // selection all still answer. Armed for BOTH policies: "off" only means
    // the viewport holds its picture, not that the app is blocked.
    //
    // Armed HERE, beside the macro, rather than through a host hook, so the
    // two halves of the gate — the run bracket and the verb scope the
    // dispatcher opens — can never be half-wired by a host. A host that wired
    // one and forgot the other would refuse the SCRIPT's own writes.
    editgate::runStarted();

    // THE RUN POLICY, handed to the render loop as one state. Off suspends the
    // driver's tick for the duration — which is what a blocked UI thread used
    // to do for free, and what the 54 frame-stepping e2e scripts and the 18
    // frame-counter readers depend on. Live lets it draw, paced.
    if (mHost.scriptRunState)
        mHost.scriptRunState(policy == ScriptRunPolicy::Off ? ScriptRunState::Off
                                                            : ScriptRunState::Live);

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
    bool quitSeen = false;
    while (!done) {
        loop.exec();
        if (done) break;
        // A LOOP CAN BE EXITED FROM OUTSIDE: QCoreApplication::quit() exits
        // every event loop on this thread, nested ones included, and exec()
        // then returns instantly for ever after. Leaving the wait early is not
        // an option — the worker would hop into a dispatcher whose owner is
        // being torn down — so keep servicing the hops in timed slices instead
        // of spinning a core.
        //
        // AND STOP THE SCRIPT, ONCE (round 2, M2). Somebody has asked the
        // process to end; without this a `while (true)` would go on being
        // served for ever and the process would never exit. That is not
        // hypothetical — Preferences' "wipe and restart" quits and then starts
        // a SECOND instance, which is the one-instance hazard wearing a
        // different hat. Once, not per slice: interrupting repeatedly would
        // fight a verb that is legitimately still returning.
        if (!quitSeen) {
            quitSeen = true;
            stop();
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    watchdog.stop();
    disconnect(finished);

    result = mWorker->result();
    mWorker->engine()->setInterrupted(false);

    // The document is the user's again (the notice re-arms with the next run).
    editgate::runFinished();

    if (mHost.scriptRunState) mHost.scriptRunState(ScriptRunState::None);

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
    } else if (stopped && !result.ok) {
        // `&& !result.ok` above (round 2, L1): Stop can land in the instant
        // between the script's last statement and the worker's report, and a
        // run that FINISHED must not be told it was stopped — the interrupt
        // simply arrived too late to do anything.
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
