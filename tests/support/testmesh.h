/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef TESTS_SUPPORT_TESTMESH_H
#define TESTS_SUPPORT_TESTMESH_H

// A SUITE'S FIXTURE GEOMETRY (SHIPPED-BAKES-1).
//
// The app parses nothing at run time: every mesh it draws is a bake
// (irisgl/document/assets/shippedmeshes.h, src/data/primitives.h). A suite that
// needs "some geometry on a node" has no library, no store and no bake, and
// standing one up would test the library instead of the subject — so it PARSES
// the fixture here, on the importer's own entry point. Tests only:
// `source.assimp_import_only` keeps this out of src/ and irisgl/.
//
// TWO PATHS, RESOURCE THEN FILE: a shipped mesh is compiled into the app's
// resources AND copied beside the binary, and which one a given test binary has
// depends on the .qrc files its target lists.
//
// `installShippedResolver` stands in for the app's seeded library behind
// IrisGL's shipped-mesh seam, so a suite that drives a preview dock or the VR
// controller slot gets geometry for the dock's seed keys: the resource when the
// binary carries it, else the same file under the app folder.

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QString>

#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/shippedmeshes.h"
#include "irisgl/import/graphicshelper.h"

namespace testmesh
{

/// The first mesh of `path` — a file or a ":/" resource. Null when it is not there.
inline iris::MeshPtr load(const QString &path)
{
    const QList<iris::MeshPtr> meshes = iris::GraphicsHelper::loadAllMeshesFromFile(path);
    return meshes.isEmpty() ? iris::MeshPtr() : meshes.first();
}

/// `resourcePath`, or `appRelativePath` under the app folder when this binary
/// carries no such resource.
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

/// A seed key (":/x/y.obj" or "app/x/y.obj") parsed from the fixture files,
/// held for the process like the app's own cache.
inline iris::MeshPtr seed(const QString &seedKey)
{
    static QHash<QString, iris::MeshPtr> held;
    const auto hit = held.constFind(seedKey);
    if (hit != held.constEnd()) return hit.value();
    iris::MeshPtr mesh;
    if (seedKey.startsWith(QLatin1Char(':')))
        mesh = load(seedKey, QStringLiteral("app") + seedKey.mid(1));
    else
        mesh = load(QString(), seedKey);
    if (mesh) held.insert(seedKey, mesh);
    return mesh;
}

/// Stand the fixture parse in for the app's seeded library behind the seam.
inline void installShippedResolver()
{
    iris::ShippedMeshes::setResolver([](const QString &key) { return seed(key); });
}

}   // namespace testmesh

#endif   // TESTS_SUPPORT_TESTMESH_H
