/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/loadtimeline.h"

#include "services/jahlog.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QVariantMap>
#include <QVector>

namespace {

struct Stage
{
    QString name;
    double  ms = 0.0;
};

struct Counter
{
    double ms = 0.0;
    int    items = 0;
};

struct Run
{
    bool           running = false;
    QString        label;
    QElapsedTimer  wall;
    qint64         stageStartNs = 0;
    QVector<Stage> stages;
    QMap<QString, Counter> counters;
};

Run &run()
{
    static Run r;
    return r;
}

/// One lock for the whole ledger: the open runner's worker adds to counters
/// while the UI thread marks stages (LOAD_ASYNC: the prepare phase's assimp
/// parses are timed on the worker).
QMutex &lock()
{
    static QMutex m;
    return m;
}

QVariantList &lastRunStore()
{
    static QVariantList list;
    return list;
}

/// The shell's scene-stats hook (LoadTimeline::setStatsProvider). Guarded by
/// the ledger lock, called OUTSIDE it.
std::function<QStringList()> &statsProvider()
{
    static std::function<QStringList()> p;
    return p;
}

/// Caller holds the lock.
void closeStageLocked()
{
    Run &r = run();
    if (r.stages.isEmpty()) return;
    const qint64 now = r.wall.nsecsElapsed();
    r.stages.last().ms = double(now - r.stageStartNs) / 1.0e6;
    r.stageStartNs = now;
}

}   // namespace

namespace LoadTimeline {

void begin(const QString &label)
{
    QMutexLocker locked(&lock());
    Run &r = run();
    r.running = true;
    r.label = label;
    r.stages.clear();
    r.counters.clear();
    r.wall.start();
    r.stageStartNs = 0;
}

void mark(const QString &stage)
{
    QMutexLocker locked(&lock());
    Run &r = run();
    if (!r.running) return;
    closeStageLocked();
    r.stages.append({ stage, 0.0 });
}

void add(const QString &counter, double ms, int items)
{
    QMutexLocker locked(&lock());
    Run &r = run();
    if (!r.running) return;
    Counter &c = r.counters[counter];
    c.ms += ms;
    c.items += items;
}

void setStatsProvider(std::function<QStringList()> provider)
{
    QMutexLocker locked(&lock());
    statsProvider() = std::move(provider);
}

void end()
{
    QString line;
    QString label;
    double total = 0.0;
    QStringList blockLines;
    std::function<QStringList()> provider;
    {
        QMutexLocker locked(&lock());
        Run &r = run();
        if (!r.running) return;
        closeStageLocked();
        r.running = false;

        total = double(r.wall.nsecsElapsed()) / 1.0e6;
        label = r.label;

        QVariantList out;
        {
            QVariantMap head;
            head["stage"] = QStringLiteral("total");
            head["ms"] = total;
            head["label"] = r.label;
            out.append(head);
        }
        line = QStringLiteral("[open-profile] %1 — %2 ms total")
                   .arg(r.label).arg(total, 0, 'f', 1);
        for (const Stage &s : r.stages) {
            QVariantMap entry;
            entry["stage"] = s.name;
            entry["ms"] = s.ms;
            out.append(entry);
            line += QStringLiteral("\n[open-profile]   %1 %2 ms")
                        .arg(s.name, -34).arg(s.ms, 8, 'f', 1);
            blockLines << QStringLiteral("%1 %2 ms").arg(s.name, -34).arg(s.ms, 8, 'f', 1);
        }
        for (auto it = r.counters.constBegin(); it != r.counters.constEnd(); ++it) {
            QVariantMap entry;
            entry["stage"] = QStringLiteral("counter:") + it.key();
            entry["ms"] = it.value().ms;
            entry["items"] = it.value().items;
            out.append(entry);
            line += QStringLiteral("\n[open-profile]   (of which) %1 %2 ms over %3")
                        .arg(it.key(), -22).arg(it.value().ms, 8, 'f', 1).arg(it.value().items);
            blockLines << QStringLiteral("(of which) %1 %2 ms over %3")
                              .arg(it.key(), -22).arg(it.value().ms, 8, 'f', 1).arg(it.value().items);
        }
        lastRunStore() = out;
        provider = statsProvider();
    }

    // THE SCENE OPEN BLOCK (SESSION_LOG_SPEC §5). This is the choke point every
    // open path funnels through — a tile click, project.open, openAsync, the
    // sync path and import all call begin() and exactly one place calls end() —
    // so a UI-driven open and a scripted one produce the identical record.
    //
    // OUTSIDE THE LEDGER LOCK, for the same reason the qDebug it replaces was
    // (spec §9-R7): emitting a multi-line block while holding the timeline lock
    // would put the log mutex inside the timeline mutex, and LoadTimeline::add()
    // — which workers call — can invert that order.
    //
    // The stats walk runs here too, AFTER the total is banked, so the cost of
    // counting the scene can never appear in the ledger it accompanies (§9-R8).
    if (provider) blockLines << provider();
    JahLog::writeBlock(JahLog::scene, JahLog::Level::Display,
                       QStringLiteral("=== SCENE OPEN === '%1' in %2 ms")
                           .arg(label).arg(total, 0, 'f', 1),
                       blockLines);

    // The qDebug stays: it is what the heartbeat e2e and the profiling tables
    // read, it is bit-identical on stderr, and the funnel does NOT double it
    // into the file (qDebug maps to Verbose, and `qt` sits at Log).
    qDebug().noquote() << line;
}

bool isRunning()
{
    QMutexLocker locked(&lock());
    return run().running;
}

QString currentStage()
{
    QMutexLocker locked(&lock());
    Run &r = run();
    if (!r.running || r.stages.isEmpty()) return QString();
    return r.stages.last().name;
}

double elapsedMs()
{
    QMutexLocker locked(&lock());
    Run &r = run();
    return r.running ? double(r.wall.nsecsElapsed()) / 1.0e6 : 0.0;
}

QVariantList lastRun()
{
    QMutexLocker locked(&lock());
    return lastRunStore();
}

Accumulate::Accumulate(const QString &counter, int items)
    : mCounter(counter), mItems(items), mStartNs(0), mActive(false)
{
    QMutexLocker locked(&lock());
    if (!run().running) return;
    mActive = true;
    mStartNs = run().wall.nsecsElapsed();
}

Accumulate::~Accumulate()
{
    stop();
}

void Accumulate::stop()
{
    if (!mActive) return;
    mActive = false;
    double ms = 0.0;
    {
        QMutexLocker locked(&lock());
        if (!run().running) return;
        ms = double(run().wall.nsecsElapsed() - mStartNs) / 1.0e6;
    }
    add(mCounter, ms, mItems);
}

}   // namespace LoadTimeline
