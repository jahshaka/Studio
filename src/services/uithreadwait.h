/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef UITHREADWAIT_H
#define UITHREADWAIT_H

// THE WORK OFF THE UI THREAD, THE ANSWER STILL SYNCHRONOUS (VERB-IMPORT-OFF-UI-1).
//
// A verb that must hand back a result in the same call (assets.importFile returns the
// guid) used to do its expensive half — an import's parse and bake, a bake rebuild —
// on the thread that draws. The open path solved the same problem first
// (ProjectRunner's prewarm, MeshBakeStore::rebuildPumped): the work runs on a pool
// thread and the UI thread WAITS FOR IT WHILE PUMPING — timers and posted events, user
// input excluded, so the frame loop and the heartbeat tick and nothing re-enters the
// editor from outside. This is that shape, once, for every caller.
//
// THE WORK MUST BE WORKER-SAFE: no default QSqlDatabase connection (it is bound to the
// UI thread), no widget, no document node the UI thread can reach. Everything the
// worker needs goes in by value; everything it produces comes back through the
// captured state the caller reads after run() returns.
//
// Pump::None is the JOIN: the work still runs on the pool, the caller blocks without
// delivering events. For a caller that cannot take an event at all — the library seed
// runs inside MainWindow's constructor, where a delivered event could reach half a
// window (ShellLifecycle::openLibrary).
//
// Off the UI thread (a test's worker, a QtConcurrent caller) run() is a plain call:
// the caller is already not the thread that draws.

#include <QCoreApplication>
#include <QEventLoop>
#include <QFuture>
#include <QMetaObject>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <functional>
#include <utility>
#include <vector>

namespace UiThreadWait
{

enum class Pump { Events, None };

// THE ONE RULE FOR WHAT A PUMP MAY DELIVER (STUDIO-D1 fix round, items 3-4).
//
// The pump runs timers and posted events so the frame loop and the heartbeat
// keep ticking — but the UI-thread operation that is waiting is HALF DONE (a
// preset's map row written and its material row not yet; an import's bytes
// staged and its rows not yet committed). A script verb hop or an MCP request
// delivered inside that pump would see the half-done state (the defect
// MaterialPresetSeeder's header names: a row count that moves under a script's
// feet) or nest a second wait inside the first (an MCP tool's waitForScriptIdle
// then holds the outer verb for its whole 30 s ceiling).
//
// So while any run() on the UI thread is waiting, the two doors that start
// OTHER work — the script bridge's verb hop (ScriptBridge::call) and the MCP
// server's request queue (McpServer::drainQueue) — hand their work to
// afterOperation() instead of running it: it is POSTED to the event loop when
// the outermost operation ends, so it runs after the operation's caller has
// finished its own turn, in arrival order. User input is excluded from the
// pump outright (ExcludeUserInputEvents). Nothing else is deferred: paint,
// timers and the work the operation itself posts still run.
inline int &operationDepth()
{
    static int depth = 0;   // UI thread only
    return depth;
}
inline std::vector<std::function<void()>> &deferredWork()
{
    static std::vector<std::function<void()>> work;   // UI thread only
    return work;
}
/// True while a UI-thread operation is waiting on its worker (see above).
inline bool inOperation() { return operationDepth() > 0; }
/// Runs `work` now when no operation is waiting; otherwise posts it to the
/// event loop the moment the outermost operation ends. UI thread only.
inline void afterOperation(std::function<void()> work)
{
    if (!inOperation()) { work(); return; }
    deferredWork().push_back(std::move(work));
}
namespace detail {
struct OperationScope
{
    OperationScope() { ++operationDepth(); }
    ~OperationScope()
    {
        if (--operationDepth() > 0) return;
        std::vector<std::function<void()>> pending;
        pending.swap(deferredWork());
        QCoreApplication *app = QCoreApplication::instance();
        for (auto &work : pending) {
            if (app) QMetaObject::invokeMethod(app, std::move(work), Qt::QueuedConnection);
        }
    }
};
}   // namespace detail

/// True on the thread that owns the application object (the UI thread).
inline bool onUiThread()
{
    const QCoreApplication *app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}

/// Runs `work` and returns once it has finished — on a pool thread when called on the
/// UI thread (see the header), inline otherwise. `tick`, when given, runs on the
/// waiting thread between pumps (a progress relay). An exception thrown by `work`
/// propagates out of run() either way.
template <typename Work>
void run(Work &&work, Pump pump = Pump::Events, const std::function<void()> &tick = {})
{
    if (!onUiThread()) {
        work();
        return;
    }
    // THE FLAG IS FLIPPED BY A SCOPE GUARD (ProjectRunner's rule): a throw out of the
    // work must end the pump, and the future's waitForFinished rethrows it here.
    std::atomic<bool> done { false };
    QFuture<void> future = QtConcurrent::run([&work, &done]() {
        struct Finish { std::atomic<bool> &flag; ~Finish() { flag.store(true); } } finish { done };
        work();
    });
    if (pump == Pump::Events) {
        detail::OperationScope operation;   // verb hops and MCP requests wait (see above)
        while (!done.load()) {
            if (tick) tick();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
            if (!done.load()) QThread::msleep(2);
        }
    }
    future.waitForFinished();
    if (tick) tick();
}

}   // namespace UiThreadWait

#endif // UITHREADWAIT_H
