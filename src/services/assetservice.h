/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETSERVICE_H
#define ASSETSERVICE_H

// AssetService — the asset-store operations (APP_ARCHITECTURE_AUDIT §3.3).
//
// Grows around AssetImporter (the first service to escape MainWindow's orbit,
// now living beside it in src/services/). Phase 1 exposes the headless mesh
// import; AssetView's add/delete/rename sweeps drain into it in later phases
// (audit §3.3 "AssetService" row). Constructor-injected, QObject-free.

#include <QString>

#include "assetimporter.h"

class Database;

class AssetService
{
public:
    explicit AssetService(Database *db) : db(db) {}

    /// Imports a mesh file (obj/fbx/dae/blend/glb/gltf) into the global asset
    /// store. Pure document/DB work — safe headless.
    AssetImporter::Result importMesh(const QString &filePath)
    {
        return AssetImporter::importMesh(filePath, db);
    }

private:
    Database *db;
};

#endif // ASSETSERVICE_H
