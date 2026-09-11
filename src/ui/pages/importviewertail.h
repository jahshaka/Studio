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
//  * ImportMeshTail::run previews THE ASSET THE LIBRARY STORED, through the
//    viewer's ordinary library load (IAssetViewer::loadModel -> the stored
//    blob, textures resolved through the store, the asset's fit applied) —
//    the very path a tile double-click takes, so a just-imported asset and a
//    re-selected one are the same picture.
//
//    IT USED TO RENDER `ImportResult::node->duplicate()`, the LIVE import
//    fragment, whose material paths point into the import's staging directory
//    — deleted by the time this runs. That white render was then PERSISTED as
//    the asset's thumbnail, which is the owner's "GLB imports show no textures
//    in the Assets tiles/thumbnails/preview while the editor drop has them"
//    (smoke S6, 2026-09-11). The deleted optimisation was one assimp parse per
//    import; the price of it was a white thumbnail that never healed.
//
//  * ImportTailQueue runs one tail item per EVENT-LOOP TURN (queued
//    single-shots), so the window keeps painting, clicking and moving
//    between items — tiles update live, one by one, the way the media
//    tiles already did mid-batch.

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

/// The per-mesh tail body: the library preview + the row's camera properties.
/// One item's worth of UI/engine-thread work.
///
/// The THUMBNAIL is not written here: it is assetthumb::storeObject (the one
/// routine `assets.refreshThumbnail` uses), called by the page — so a page
/// import and a scripted import store the same image instead of two renders
/// of two different nodes.
namespace ImportMeshTail
{
    struct Outcome
    {
        /// True when the stored asset was found and handed to the viewer.
        bool previewed = false;
    };

    Outcome run(Database *db, IAssetViewer *viewer,
                const ImportResult &result, const QString &fileName);
}

#endif // IMPORTVIEWERTAIL_H
