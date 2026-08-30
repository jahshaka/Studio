/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <QQuaternion>
#include "services/thumbnailgenerator.h"

#include <QJsonDocument>
#include <QtMath>
#include <QStandardPaths>
#include <QTimer>

#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"

#include "data/constants.h"
#include "io/assetmanager.h"
#include "io/scenereader.h"
#include "io/materialreader.h"
#include "bridge/enginehost.h"
#include "bridge/enginethumbnailrenderer.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"

ThumbnailGenerator* ThumbnailGenerator::instance = nullptr;

ThumbnailGenerator::ThumbnailGenerator()
{
    // Requests are drained one per tick on the main thread once the Engine is up
    // (EngineHost starts it after this singleton may already exist).
    tick = new QTimer(this);
    tick->setInterval(16);
    QObject::connect(tick, &QTimer::timeout, [this] { processOneEngineRequest(); });
}

ThumbnailGenerator::~ThumbnailGenerator() = default;

ThumbnailGenerator *ThumbnailGenerator::getSingleton()
{
    if (instance == Q_NULLPTR) instance = new ThumbnailGenerator();
    return instance;
}

void ThumbnailGenerator::requestThumbnail(ThumbnailRequestType type, QString path, QString id, bool preview,
                                          QSize size)
{
    ThumbnailRequest req;
    req.type	= type;
    req.path	= path;
    req.id		= id;
    req.preview = preview;
    pending.append({req, size});
    if (tick && !tick->isActive()) tick->start();
}

void ThumbnailGenerator::shutdown()
{
    // Must run while the Engine is alive (EngineHost::shutdown() comes after
    // the main window is gone); the renderer checks anyway.
    if (tick) tick->stop();
    pending.clear();
    engineRenderer.reset();
}

// ---------------------------------------------------------------------------
// Engine path (main thread)
// ---------------------------------------------------------------------------

void ThumbnailGenerator::processOneEngineRequest()
{
    if (pending.isEmpty()) { if (tick) tick->stop(); return; }
    // Re-entrancy guard: a result handler may open a modal dialog (material preview
    // export), whose nested event loop would tick us again mid-render.
    static bool inFlight = false;
    if (inFlight) return;
    struct Guard { Guard() { inFlight = true; } ~Guard() { inFlight = false; } } guard;
    auto engine = EngineHost::instance().engine();
    if (!engine) {
        // Engine not started yet (or a headless run where it never starts):
        // keep a bounded backlog until it comes up.
        if (!EngineHost::instance().isRunning()) { pending.clear(); if (tick) tick->stop(); return; }
        while (pending.size() > 256) pending.removeFirst();
        return;
    }
    if (!engineRenderer) engineRenderer.reset(new EngineThumbnailRenderer(engine));
    // One request per tick: never block the UI for a batch.
    const EngineRequest job = pending.takeFirst();
    QImage img = renderEngineRequest(job.request, job.size);
    auto result = new ThumbnailResult;
    result->id          = job.request.id;
    result->type        = job.request.type;
    result->path        = job.request.path;
    result->preview     = job.request.preview;
    result->thumbnail   = img;
    // Deliver from the event loop, not from inside this tick: receivers may block
    // (a save dialog) and must never re-enter the renderer.
    QMetaObject::invokeMethod(this, [this, result] { emit thumbnailComplete(result); },
                              Qt::QueuedConnection);
}

iris::MaterialPtr ThumbnailGenerator::previewMaterialFor(iris::MaterialPtr material)
{
    // Shared with the mesh path: colours AND textures survive the conversion
    // (a colour-only downgrade rendered every textured material grey).
    return EngineThumbnailRenderer::previewMaterialFor(material);
}

QImage ThumbnailGenerator::renderEngineRequest(const ThumbnailRequest &request, QSize size)
{
    if (request.type == ThumbnailRequestType::ImportedMesh) {
        if (!db || !Globals::project) return QImage();
        QJsonDocument document = QJsonDocument::fromJson(db->fetchAssetData(request.id));
        SceneReader reader;
        reader.setDatabaseHandle(db);
        reader.setBaseDirectory(IrisUtils::join(Globals::project->getProjectFolder()));
        QJsonObject objectHierarchy = document.object();
        auto node = reader.readSceneNode(objectHierarchy);
        if (!node) return QImage();
        return engineRenderer->renderNode(node, size);
    }

    if (request.type == ThumbnailRequestType::Mesh) {
        iris::SceneSource source;   // owns the assimp importer for the load's duration
        auto node = iris::MeshNode::loadAsSceneFragment(request.path,
            [](iris::MeshPtr, iris::MeshMaterialData &data)
        {
            // Colours AND texture maps — the asset's real look, not a grey stand-in.
            return EngineThumbnailRenderer::previewMaterialForMeshData(data);
        }, &source);
        if (!node) return QImage();
        return engineRenderer->renderNode(node, size);
    }

    if (request.type == ThumbnailRequestType::Material) {
        QFile file(request.path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QImage();
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        MaterialReader reader;
        auto material = reader.parseMaterial(doc.object(), db);
        return engineRenderer->renderMaterial(previewMaterialFor(material.staticCast<iris::Material>()), size);
    }
    return QImage();
}
