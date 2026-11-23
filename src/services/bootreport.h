/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef BOOTREPORT_H
#define BOOTREPORT_H

// THE BOOT's FACTS, AS RESULTS (NOQT-CORE-1 E2; the lead's ruling: a tool never reads a log line). What the boot
// did to the library - the shipped meshes' seed, the one-time index gate, the indexes an existing library's
// rows reached the opening thread without, the stale bakes the background sweep planned - recorded by the code
// that produced each fact and read by `app.bootReport()`. Memory only (no durable write on the UI thread).
//
// ONE per process, owned by the shell's lifecycle (shell/shelllifecycle.h: the facts land while MainWindow is
// built, before the services container exists); StudioServices points at it. Every field but `sweep` is written
// on the UI thread before any script runs; `sweep` is published by the sweep's worker (it holds its own
// reference: the report may die first).

#include "services/primitiveassets.h"

#include <QtGlobal>

#include <condition_variable>
#include <memory>
#include <mutex>

/// THE BACKGROUND BAKE SWEEP's PLAN (MeshBakeStore::startBackgroundRebuild), as a result with a TERMINAL state on
/// every way out: a reader joins it and gets an answer when the worker is done planning - never a "not yet" it
/// must guess the length of. NotStarted: no sweep was ever asked for (the shell did not open a library).
class SweepPlan
{
public:
    enum class State { NotStarted, Off, Planning, Planned, Unread, Stopped, Silent };

    /// The sweep was asked for and its worker is queued: a join waits from here.
    void start() { set(State::Planning, 0); }
    /// A terminal answer: Planned with its count; Off (no sweep runs: JAHSHAKA_BAKE_SWEEP=0, no open library,
    /// one already running); Unread (the plan's connection never opened); Stopped (a reset or a quit cut the plan);
    /// Silent (the worker ended without saying which: a defect, named so a reader never waits on it).
    void finish(State s, int rebuilds = 0) { set(s, rebuilds); }
    /// Publishes Silent when the worker ends still Planning (its exit guard's one call).
    void finishIfPlanning()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mState != State::Planning) return;
        mState = State::Silent;
        mCv.notify_all();
    }

    State state() const { std::lock_guard<std::mutex> lock(mMutex); return mState; }
    int rebuilds() const { std::lock_guard<std::mutex> lock(mMutex); return mRebuilds; }
    /// Waits while the worker is Planning; its bound is the worker's own end (it publishes on every exit).
    State join() const
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mCv.wait(lock, [this] { return mState != State::Planning; });
        return mState;
    }

    static const char *name(State s)
    {
        switch (s) {
        case State::NotStarted: return "notStarted";
        case State::Off:        return "off";
        case State::Planning:   return "planning";
        case State::Planned:    return "planned";
        case State::Unread:     return "unread";
        case State::Stopped:    return "stopped";
        case State::Silent:     return "silent";
        }
        return "?";
    }

private:
    void set(State s, int rebuilds)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mState = s;
        mRebuilds = rebuilds;
        mCv.notify_all();
    }

    mutable std::mutex mMutex;
    mutable std::condition_variable mCv;
    State mState = State::NotStarted;
    int mRebuilds = 0;
};

struct BootReport
{
    /// The boot's seedAll (shell/shelllifecycle.cpp openLibrary): rows planned, created, re-seeded by name, failed.
    /// The BOOT's: a library reset re-seeds (services/libraryreset.cpp) without changing it.
    PrimitiveAssets::SeedResult seed;
    /// The one-time index gate before the shell opened the library (app/main.cpp): statements built, its wall.
    struct IndexGate { int built = 0; qint64 ms = 0; } indexGate;
    /// Index statements run over an EXISTING library's rows on the opening thread (Database::createIndexes):
    /// a defect of the caller, 0 when the gate did its job.
    int indexAtOpen = 0;
    /// The background bake sweep's plan: its state and the stale bakes it planned to rebuild.
    std::shared_ptr<SweepPlan> sweep = std::make_shared<SweepPlan>();
};

#endif // BOOTREPORT_H
