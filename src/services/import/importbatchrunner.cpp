/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/import/importbatchrunner.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QSemaphore>
#include <QThread>
#include <QtConcurrent>
#include <memory>

#include "services/import/assetimportservice.h"

ImportBatchRunner::ImportBatchRunner(Database *db, Project *project, QObject *parent)
    : QObject(parent), db(db), project(project),
      mService(new AssetImportService(db, project))
{
}

ImportBatchRunner::~ImportBatchRunner()
{
    // A live worker re-enters this object (mService, the atomics, emits) —
    // it must be gone before we are. requestAbort makes the hop give up
    // within one slice; only a worker deep inside a long prepare (one big
    // assimp parse checks progress rarely) can hold this up, and the orderly
    // shutdown path (waitForDone + the force-exit guard in main) bounds that.
    requestAbort();
    if (mFuture.isValid() && !mFuture.isFinished()) mFuture.waitForFinished();
    delete mService;
}

void ImportBatchRunner::setRequests(const QVector<ImportRequest> &requests)
{
    mRequests = requests;
}

void ImportBatchRunner::start()
{
    Q_ASSERT(!mRunning.load());
    mRunning.store(true);
    // The future is KEPT (unlike the first version): waitForDone and the
    // destructor join on it so a worker can never outlive the runner.
    mFuture = QtConcurrent::run([this]() { runBatch(); });
}

bool ImportBatchRunner::waitForDone(int msTimeout)
{
    if (!mFuture.isValid()) return true;
    // Pumping events below can DESTROY this runner: the batch's finished()
    // handler nulls the owner's pointer and deleteLater()s us, and the
    // DeferredDelete is delivered by the very processEvents call we make.
    // A QPointer turns that into an observable end-of-life (the runner is
    // only deleted AFTER the worker finished, so vanishing == done); without
    // it the next mFuture.isFinished() reads freed memory — an exit-time
    // SIGSEGV in QFutureInterfaceBase::queryState.
    QPointer<ImportBatchRunner> self(this);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < msTimeout) {
        if (self.isNull()) return true;
        if (self->mFuture.isFinished()) return true;
        // Service the commit hop (and completion signals) so a healthy
        // worker drains CLEANLY through its normal path; user input stays
        // out so nothing re-enters the UI mid-shutdown.
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        if (self.isNull()) return true;
        if (self->mFuture.isFinished()) return true;
        QThread::msleep(10);
    }
    return self.isNull() || self->mFuture.isFinished();
}

void ImportBatchRunner::runBatch()
{
    const int total = mRequests.size();

    for (int i = 0; i < total; ++i) {
        if (mCancelled.load() || mAborted.load()) break;
        const ImportRequest request = mRequests.at(i);

        {
            const QString name = QFileInfo(request.sourcePath).fileName();
            QMetaObject::invokeMethod(this, [this, i, total, name]() {
                emit fileStarted(i, total, name);
            }, Qt::QueuedConnection);
        }

        // ---- CPU half, this worker thread --------------------------------
        auto workerProgress = [this, i](const QString &stage, int done, int stageTotal) -> bool {
            QMetaObject::invokeMethod(this, [this, i, stage, done, stageTotal]() {
                emit stageProgress(i, stage, done, stageTotal);
            }, Qt::QueuedConnection);
            return !mCancelled.load();
        };
        auto prepared = std::make_shared<PreparedImport>(
            mService->prepare(request, workerProgress));

        if (mAborted.load()) break;   // shutting down: nothing was committed

        // ---- DB half, shutdown-safe hop to the UI thread -----------------
        // (the default QSqlDatabase connection, AssetManager registration and
        // drawer filing are all UI-thread-bound; one file in flight at a time
        // keeps the staging dir alive across its own commit only.)
        //
        // NOT a Qt::BlockingQueuedConnection: that blocks unconditionally on
        // the UI event loop, and an event loop that has stopped pumping (the
        // app quitting underneath a running import) leaves the worker asleep
        // forever — QThreadPool's exit-time wait then never returns and the
        // process outlives its window. Instead the lambda releases a
        // semaphore and the worker waits in slices, abandoning when
        // requestAbort() flags shutdown. The shared_ptrs keep the prepared
        // plan and the semaphore alive for whichever side runs last.
        auto hopDone = std::make_shared<QSemaphore>();
        QMetaObject::invokeMethod(this, [this, i, prepared, hopDone]() {
            if (!mAborted.load()) {
                ImportResult result;
                if (!prepared->ok()) {
                    result = prepared->result;
                } else {
                    auto uiProgress = [this, i](const QString &stage, int done, int stageTotal) -> bool {
                        emit stageProgress(i, stage, done, stageTotal);   // direct: UI thread
                        return !mCancelled.load();
                    };
                    result = mService->commit(*prepared, uiProgress);
                }
                emit fileFinished(i, prepared->request, result);          // direct: UI thread
            }
            hopDone->release();
        }, Qt::QueuedConnection);

        bool committed = false;
        while (!(committed = hopDone->tryAcquire(1, 50))) {
            if (mAborted.load()) break;   // dead/stopped UI loop: abandon
        }
        if (!committed) break;
    }

    // Worker-side completion FIRST (isRunning()/waitForDone read it without
    // the event loop); the signal still arrives on the UI thread, queued.
    mRunning.store(false);
    QMetaObject::invokeMethod(this, [this]() {
        emit finished(mCancelled.load());
    }, Qt::QueuedConnection);
}
