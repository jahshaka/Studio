/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MAINTHREADWATCHDOG_H
#define MAINTHREADWATCHDOG_H

// MainThreadWatchdog — "the window froze; here is WHERE"
// (STABILITY_PROGRAM_SPEC.md Lane 5, decision D2 option A).
//
// MainThreadHeartbeat measures the freeze. This acts on it: a thread of our
// own polls the heartbeat's last-tick atomic, and when the UI thread has not
// ticked for `stallMs` it produces a BACKTRACE OF THE UI THREAD — the one
// thing app.heartbeatStats() cannot tell you.
//
// WHY A SIGNAL. backtrace()/backtrace_symbols_fd() capture the CALLING
// thread's stack, so a watchdog thread calling them photographs itself, which
// is useless. The only in-process way to photograph another thread is to make
// that thread do it: pthread_kill(uiThread, SIGUSR2) runs our handler ON the
// UI thread, right where it is stuck, and the handler writes its own stack.
//
// THE SAFEGUARDS ARE NOT OPTIONAL (all four are spec-mandated):
//   * SA_RESTART — an interrupted restartable syscall resumes instead of
//     failing with EINTR.
//   * backtrace() is PRE-WARMED once at start(). Its first call dlopen()s
//     libgcc's unwinder and mallocs; doing that first from inside a signal
//     handler is the classic in-handler deadlock (the UI thread is very
//     plausibly stuck holding the malloc arena).
//   * At most ONE report per stall, and a global cap + cooldown on top, so a
//     genuinely wedged process cannot turn its own log into the hang.
//   * The handler uses write() and backtrace_symbols_fd() only. No qWarning,
//     no snprintf, no Qt. The human-readable [warn] line is emitted from the
//     WATCHDOG thread, which is a perfectly ordinary thread.
//
// RESIDUAL RISK, on the record: poll/select/epoll_wait return EINTR even under
// SA_RESTART. Qt's event loop retries correctly; third-party code in the stall
// path might not. That is why this is a DEVELOPMENT-BUILD FEATURE: start() is
// a no-op in release builds, and even in a dev build it can be turned off with
// the `watchdog_enabled` preference or `--watchdog=off`.
//
// It is also stopped in MainWindow::shutdownBackgroundWork — a teardown that
// takes two seconds is normal, and photographing it would be noise (the
// separate 20 s force-exit thread there is the shutdown watchdog; this is not
// it).

#include <QString>
#include <QVariantMap>

namespace MainThreadWatchdog {

/// True when this build can run the watchdog at all (dev build, POSIX).
bool isSupported();

/// Starts the watchdog for the CALLING thread — which must be the UI thread,
/// because that is the thread the backtraces will be of. Ensures the heartbeat
/// probe is running (it is the input). Idempotent; a no-op when unsupported or
/// disabled by preference/flag. `stallMs` is clamped to >= 250.
void start(int stallMs = 2000);

/// Stops the watchdog thread (bounded join) and restores the previous SIGUSR2
/// disposition. Safe to call twice; safe when never started.
void stop();

bool isRunning();

/// {supported, running, enabled, stallMs, reports, lastStallMs}.
/// `reports` is how many stalls this session actually reported — the number
/// the app.watchdog_stall gate asserts.
QVariantMap stats();

/// Turn it off for the rest of the process regardless of preference — what
/// `--watchdog=off` sets. Must be called before start() to prevent it, and
/// stops a running watchdog.
void disable();

// ---- The gate's lever (dev builds only) ------------------------------------
/// Blocks the calling thread for `ms` without servicing the event loop — the
/// deliberate freeze `app.blockUiThread(ms)` exposes so the watchdog can be
/// tested through the registry like everything else. Returns false (and does
/// nothing) in release builds.
bool blockCallingThread(int ms);

}   // namespace MainThreadWatchdog

#endif   // MAINTHREADWATCHDOG_H
