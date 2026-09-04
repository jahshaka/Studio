/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/mainthreadwatchdog.h"

#include "data/settingsmanager.h"
#include "services/loadtimeline.h"
#include "services/mainthreadheartbeat.h"

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

// The whole mechanism is POSIX (pthread_kill + sigaction + backtrace) and
// deliberately DEV-ONLY. Windows keeps the header and gets no-ops.
#if defined(QT_DEBUG) && !defined(_WIN32)
#  define JAH_WATCHDOG_ENABLED 1
#  include <csignal>
#  include <cstring>
#  include <execinfo.h>
#  include <pthread.h>
#  include <unistd.h>
#else
#  define JAH_WATCHDOG_ENABLED 0
#endif

namespace {

std::atomic<bool> gDisabled { false };
std::atomic<int>  gReports { 0 };
std::atomic<qint64> gLastStallMs { 0 };
std::atomic<int>  gStallMs { 2000 };

#if JAH_WATCHDOG_ENABLED

/// The absolute ceiling on reports per session. A wedged process must not be
/// able to spend its remaining life unwinding stacks; twenty reports is far
/// more than anyone reads and small enough to be free.
constexpr int  kMaxReportsPerSession = 20;
/// Even distinct stalls get a cooldown: a UI thread ticking once every three
/// seconds is one pathology, not fifty incidents.
constexpr qint64 kCooldownMs = 5000;

std::atomic<bool> gRunning { false };
std::atomic<bool> gStop { false };
pthread_t         gUiThread {};
// A POINTER, not a std::thread member: a joinable std::thread destroyed at
// static-destruction time calls std::terminate ("terminate called without an
// active exception"), and not every exit path reaches stop() at the same
// moment. A leaked-but-joined pointer cannot do that. stop() is additionally
// registered with atexit so the join happens before this TU's statics go.
std::thread      *gThread = nullptr;
struct sigaction  gPrevUsr2;
bool              gInstalled = false;
bool              gAtexitRegistered = false;

// ---------------------------------------------------------------------------
// THE HANDLER. Runs on the UI thread, inside whatever it is stuck in.
// async-signal-safe only: write(), backtrace(), backtrace_symbols_fd().
// ---------------------------------------------------------------------------
void writeStr(int fd, const char *s) { (void)!write(fd, s, strlen(s)); }

void stallHandler(int)
{
    writeStr(2, "[watchdog] --- UI-thread backtrace (decode: "
                "addr2line -Cfe ./Jahshaka <addr>) ---\n");
    void *frames[64];
    const int count = backtrace(frames, 64);
    if (count > 0) backtrace_symbols_fd(frames, count, 2);
    writeStr(2, "[watchdog] --- end of UI-thread backtrace ---\n");
}

void watchdogLoop()
{
    // Poll fast relative to the stall threshold: the report should land while
    // the thread is still stuck, not after it recovered.
    const int pollMs = 200;
    qint64 lastReportedTick = -1;      // the tick timestamp a report was made for
    qint64 lastReportAtMs = -kCooldownMs;

    while (!gStop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
        if (gStop.load(std::memory_order_relaxed)) break;
        if (!MainThreadHeartbeat::isRunningAtomic()) continue;

        const qint64 tick = MainThreadHeartbeat::lastTickNs();
        if (tick == 0) continue;
        const qint64 nowMs = MainThreadHeartbeat::nowNs() / 1000000;
        const qint64 stallMs = nowMs - tick / 1000000;
        if (stallMs < gStallMs.load(std::memory_order_relaxed)) continue;

        // ONE report per stall: the stall is identified by the tick it is
        // stuck behind, so a thread that never ticks again is reported once.
        if (tick == lastReportedTick) continue;
        if (gReports.load(std::memory_order_relaxed) >= kMaxReportsPerSession) continue;
        if (nowMs - lastReportAtMs < kCooldownMs) continue;

        lastReportedTick = tick;
        lastReportAtMs = nowMs;
        gLastStallMs.store(stallMs, std::memory_order_relaxed);
        gReports.fetch_add(1, std::memory_order_relaxed);

        // The human line comes from HERE, not the handler: this is an ordinary
        // thread and may allocate. The handler only writes the stack.
        const QString stage = LoadTimeline::currentStage();
        qWarning("[watchdog] UI thread stalled %lld ms (stage: %s) — signalling it "
                 "for a backtrace", static_cast<long long>(stallMs),
                 stage.isEmpty() ? "-" : qUtf8Printable(stage));
        // stderr is unbuffered, but qWarning goes through Qt's own formatting;
        // flush so the [warn] line cannot land after the handler's stack.
        std::fflush(nullptr);
        pthread_kill(gUiThread, SIGUSR2);
    }
    gRunning.store(false, std::memory_order_relaxed);
}

bool enabledByConfiguration()
{
    if (gDisabled.load(std::memory_order_relaxed)) return false;
    // `--watchdog=off` / `--watchdog=on`. Read from the argument list rather
    // than CliOptions on purpose: the watchdog is started from MainWindow's
    // constructor (the UI thread it watches is the one that runs it), and
    // MainWindow never sees CliOptions. Unknown flags are ignored by
    // CliOptions::parse, so this costs nobody anything.
    const QStringList args = QCoreApplication::arguments();
    if (args.contains(QLatin1String("--watchdog=off"))) return false;
    if (args.contains(QLatin1String("--watchdog=on"))) return true;
    // The preference, default ON in a dev build (this code does not compile in
    // a release one).
    if (SettingsManager *s = SettingsManager::getDefaultManager())
        return s->getValue("watchdog_enabled", true).toBool();
    return true;
}

#endif   // JAH_WATCHDOG_ENABLED

}   // namespace

