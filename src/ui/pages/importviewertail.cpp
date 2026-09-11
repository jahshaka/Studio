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
#include <QTimer>
#include <QDebug>

#include "data/database/database.h"
#include <QSqlDatabase>

#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "ui/pages/iassetviewer.h"

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

    // THE LIBRARY LOAD — the committed asset, not the import fragment (see the
    // header). The viewer resolves the row's blob through the store and applies
    // the asset's fit, which is what makes this preview agree with a tile
    // double-click, with the thumbnail, and with the editor's drop.
    QString storedModel = AssetCas::resolveFile(
        QSqlDatabase::database(), AssetStorePaths::root(),
        guid, QFileInfo(fileName).fileName());
    if (storedModel.isEmpty())
        storedModel = AssetCas::resolveSource(
            QSqlDatabase::database(), AssetStorePaths::root(), guid);
    viewer->loadModel(storedModel, guid);
    outcome.previewed = true;

    if (db) {
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
            << "library-preview" << timer.elapsed() << "ms";
    return outcome;
}
