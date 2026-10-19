#ifndef PROCESSBUDGET_H
#define PROCESSBUDGET_H

// THE ONE CORE BUDGET, APPLIED TO THE PROCESS (THREADING-1): the two pools nobody sized before it. Called ONCE, by
// main.cpp right after QApplication (before any scene, any QtConcurrent job, any Bullet call) — and by threading.budget,
// which proves the same call's effect. The budget itself is irisgl/core/threadbudget.h; the pools sized elsewhere
// read it where they are made (the scene workers: sceneworkerthreads.h; the multiload and the shader cache's boot
// apply: the engine; the bake pool: meshbake.cpp; the bake store's and the tile cache's pools: their files).

#include <QThreadPool>

#include "irisgl/document/physics/environment.h"
#include "irisgl/core/threadbudget.h"

namespace processbudget {

inline void apply()
{
    const iris::ThreadBudget &budget = iris::threadBudget();
    // THE GAME THREAD IS THE CALLER (ENGINE-UPGRADE-U4 stage 6): Bullet's scheduler bound here, before any scene exists,
    // so this thread is Bullet's index 0 and the step's foreground share is fixed for the run.
    iris::Environment::bindGameThread();
    // QT'S GLOBAL POOL IS THE BACKGROUND TIER (it was uncapped: one thread per hardware thread). The 28 QtConcurrent
    // sites stay on it — parses, bakes, encodes and file I/O, none per frame, and blocking I/O must never occupy a
    // compute worker — and a wait on one of its futures runs the job inline when no thread is free
    // (QFuture::waitForFinished steals an unstarted runnable), so the cap cannot deadlock a nested wait.
    QThreadPool::globalInstance()->setMaxThreadCount(budget.background);
}

}   // namespace processbudget

#endif   // PROCESSBUDGET_H