namespace MainThreadWatchdog {

bool isSupported()
{
#if JAH_WATCHDOG_ENABLED
    return true;
#else
    return false;
#endif
}

void start(int stallMs)
{
#if JAH_WATCHDOG_ENABLED
    if (gRunning.load(std::memory_order_relaxed)) return;
    if (!enabledByConfiguration()) return;

    gStallMs.store(qMax(250, stallMs), std::memory_order_relaxed);
    gUiThread = pthread_self();

    // PRE-WARM. The first backtrace() in a process dlopen()s the unwinder and
    // allocates; the handler must never be the call that does it.
    {
        void *warm[4];
        (void)backtrace(warm, 4);
    }

    if (!gInstalled) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = stallHandler;
        sa.sa_flags = SA_RESTART;      // interrupted restartable syscalls resume
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGUSR2, &sa, &gPrevUsr2) == 0) gInstalled = true;
        else return;
    }

    // The input. Nothing else in a normal run starts the probe, and a watchdog
    // with no heartbeat would watch a clock that never moves.
    if (!MainThreadHeartbeat::isRunningAtomic()) MainThreadHeartbeat::start(250);

    gStop.store(false, std::memory_order_relaxed);
    gRunning.store(true, std::memory_order_relaxed);
    gThread = new std::thread(watchdogLoop);
    if (!gAtexitRegistered) {
        // Last line of defence: the CLI paths (--script, --dump-api-docs) never
        // close the window, so they never reach step 2 of the shutdown order.
        std::atexit([]() { MainThreadWatchdog::stop(); });
        gAtexitRegistered = true;
    }
#else
    Q_UNUSED(stallMs);
#endif
}

void stop()
{
#if JAH_WATCHDOG_ENABLED
    gStop.store(true, std::memory_order_relaxed);
    if (gThread) {
        if (gThread->joinable()) gThread->join();
        delete gThread;
        gThread = nullptr;
    }
    gRunning.store(false, std::memory_order_relaxed);
    if (gInstalled) { sigaction(SIGUSR2, &gPrevUsr2, nullptr); gInstalled = false; }
#endif
}

bool isRunning()
{
#if JAH_WATCHDOG_ENABLED
    return gRunning.load(std::memory_order_relaxed);
#else
    return false;
#endif
}

void disable()
{
    gDisabled.store(true, std::memory_order_relaxed);
    stop();
}

QVariantMap stats()
{
    QVariantMap m;
    m["supported"] = isSupported();
    m["running"] = isRunning();
    m["enabled"] = isSupported() && !gDisabled.load(std::memory_order_relaxed);
    m["stallMs"] = gStallMs.load(std::memory_order_relaxed);
    m["reports"] = gReports.load(std::memory_order_relaxed);
    m["lastStallMs"] = double(gLastStallMs.load(std::memory_order_relaxed));
    return m;
}

bool blockCallingThread(int ms)
{
#if JAH_WATCHDOG_ENABLED
    if (ms <= 0) return false;
    // A plain sleep, deliberately: no processEvents, no timer. The whole
    // point is a UI thread that cannot service its own loop.
    std::this_thread::sleep_for(std::chrono::milliseconds(qMin(ms, 60000)));
    return true;
#else
    Q_UNUSED(ms);
    return false;
#endif
}

}   // namespace MainThreadWatchdog
