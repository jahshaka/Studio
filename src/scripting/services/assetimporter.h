/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_ASSETIMPORTER_H
#define SCRIPTING_ASSETIMPORTER_H

// AssetImporter — the headless mesh-import service (SCRIPTING_SPEC §1.3,
// extracted from AssetView::importModel's mesh branch).
//
// Same store layout and DB contract as the widget: one guid names the Object
// row, the AssetStore/<guid> folder and the parent of every member row; the
// Object row alone gets AssetViewFilter::AssetsView; the blob's mesh path and
// texture references are rewritten to guids; Object->Mesh and Object->Texture
// dependency rows link the members. Referenced on-disk textures are copied and
// registered; embedded textures are not extracted (v1, same bytes-on-disk gap
// as the widget). No viewer, no dialogs — the thumbnail is the caller's job
// (assets.refreshThumbnail).

#include <QString>

#include "../../irisgl/src/irisglfwd.h"

class Database;

class AssetImporter
{
public:
    struct Result
    {
        QString objectGuid;     // empty on failure
        QString meshGuid;
        QString error;
        iris::SceneNodePtr node;   // the imported (guid-rewritten) scene fragment
        bool ok() const { return !objectGuid.isEmpty(); }
    };

    /// Imports a mesh file (obj/fbx/dae/blend/glb/gltf) into the global asset
    /// store. Pure document/DB work — safe headless.
    static Result importMesh(const QString &filePath, Database *db);
};

#endif // SCRIPTING_ASSETIMPORTER_H
