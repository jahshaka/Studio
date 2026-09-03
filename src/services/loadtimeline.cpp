/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/loadtimeline.h"

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

void end()
{
    QString line;
    {
        QMutexLocker locked(&lock());
        Run &r = run();
        if (!r.running) return;
        closeStageLocked();
        r.running = false;

        const double total = double(r.wall.nsecsElapsed()) / 1.0e6;

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
        }
        for (auto it = r.counters.constBegin(); it != r.counters.constEnd(); ++it) {
            QVariantMap entry;
            entry["stage"] = QStringLiteral("counter:") + it.key();
            entry["ms"] = it.value().ms;
            entry["items"] = it.value().items;
            out.append(entry);
            line += QStringLiteral("\n[open-profile]   (of which) %1 %2 ms over %3")
                        .arg(it.key(), -22).arg(it.value().ms, 8, 'f', 1).arg(it.value().items);
        }
        lastRunStore() = out;
    }
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
