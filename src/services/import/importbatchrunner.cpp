/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/import/importbatchrunner.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QtConcurrent>

#include "services/import/assetimportservice.h"

ImportBatchRunner::ImportBatchRunner(Database *db, Project *project, QObject *parent)
    : QObject(parent), db(db), project(project),
      mService(new AssetImportService(db, project))
{
}

ImportBatchRunner::~ImportBatchRunner()
{
    delete mService;
}

void ImportBatchRunner::setRequests(const QVector<ImportRequest> &requests)
{
    mRequests = requests;
}

void ImportBatchRunner::start()
{
    Q_ASSERT(!mRunning);
    mRunning = true;
    // The future is deliberately unowned: the worker's last act is a queued
    // finished() emit, and the runner outlives it (parented to the view).
    (void)QtConcurrent::run([this]() { runBatch(); });
}

void ImportBatchRunner::runBatch()
{
    const int total = mRequests.size();

    for (int i = 0; i < total; ++i) {
        if (mCancelled.load()) break;
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
        PreparedImport prepared = mService->prepare(request, workerProgress);

        // ---- DB half, blocking hop to the UI thread ----------------------
        // (the default QSqlDatabase connection, AssetManager registration and
        // drawer filing are all UI-thread-bound; one file in flight at a time
        // keeps the staging dir alive across its own commit only)
        QMetaObject::invokeMethod(this, [this, i, &prepared]() {
            ImportResult result;
            if (!prepared.ok()) {
                result = prepared.result;
            } else {
                auto uiProgress = [this, i](const QString &stage, int done, int stageTotal) -> bool {
                    emit stageProgress(i, stage, done, stageTotal);   // direct: UI thread
                    return !mCancelled.load();
                };
                result = mService->commit(prepared, uiProgress);
            }
            emit fileFinished(i, prepared.request, result);           // direct: UI thread
        }, Qt::BlockingQueuedConnection);
    }

    QMetaObject::invokeMethod(this, [this]() {
        mRunning = false;
        emit finished(mCancelled.load());
    }, Qt::QueuedConnection);
}
