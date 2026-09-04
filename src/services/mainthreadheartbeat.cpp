/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/mainthreadheartbeat.h"

#include "services/loadtimeline.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>

#include <atomic>

namespace {

struct Probe
{
    QTimer       *timer = nullptr;
    QElapsedTimer sinceTick;
    int           intervalMs = 250;
    int           ticks = 0;
    double        maxGapMs = 0.0;
};

Probe &probe()
{
    static Probe p;
    return p;
}

// The off-thread view. A process-wide monotonic reference (started once, never
// restarted) so a watchdog thread can subtract two readings without touching
// the QElapsedTimer the UI thread is restarting under it.
QElapsedTimer &refClock()
{
    static QElapsedTimer c = []() { QElapsedTimer t; t.start(); return t; }();
    return c;
}

std::atomic<qint64> gLastTickNs { 0 };
std::atomic<bool>   gRunning { false };

}   // namespace

namespace MainThreadHeartbeat {

void start(int intervalMs)
{
    Probe &p = probe();
    p.intervalMs = qMax(10, intervalMs);
    p.ticks = 0;
    p.maxGapMs = 0.0;
    if (!p.timer) {
        // Parentless and never deleted: the probe outlives every page and
        // every project, and a singleton timer on the UI thread costs one
        // wakeup per interval.
        p.timer = new QTimer;
        QObject::connect(p.timer, &QTimer::timeout, []() {
            Probe &q = probe();
            const double gap = double(q.sinceTick.nsecsElapsed()) / 1.0e6;
            if (gap > q.maxGapMs) q.maxGapMs = gap;
            // A gap this size is a freeze the user can see. Name it: the
            // ledger's open stage (when an open is being measured) is usually
            // the answer, and "which stage" is the whole diagnostic.
            if (gap >= 400.0) {
                qWarning("[heartbeat] UI thread blocked %.0f ms (stage: %s)", gap,
                         LoadTimeline::currentStage().isEmpty()
                             ? "-" : qUtf8Printable(LoadTimeline::currentStage()));
            }
            ++q.ticks;
            q.sinceTick.restart();
            // Publish LAST: everything above is the UI thread's own
            // bookkeeping, and the watchdog only cares that a tick happened.
            gLastTickNs.store(refClock().nsecsElapsed(), std::memory_order_relaxed);
        });
    }
    p.timer->setInterval(p.intervalMs);
    p.sinceTick.start();
    gLastTickNs.store(refClock().nsecsElapsed(), std::memory_order_relaxed);
    gRunning.store(true, std::memory_order_relaxed);
    p.timer->start();
}

void stop()
{
    Probe &p = probe();
    // Cleared BEFORE the timer stops: a watchdog polling in between must never
    // see "running, and the last tick was ages ago" — that is a stall report
    // for a probe somebody just switched off.
    gRunning.store(false, std::memory_order_relaxed);
    if (p.timer) p.timer->stop();
}

qint64 lastTickNs() { return gLastTickNs.load(std::memory_order_relaxed); }
qint64 nowNs()      { return refClock().nsecsElapsed(); }
bool isRunningAtomic() { return gRunning.load(std::memory_order_relaxed); }

bool isRunning()
{
    Probe &p = probe();
    return p.timer && p.timer->isActive();
}

QVariantMap stats()
{
    Probe &p = probe();
    const bool running = isRunning();
    const double since = running ? double(p.sinceTick.nsecsElapsed()) / 1.0e6 : 0.0;
    QVariantMap out;
    out["running"] = running;
    out["intervalMs"] = p.intervalMs;
    out["ticks"] = p.ticks;
    // The gap in progress counts: a probe read from inside a long call must
    // not report a rosy maximum measured before the call started.
    out["maxGapMs"] = qMax(p.maxGapMs, since);
    out["sinceLastTickMs"] = since;
    return out;
}

}   // namespace MainThreadHeartbeat
