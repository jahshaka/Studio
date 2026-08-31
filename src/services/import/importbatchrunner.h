/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef IMPORTBATCHRUNNER_H
#define IMPORTBATCHRUNNER_H

// ImportBatchRunner — the interactive (threaded) driver of the import
// pipeline. The UI freeze fix: AssetImportService::prepare (assimp parse,
// texture extraction, content hashing — the expensive half) runs on a
// QtConcurrent worker while the UI thread keeps its event loop; the store +
// register half hops back to the UI thread per file (the default QSqlDatabase
// connection and the session AssetManager are UI-thread-bound).
//
//   worker:  prepare(file 0) ──hop──▶ UI: commit(file 0), fileFinished
//            prepare(file 1) ──hop──▶ UI: commit(file 1), fileFinished
//            …                        UI: finished(cancelled)
//
// The hop is a Qt::BlockingQueuedConnection invoke — one file is in flight at
// a time, so a multi-file drop is one dialog counting "N of M" and the
// staging dir of each PreparedImport lives exactly until its commit returns.
//
// Cancel: cancel() flips an atomic the ImportProgressFn checks — during
// prepare the convert aborts with nothing written; during commit the spine's
// existing contract rolls the transaction back and removes this import's
// orphaned CAS objects. Files already committed stay imported (per-file
// transactions — matching the old sequential behavior).
//
// Headless/verb imports (assets.importFile) do NOT come through here — they
// call AssetImportService::import synchronously, dialog-free, as before.

#include <QObject>
#include <QVector>
#include <atomic>

#include "services/import/importtypes.h"

class Database;
class Project;
class AssetImportService;

class ImportBatchRunner : public QObject
{
    Q_OBJECT

public:
    ImportBatchRunner(Database *db, Project *project, QObject *parent = nullptr);
    ~ImportBatchRunner() override;

    /// Queue the batch. Call before start(); requests are fixed afterwards.
    void setRequests(const QVector<ImportRequest> &requests);
    const QVector<ImportRequest> &requests() const { return mRequests; }

    /// Launch the worker. Must be called on the UI thread (the thread that
    /// owns the default DB connection — commits hop back to it).
    void start();

    /// Request cancellation: the current stage's next progress callback
    /// returns false. Safe from any thread (the dialog's Cancel button).
    void cancel() { mCancelled.store(true); }
    bool wasCancelled() const { return mCancelled.load(); }
    bool isRunning() const { return mRunning; }

signals:
    /// A file's pipeline began (worker thread → queued to the UI).
    void fileStarted(int index, int total, const QString &fileName);
    /// Stage granularity within one file ("sniff"/"convert"/"textures"/
    /// "hash"/"store"; total 0 = indeterminate).
    void stageProgress(int index, const QString &stage, int done, int total);
    /// Emitted ON THE UI THREAD right after the file's commit (or its
    /// prepare failure) — tile adds and other UI tails connect here.
    void fileFinished(int index, const ImportRequest &request, const ImportResult &result);
    /// The batch is done (UI thread). cancelled = a cancel stopped it early.
    void finished(bool cancelled);

private:
    void runBatch();   // worker-thread body

    Database *db;
    Project *project;
    AssetImportService *mService;
    QVector<ImportRequest> mRequests;
    std::atomic<bool> mCancelled { false };
    bool mRunning = false;
};

#endif // IMPORTBATCHRUNNER_H
