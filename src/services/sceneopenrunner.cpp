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
#include "services/meshbakestore.h"
#include "irisgl/core/logger.h"
#include "services/assetstorepaths.h"
#include "services/openreads.h"
#include <QSqlDatabase>

SceneOpenRunner::SceneOpenRunner(Database *db, Project *project, QObject *parent)
    : QObject(parent), db(db), project(project),
      mPrewarm(std::make_shared<iris::MeshPrewarm>())
{
}

SceneOpenRunner::~SceneOpenRunner()
{
    // A live worker writes into mPrewarm and re-enters the atomics — it must
    // be gone before we are. It never waits on the UI thread, so this join is
    // bounded by one file's read (and the side reads by their queries).
    requestAbort();
    if (mFuture.isValid() && !mFuture.isFinished()) mFuture.waitForFinished();
    if (mSideFuture.isValid() && !mSideFuture.isFinished()) mSideFuture.waitForFinished();
}

void SceneOpenRunner::setPlan(const Reads &reads, const QVector<Slice> &slices, const QString &label)
{
    mReads = reads;
    mModelPaths.clear();
    mPlan.clear();
    mRebuild.clear();
    mSlices = slices;
    mLabel = label;
    mNextSlice = 0;
    mEmittedSlice = -1;
    mParsed.store(false);
    mAborted.store(false);
    mPrewarm = std::make_shared<iris::MeshPrewarm>();
}

void SceneOpenRunner::start()
{
    mRunning.store(true);
    emit progress(5, QStringLiteral("Reading %1…").arg(mLabel));
    if (!mReads.plan) {
        // Nothing to read: go straight to the slices, still one per turn.
        mParsed.store(true);
        QTimer::singleShot(0, this, [this]() { runNextSlice(); });
        return;
    }
    // THE SIDE READS FROM THE START, beside the plan and the bake read (finishWorker joins them).
    if (mReads.side)
        mSideFuture = QtConcurrent::run([this]() {
            openreads::ReadConnection conn(mReads.dbPath);
            if (!mAborted.load()) mReads.side(conn.db());
        });
    mFuture = QtConcurrent::run([this]() { runWorker(); });
}

void SceneOpenRunner::runWorker()
{
    QStringList clips;
    {
        openreads::ReadConnection conn(mReads.dbPath);
        mReads.plan(conn.db(), &mModelPaths, &clips);
        mRebuild = MeshBakeStore::staleJobsFor(conn.db(), mReads.root, mModelPaths + clips);
        if (mRebuild.isEmpty() || mAborted.load()) {
            readBakes(conn.db());
            finishWorker();
            return;
        }
    }
    // THE STALE BAKES FIRST (D1): rebuilt here, committed on the UI thread (the
    // catalog's writer), then this runner reads on a fresh worker.
    auto results = std::make_shared<QVector<MeshBakeStore::BakeResult>>();
    const int n = mRebuild.size();
    for (int i = 0; i < n; ++i) {
        if (mAborted.load()) break;
        QMetaObject::invokeMethod(this, [this, i, n]() {
            emit progress(5 + (20 * i) / qMax(1, n),
                          QStringLiteral("Rebuilding models (%1 of %2)…").arg(i + 1).arg(n));
        }, Qt::QueuedConnection);
        results->append(MeshBakeStore::runBake(mRebuild.at(i)));
    }
    QMetaObject::invokeMethod(this, [this, results]() {
        QSqlDatabase conn = QSqlDatabase::database();
        const QString root = AssetStorePaths::root();
        for (MeshBakeStore::BakeResult &result : *results) {
            QString error;
            if (!MeshBakeStore::commitBake(conn, root, result, &error))
                irisLog(QStringLiteral("open: a stale bake could not be rebuilt (%1)").arg(error));
        }
        mRebuild.clear();
        mFuture = QtConcurrent::run([this]() {
            openreads::ReadConnection read(mReads.dbPath);
            readBakes(read.db());
            finishWorker();
        });
    }, Qt::QueuedConnection);
}

