/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef THUMBNAILGENERATOR_H
#define THUMBNAILGENERATOR_H

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/materials/defaultmaterial.h"

#include <QObject>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QSize>
#include <memory>

#include "data/database/database.h"

class QTimer;
class Project;
class EngineThumbnailRenderer;

enum class ThumbnailRequestType
{
    Material,
    Mesh,
    ImportedMesh // Stuff that's already in the app to refresh previews
};

struct ThumbnailRequest
{
    ThumbnailRequestType type;
    QString path;
    QString id;
    bool preview;
};

struct ThumbnailResult
{
    ThumbnailRequestType type;
    bool preview;
    QString path;
    QString id;
    QImage thumbnail;
};

// Thumbnails are rendered on the MAIN thread through an offscreen engine View
// (EngineThumbnailRenderer), at most one request per timer tick so the UI never
// blocks. (The legacy GL RenderThread died at step 14; the queue contract —
// requestThumbnail() in, thumbnailComplete() out — is unchanged.)
class ThumbnailGenerator : public QObject
{
    Q_OBJECT
public:
    ~ThumbnailGenerator() override;
    static ThumbnailGenerator* getSingleton();
    void requestThumbnail(ThumbnailRequestType type, QString path, QString id = "", bool preview = false,
                          QSize size = QSize(512, 512));

    // must be called to properly shutdown ui components
    void shutdown();

    Database *db;
    void setDatabase(Database *db) {
        this->db = db;
    }

    /// The live Project (Phase 4: was the Globals::project static). Nullable —
    /// the ImportedMesh path already null-checked it and returns an empty
    /// QImage when it is not set.
    Project *project = nullptr;
    void setProject(Project *p) {
        this->project = p;
    }

    /// True when thumbnails are rendered through the engine on the main thread.
    bool usesEngine() const { return true; }
    /// Requests still waiting for a tick.
    int pendingCount() const { return pending.size(); }

signals:
    void thumbnailComplete(ThumbnailResult* result);

private:
	static ThumbnailGenerator* instance;
	ThumbnailGenerator();

    // ---- engine path ----
    struct EngineRequest { ThumbnailRequest request; QSize size; };
    void processOneEngineRequest();
    QImage renderEngineRequest(const ThumbnailRequest &request, QSize size);
    iris::MaterialPtr previewMaterialFor(iris::MaterialPtr material);

    QList<EngineRequest> pending;
    QTimer *tick = nullptr;
    std::unique_ptr<EngineThumbnailRenderer> engineRenderer;
};

#endif // THUMBNAILGENERATOR_H
