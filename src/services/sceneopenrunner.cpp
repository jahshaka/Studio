/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/sceneopenrunner.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>

#include "services/loadtimeline.h"

SceneOpenRunner::SceneOpenRunner(Database *db, Project *project, QObject *parent)
    : QObject(parent), db(db), project(project),
      mPrewarm(std::make_shared<iris::MeshPrewarm>())
{
}

SceneOpenRunner::~SceneOpenRunner()
{
    // A live worker writes into mPrewarm and re-enters the atomics — it must
    // be gone before we are. It never waits on the UI thread, so this join is
    // bounded by one file's parse.
    requestAbort();
    if (mFuture.isValid() && !mFuture.isFinished()) mFuture.waitForFinished();
}

void SceneOpenRunner::setPlan(const QStringList &modelPaths, const QVector<Slice> &slices,
                              const QString &label)
{
    mModelPaths = modelPaths;
    mSlices = slices;
    mLabel = label;
    mNextSlice = 0;
    mParsed.store(false);
    mAborted.store(false);
    mPrewarm = std::make_shared<iris::MeshPrewarm>();
}

void SceneOpenRunner::start()
{
    mRunning.store(true);
    emit progress(5, QStringLiteral("Reading %1…").arg(mLabel));
    if (mModelPaths.isEmpty()) {
        // Nothing to parse: go straight to the slices, still one per turn.
        mParsed.store(true);
        QTimer::singleShot(0, this, [this]() { runNextSlice(); });
        return;
    }
    mFuture = QtConcurrent::run([this]() { runWorker(); });
}

void SceneOpenRunner::runWorker()
{
    const int total = mModelPaths.size();
    for (int i = 0; i < total; ++i) {
        if (mAborted.load()) break;
        const QString path = mModelPaths.at(i);
        {
            LoadTimeline::Accumulate parse(QStringLiteral("worker:assimp"));
            mPrewarm->parse(path);
        }
        const int pct = 5 + (35 * (i + 1)) / total;
        QMetaObject::invokeMethod(this, [this, pct, total, i]() {
            emit progress(pct, QStringLiteral("Loading models (%1 of %2)…").arg(i + 1).arg(total));
        }, Qt::QueuedConnection);
    }
    mParsed.store(true);
    // Hand back through the event loop — NOT a blocking connection: a UI loop
    // that stopped pumping (the app quitting) must never be able to strand
    // this worker (the import.shutdown zombie).
    QMetaObject::invokeMethod(this, [this]() { runNextSlice(); }, Qt::QueuedConnection);
}

void SceneOpenRunner::runNextSlice()
{
    if (mAborted.load()) {
        mRunning.store(false);
        emit finished(true);
        return;
    }
    if (mNextSlice >= mSlices.size()) {
        mRunning.store(false);
        emit finished(false);
        return;
    }

    const Slice slice = mSlices.at(mNextSlice++);
    emit progress(slice.percent, slice.label);
    QElapsedTimer sliceTimer;
    sliceTimer.start();
    if (slice.run) slice.run();
    const double ms = double(sliceTimer.nsecsElapsed()) / 1.0e6;
    // A slice IS a block of UI-thread time: if one grows past the
    // responsiveness budget it has to be split, and this is how that gets
    // noticed instead of guessed.
    if (ms >= 200.0)
        qWarning("[open-profile] slow slice: %.0f ms (%s)", ms, qUtf8Printable(slice.label));

    // One slice per event-loop turn: the window paints, moves and answers
    // between them (ImportTailQueue's rule). A QPointer is NOT needed here —
    // the queued lambda targets `this` as its context object, so Qt drops it
    // if the runner dies first.
    //
    // ONE millisecond, not zero: a chain of zero timers is always "due", and
    // Qt's dispatcher will keep picking it over timers that are merely due
    // NOW — including the app's own render tick and anything else on a
    // deadline. Measured on the Showroom sample, the 1 ms gap between slices
    // cost nothing (14 slices) and stopped the slice chain from crowding the
    // rest of the loop.
    QTimer::singleShot(1, this, [this]() { runNextSlice(); });
}

bool SceneOpenRunner::waitForDone(int msTimeout)
{
    QPointer<SceneOpenRunner> self(this);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < msTimeout) {
        if (self.isNull() || !self->mRunning.load()) return true;
        // Service the worker's completion hop and the slice timers so a
        // healthy run drains through its normal path; user input stays out so
        // nothing re-enters the UI mid-shutdown.
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        if (self.isNull() || !self->mRunning.load()) return true;
        QThread::msleep(5);
    }
    return self.isNull() || !self->mRunning.load();
}
