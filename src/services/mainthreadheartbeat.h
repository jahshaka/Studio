/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MAINTHREADHEARTBEAT_H
#define MAINTHREADHEARTBEAT_H

// MainThreadHeartbeat — "did the window stay alive?", as a number.
//
// A timer on the UI thread that records the WORST gap between its own ticks.
// A UI thread inside a long synchronous call cannot service its event loop,
// so it cannot tick: the gap IS the freeze, in milliseconds, and it is what
// the desktop environment measures before it offers to kill the app.
//
// Deliberately dumb: no averages, no history — the maximum is the only
// number that decides whether an operation is allowed to run on this thread.
// Driven from scripts through app.heartbeat()/app.heartbeatStats(), which is
// how the open-responsiveness e2e asserts its budget without a window
// manager.

#include <QVariantMap>
#include <QtGlobal>

namespace MainThreadHeartbeat {

/// Starts (or restarts, resetting the statistics) the probe on the calling
/// thread's event loop. `intervalMs` is clamped to >= 10.
void start(int intervalMs);
void stop();
bool isRunning();

/// {running, intervalMs, ticks, maxGapMs, sinceLastTickMs}. maxGapMs also
/// accounts for the time since the last tick, so a probe read from inside a
/// long-running call reports the freeze in progress.
///
/// UI THREAD ONLY. It builds a QVariantMap (allocations, implicit sharing) and
/// reads non-atomic members — never call it from a watchdog thread.
QVariantMap stats();

// ---- The off-thread view (STABILITY_PROGRAM_SPEC Lane 5) -------------------
//
// The watchdog (services/mainthreadwatchdog.h) lives on its OWN thread and has
// to answer one question every 250 ms: "when did the UI thread last tick?".
// stats() cannot answer it — a QVariantMap is not something to build from a
// thread that is watching for a deadlock. These three are plain atomics with
// no locks, no allocation and no Qt containers, which is the whole point.

/// The value of a process-wide monotonic clock at the last tick, in
/// nanoseconds; 0 before the probe has ever started. Safe from any thread.
qint64 lastTickNs();
/// The same clock, read now. `nowNs() - lastTickNs()` is the stall in
/// progress. Safe from any thread.
qint64 nowNs();
/// Atomic mirror of isRunning(). A stopped probe stops updating lastTickNs,
/// so a watchdog that ignored this would read the stop as an infinite stall.
bool isRunningAtomic();

}   // namespace MainThreadHeartbeat

#endif   // MAINTHREADHEARTBEAT_H
