/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef IMPORTVIEWERTAIL_H
#define IMPORTVIEWERTAIL_H

// The import completion tail, off the UI thread's critical path.
//
// The threaded import pipeline (ImportBatchRunner) leaves one genuinely
// UI/engine-bound piece per asset: the viewer preview + rendered thumbnail.
// Running those synchronously for a whole batch after the progress dialog
// closed froze the app long enough for the OS "application unresponsive"
// dialog (owner-reported; measured seconds for one large GLB). Two fixes
// live here:
//
//  * ImportMeshTail::run consumes the PIPELINE'S OWN parsed fragment
//    (ImportResult::node, produced by MeshImporter::convert on the worker)
//    instead of re-parsing the stored model with assimp a second time — the
//    engine upload + offscreen render is all that remains on the UI thread.
//    The fragment is deep-duplicated first: the same node instance is the
//    session-registered asset, and the viewer's preview-material conversion
//    mutates materials in place.
//
//  * ImportTailQueue runs one tail item per EVENT-LOOP TURN (queued
//    single-shots), so the window keeps painting, clicking and moving
//    between items — tiles update live, one by one, the way the media
//    tiles already did mid-batch.

#include <QImage>
#include <QObject>
#include <QVector>
#include <functional>

#include "services/import/importtypes.h"

class Database;
class IAssetViewer;

/// Runs queued tasks one per event-loop turn. progress() fires before each
/// task with (done, total); finished() fires once the queue drains.
class ImportTailQueue : public QObject
{
    Q_OBJECT

public:
    explicit ImportTailQueue(QObject *parent = nullptr);

    void enqueue(const std::function<void()> &task);
    /// Begin (or continue) pumping. Safe to call while running.
    void start();
    /// Drop every pending task (the currently running one completes).
    void clear();

    bool isRunning() const { return mRunning; }
    int pendingCount() const { return mTasks.size(); }

signals:
    void progress(int done, int total);
    void finished();

private:
    void pumpOne();

    QVector<std::function<void()>> mTasks;
    int mDone = 0;
    bool mRunning = false;
};

/// The per-mesh tail body: preview load + rendered thumbnail + the row's
/// camera properties. UI/engine-thread work by nature (the engine renders,
/// the default DB connection commits) — but ONE item's worth, no parse.
namespace ImportMeshTail
{
    struct Outcome
    {
        /// True when ImportResult::node (the worker-parsed fragment) fed the
        /// viewer directly — the no-second-parse path.
        bool usedPreparedNode = false;
        /// The rendered 512x512 thumbnail; null when the viewer cannot
        /// render (headless stand-in).
        QImage snapshot;
    };

    Outcome run(Database *db, IAssetViewer *viewer,
                const ImportResult &result, const QString &fileName);
}

#endif // IMPORTVIEWERTAIL_H