void SceneOpenRunner::readBakes(QSqlDatabase &conn)
{
    {
        LoadTimeline::Accumulate plan(QStringLiteral("worker:plan"));
        mPlan.clear();
        mPlan.reserve(mModelPaths.size());
        for (const QString &path : mModelPaths)
            mPlan.append(MeshBakeStore::planFor(conn, mReads.root, path));
    }
    const int total = mPlan.size();   // the array indexed below, not its source
    for (int i = 0; i < total; ++i) {
        if (mAborted.load()) break;
        {
            LoadTimeline::Accumulate read(QStringLiteral("worker:bakeRead"));
            mPrewarm->parse(mPlan.at(i));
        }
        const int pct = 5 + (35 * (i + 1)) / total;
        QMetaObject::invokeMethod(this, [this, pct, total, i]() {
            emit progress(pct, QStringLiteral("Loading models (%1 of %2)…").arg(i + 1).arg(total));
        }, Qt::QueuedConnection);
    }
    LoadTimeline::add(QStringLiteral("worker:bakeHits"), 0.0, mPrewarm->bakedCount());
}

void SceneOpenRunner::finishWorker()
{
    // The side reads are the slices' input too: joined here, on the worker, never on the UI thread.
    if (mSideFuture.isValid()) mSideFuture.waitForFinished();
    mParsed.store(true);
    // Hand back through the event loop — NOT a blocking connection: a UI loop
    // that stopped pumping (the app quitting) must never be able to strand
    // this worker (the import.shutdown zombie).
    QMetaObject::invokeMethod(this, [this]() { runNextSlice(); }, Qt::QueuedConnection);
}

/// THE BOUNDARY BETWEEN TWO SLICES (lane OPEN-FRAMES-1): whatever the shell
/// asked to have run there — in the app, one rendered frame, falling back to
/// the engine's bare resource advance in a session with no viewport
/// (MainWindow::startOpenRun sets it). Counted, so that a suite can prove it
/// happened with no frame of its own (app.openStats().sliceBoundaries).
void SceneOpenRunner::crossSliceBoundary()
{
    if (!mSliceBoundary) return;
    ++mBoundaryRuns;
    mSliceBoundary();
}

void SceneOpenRunner::runNextSlice()
{
    if (mAborted.load()) {
        // EVEN AN ABANDONED INSTALL HAS TO HAND BACK WHAT IT ALLOCATED: the
        // slices that DID run uploaded meshes and textures, and an abort is
        // usually a close or a quit, i.e. exactly the moment no frame will ever
        // come again.
        crossSliceBoundary();
        mRunning.store(false);
        emit finished(true);
        return;
    }
    if (mNextSlice >= mSlices.size()) {
        crossSliceBoundary();
        // The run's reads (the parsed scene blob, the session rows) go with it: nothing reads them after the
        // last slice, and the next setPlan would only replace them.
        mReads = Reads();
        mSlices.clear();
        mNextSlice = 0;
        mRunning.store(false);
        emit finished(false);
        return;
    }

    const Slice slice = mSlices.at(mNextSlice);
    // Once per SLICE, not per turn: a step slice re-enters here every turn and its own progress (the
    // registration's 45 + 10 k / n) would otherwise be overwritten by the slice's start value each time —
    // the bar stepped back and forth on every turn (the merge read).
    if (mEmittedSlice != mNextSlice) {
        emit progress(slice.percent, slice.label);
        mEmittedSlice = mNextSlice;
    }
    QElapsedTimer sliceTimer;
    sliceTimer.start();
    bool done = true;
    if (slice.step) done = slice.step();
    else if (slice.run) slice.run();
    if (done) ++mNextSlice;
    const double ms = double(sliceTimer.nsecsElapsed()) / 1.0e6;
    // A slice IS a block of UI-thread time: if one grows past the
    // responsiveness budget it has to be split, and this is how that gets
    // noticed instead of guessed.
    if (ms >= 200.0)
        qWarning("[open-profile] slow slice: %.0f ms (%s)", ms, qUtf8Printable(slice.label));

    // AFTER THE SLICE, BEFORE THE TURN. The slice has just uploaded or freed
    // GPU resources; this is where the renderer is told to recycle them, so
    // that the install never depends on a frame that a busy event loop may
    // never let happen (see setSliceBoundary).
    crossSliceBoundary();

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

bool SceneOpenRunner::waitForDone(int msTimeout, int idleSleepMs)
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
        QThread::msleep(static_cast<unsigned long>(qMax(0, idleSleepMs)));
    }
    return self.isNull() || !self->mRunning.load();
}
