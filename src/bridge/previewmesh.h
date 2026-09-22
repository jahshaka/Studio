/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef BRIDGE_PREVIEWMESH_H
#define BRIDGE_PREVIEWMESH_H

// A PREVIEW DOCK'S OWN FURNITURE (ATOM P2).
//
// The scene's meshes are baked library assets — the primitives, the Ground, the
// Teapot (services/primitiveassets.h) — and `iris::Mesh::loadMesh` is deleted:
// nothing in the product parses a model outside an import.
//
// A PREVIEW SUBJECT IS NOT ONE OF THEM, measured rather than assumed. The asset
// dock's high-poly sphere, the material dock's low-poly ball, the thumbnail
// renderer's sphere and the avatar room's cube are drawn at ONE distance in a
// small tile: a LOD chain, surface cards and an SDF buy them nothing, and making
// them library assets would make four preview-only test binaries — six source
// files and no catalog between them — depend on the whole library and import
// pipeline for a dock's sphere. So they are parsed HERE, once per process, on the
// importer's own parse entry point (`GraphicsHelper::loadAllMeshesFromFile`,
// which reads a Qt resource as well as a file), with no card generation behind it:
// the ~6 ms per node that used to run on the thread that draws is still gone.
//
// TWO PATHS, RESOURCE THEN FILE, because a shipped mesh is BOTH: compiled into
// the app's resources and copied beside the binary. Which one a given binary has
// depends on the .qrc files its target lists, so every caller states both, as the
// retired loadMesh callers did.

#include <QFileInfo>
#include <QList>
#include <QString>

#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/import/graphicshelper.h"

namespace previewmesh
{

/// The first mesh of `resourcePath`, or of `appRelativePath` under the app
/// folder when this binary carries no such resource. Null when neither is there
/// (a headless test with neither the .qrc nor the app tree) — the caller draws
/// nothing, which is what it did when the parse failed.
inline iris::MeshPtr load(const QString &resourcePath, const QString &appRelativePath)
{
    QList<iris::MeshPtr> meshes;
    if (QFileInfo::exists(resourcePath))
        meshes = iris::GraphicsHelper::loadAllMeshesFromFile(resourcePath);
    if (meshes.isEmpty() && !appRelativePath.isEmpty()) {
        const QString onDisk = IrisUtils::getAbsoluteAssetPath(appRelativePath);
        if (QFileInfo(onDisk).isFile())
            meshes = iris::GraphicsHelper::loadAllMeshesFromFile(onDisk);
    }
    return meshes.isEmpty() ? iris::MeshPtr() : meshes.first();
}

}   // namespace previewmesh

#endif   // BRIDGE_PREVIEWMESH_H
