/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/pages/importviewertail.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QTimer>
#include <QDebug>

#include "data/database/database.h"
#include <QSqlDatabase>

#include "services/assetcas.h"
#include "services/assethelper.h"
#include "services/assetstorepaths.h"
#include "ui/pages/iassetviewer.h"
#include "irisgl/document/scenegraph/scenenode.h"

// ---- ImportTailQueue -------------------------------------------------------

ImportTailQueue::ImportTailQueue(QObject *parent) : QObject(parent) {}

void ImportTailQueue::enqueue(const std::function<void()> &task)
{
    mTasks.append(task);
}

void ImportTailQueue::start()
{
    if (mRunning) return;
    mRunning = true;
    QTimer::singleShot(0, this, [this]() { pumpOne(); });
}

void ImportTailQueue::clear()
{
    mTasks.clear();
}

void ImportTailQueue::pumpOne()
{
    if (mTasks.isEmpty()) {
        mRunning = false;
        mDone = 0;
        emit finished();
        return;
    }

    const int total = mDone + mTasks.size();
    emit progress(mDone, total);

    const auto task = mTasks.takeFirst();
    task();
    ++mDone;

    // Yield the event loop between items: paints, clicks and moves happen
    // BETWEEN tails — the whole point of the queue.
    QTimer::singleShot(0, this, [this]() { pumpOne(); });
}

// ---- ImportMeshTail --------------------------------------------------------

ImportMeshTail::Outcome ImportMeshTail::run(Database *db, IAssetViewer *viewer,
                                            const ImportResult &result,
                                            const QString &fileName)
{
    Outcome outcome;
    if (!viewer) return outcome;
    const QString guid = result.assetGuid;

    QElapsedTimer timer;
    timer.start();

    if (result.node) {
        // The pipeline's convert already parsed the model on the worker and
        // registerSession re-pointed its textures at durable CAS paths — no
        // second assimp parse. Deep-duplicate for the viewer: the original
        // is the session-registered asset, and the preview-material
        // conversion (EngineThumbnailRenderer::previewMaterials via
        // addNodeToScene) swaps materials in place.
        auto previewNode = result.node->duplicate();
        viewer->addNodeToScene(previewNode, guid, false, true);
        outcome.usedPreparedNode = true;
    }
    else {
        // No fragment came through (older path, .jaf-shaped callers):
        // the stored model loads through the viewer's reader as before —
        // resolved by guid through the CAS, not from the retired per-guid
        // view (deep audit 2026-09, area 6).
        QString storedModel = AssetCas::resolveFile(
            QSqlDatabase::database(), AssetStorePaths::root(),
            guid, QFileInfo(fileName).fileName());
        if (storedModel.isEmpty())
            storedModel = AssetCas::resolveSource(
                QSqlDatabase::database(), AssetStorePaths::root(), guid);
        viewer->loadModel(storedModel, guid);
    }

    outcome.snapshot = viewer->takeScreenshot(512, 512);

    if (db) {
        if (!outcome.snapshot.isNull())
            db->updateAssetThumbnail(guid, AssetHelper::makeBlobFromPixmap(
                                               QPixmap::fromImage(outcome.snapshot)));

        // Camera/orbit properties merged WITHOUT clobbering the pipeline's
        // "metadata"/"import" blocks (ASSETS_AUDIT.md finding 5).
        QJsonObject properties =
            QJsonDocument::fromJson(db->fetchAsset(guid).properties).object();
        const QJsonObject cameraProps = viewer->getSceneProperties();
        for (auto it = cameraProps.constBegin(); it != cameraProps.constEnd(); ++it)
            properties[it.key()] = it.value();
        db->updateAssetProperties(guid, QJsonDocument(properties).toJson());
    }

    qInfo() << "import tail:" << QFileInfo(fileName).fileName()
            << (outcome.usedPreparedNode ? "prepared-node" : "reader-load")
            << timer.elapsed() << "ms";
    return outcome;
}
