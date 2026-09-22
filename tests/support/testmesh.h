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

// A MESH FROM A FILE, FOR A TEST THAT HAS NO LIBRARY.
//
// `iris::Mesh::loadMesh` is DELETED (ATOM P2): nothing in the product parses a
// model outside an import any more, and the meshes that used to be born that way
// — the primitives, the Ground, the preview spheres — are baked library assets
// (jahshaka/src/services/primitiveassets.h). Plenty of suites, though, only need
// SOME geometry on a node: the mirror's plumbing, the document's characterisation,
// the scene-graph benchmark. They have no database, no asset store and no bake,
// and standing one up would be testing the library instead of the thing under
// test.
//
// So they read a model file through the importer's own parse entry point — ONE
// parse site, the one an import uses (`GraphicsHelper::loadAllMeshesFromFile`,
// which handles a Qt resource path as well as a file on disk). This is a TEST
// helper on purpose: it is not a door the product may use, and there is nothing
// behind it that the product needs.

#include <QList>
#include <QString>

#include "irisgl/document/assets/mesh.h"
#include "irisgl/import/graphicshelper.h"

namespace testmesh
{

/// The first mesh of `path` (a file or a ":/" resource), or null.
inline iris::MeshPtr load(const QString &path)
{
    const QList<iris::MeshPtr> meshes = iris::GraphicsHelper::loadAllMeshesFromFile(path);
    return meshes.isEmpty() ? iris::MeshPtr() : meshes.first();
}

}   // namespace testmesh

#endif   // TESTS_SUPPORT_TESTMESH_H
