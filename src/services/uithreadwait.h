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
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <functional>
#include <utility>

namespace UiThreadWait
{

enum class Pump { Events, None };

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
