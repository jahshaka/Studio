/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/scriptengine.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <QFileInfo>
#include <QJSValueIterator>
#include <QJsonDocument>
#include <QUndoStack>

namespace {

/// The F6 watchdog. A main-thread QTimer cannot serve here: mJs.evaluate()
/// blocks the very event loop the console dock, the --script runner and the MCP
/// drain all live on (mcpserver.cpp's drainQueue is synchronous), so the timer
/// would only fire after the run it was meant to cut short had finished.
/// QJSEngine::setInterrupted is documented thread-safe, which is what makes a
/// plain worker thread the whole mechanism.
///
/// The condition variable is not decoration: without it the thread would sleep
/// out its full budget even after a fast script returned, and a 30 s default
/// would make every teardown wait 30 s.
class InterruptWatchdog
{
public:
    InterruptWatchdog(QJSEngine *js, int timeoutMs) : mJs(js)
    {
        mThread = std::thread([this, timeoutMs]() {
            std::unique_lock<std::mutex> lock(mMutex);
            if (mCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                             [this]() { return mDone; }))
                return;                     // the script finished first
            mFired = true;
            mJs->setInterrupted(true);
        });
    }

    ~InterruptWatchdog() { stop(); }

    /// Joins the thread. Call it BEFORE reading fired(): the watchdog can
    /// expire in the instant between evaluate() returning and this object being
    /// destroyed, and a fired() read that races the join would miss it.
    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mDone = true;
        }
        mCv.notify_all();
        if (mThread.joinable()) mThread.join();
    }

    bool fired() const { return mFired; }

private:
    QJSEngine *mJs;
    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mCv;
    bool mDone = false;
    std::atomic_bool mFired{false};
};

/// The host end of `console.log(...)`: the JS bootstrap below stringifies its
/// arguments and calls emitLine(). A QObject so QJSEngine bridges it for free.
class ConsoleBridge : public QObject
{
    Q_OBJECT
public:
    explicit ConsoleBridge(ScriptEngine *owner) : QObject(owner), mOwner(owner) {}
    Q_INVOKABLE void emitLine(const QString &text) { emit mOwner->consoleOutput(text); }
private:
    ScriptEngine *mOwner;
};

} // namespace

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
}

void ScriptEngine::addModule(ApiModule *module)
{
    if (!module) return;
    module->setParent(this);   // C++ ownership: the JS engine must never GC a module
    mRegistry.add(module);
}

void ScriptEngine::installApi()
{
    if (mInstalled) return;
    mInstalled = true;

    mRegistry.install(mJs);

    // console.log and friends: stringify like a browser console (objects as
    // JSON), then hand the line to the host bridge. Replaces the default
    // ConsoleExtension so the console dock and the CLI capture output.
    auto *bridge = new ConsoleBridge(this);
    mJs.globalObject().setProperty(QStringLiteral("__console_bridge"), mJs.newQObject(bridge));
    mJs.evaluate(QStringLiteral(R"JS(
        (function() {
            function fmt(a) {
                if (a === undefined) return "undefined";
                if (a === null) return "null";
                if (typeof a === "object") {
                    try { return JSON.stringify(a); } catch (e) { return String(a); }
                }
                return String(a);
            }
            function emit(args) {
                __console_bridge.emitLine(Array.prototype.map.call(args, fmt).join(" "));
            }
            console = {
                log:   function() { emit(arguments); },
                info:  function() { emit(arguments); },
                warn:  function() { emit(arguments); },
                error: function() { emit(arguments); }
            };
            print = console.log;
            help = function(topic) { console.log(api.help(topic === undefined ? "" : String(topic))); };
        })();
    )JS"), QStringLiteral("<bootstrap>"));
}

ScriptResult ScriptEngine::evaluate(const QString &source, const QString &fileName,
                                    bool wrapUndoMacro, int timeoutMs)
{
    installApi();

    ScriptResult result;
    result.fileName = fileName;

    // Always clear first: an interrupted PREVIOUS run leaves the flag set, and
    // a still-set flag aborts the next script before its first statement.
    mJs.setInterrupted(false);

    const bool useMacro = wrapUndoMacro && mHost.undoStack != nullptr;
    if (useMacro) {
        mHost.undoStack->beginMacro(QStringLiteral("script: %1").arg(QFileInfo(fileName).fileName()));
        if (mHost.macroOpenChanged) mHost.macroOpenChanged(true);
    }

    QStringList stackTrace;
    QJSValue value;
    bool interrupted = false;
    {
        std::unique_ptr<InterruptWatchdog> watchdog;
        if (timeoutMs > 0) watchdog.reset(new InterruptWatchdog(&mJs, timeoutMs));
        value = mJs.evaluate(source, fileName, 1, &stackTrace);
        if (watchdog) {
            watchdog->stop();
            interrupted = watchdog->fired();
        }
    }
    mJs.setInterrupted(false);

    if (useMacro) {
        if (mHost.macroOpenChanged) mHost.macroOpenChanged(false);
        mHost.undoStack->endMacro();
    }

    if (value.isError() || !stackTrace.isEmpty()) {
        result.ok = false;
        if (value.isError()) {
            const QString name = value.property(QStringLiteral("name")).toString();
            const QString message = value.property(QStringLiteral("message")).toString();
            result.error = name.isEmpty() ? message : name + QStringLiteral(": ") + message;
            result.line = value.property(QStringLiteral("lineNumber")).toInt();
            // Errors thrown in a different file (or the bootstrap) carry their own name.
            const QString errFile = value.property(QStringLiteral("fileName")).toString();
            if (!errFile.isEmpty()) result.fileName = errFile;
        } else {
            result.error = QStringLiteral("uncaught exception");
        }
        if (interrupted) {
            result.timedOut = true;
            result.error = QStringLiteral(
                               "script interrupted after %1 ms (timeoutMs). Note that only "
                               "JavaScript is interruptible — a run parked inside a verb "
                               "(editor.frame, graph.bake, an import) runs to completion first.")
                               .arg(timeoutMs);
        }
        result.stack = stackTrace.join(QStringLiteral("\n"));
        if (result.line <= 0 && !stackTrace.isEmpty()) {
            // "func:file.js:12" — recover the line from the top stack frame.
            const QString top = stackTrace.first();
            const int colon = top.lastIndexOf(':');
            if (colon > 0) result.line = top.mid(colon + 1).toInt();
        }
    } else if (interrupted) {
        // Belt and braces: an interrupt at a boundary where the VM produced no
        // error value must still be reported as a failure, never as a result.
        result.ok = false;
        result.timedOut = true;
        result.error = QStringLiteral("script interrupted after %1 ms (timeoutMs)").arg(timeoutMs);
    } else {
        result.ok = true;
        result.value = value.toVariant();
    }
    return result;
}

#include "scriptengine.moc"
