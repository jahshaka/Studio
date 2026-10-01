/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// THE SUITES' FIXTURE PARSE (SHIPPED-BAKES-1). The app never parses a model at
// run time — assimp is import-time only (`source.assimp_import_only`) — so the
// thin "parse a file into iris::Meshes" helper that used to live in IrisGL
// (import/graphicshelper) is not production code any more. It lives here, in
// the `jah_testmesh` library the suites that include testmesh.h link
// (tests/CMakeLists.txt), and is the PARSE TWIN the mesh bake's round trip is
// compared against: the same `new Mesh(aiMesh*)` + Mesh::extractSkeleton over
// the same choke point (readSceneFile, the canonical preset).

#include "testmesh.h"

#include "assimp/Importer.hpp"
#include "assimp/scene.h"

#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/import/importflags.h"
#include "irisgl/import/scenesource.h"

namespace testmesh
{

QList<iris::MeshPtr> fromScene(const aiScene *scene)
{
    QList<iris::MeshPtr> meshes;
    if (!scene) return meshes;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        aiMesh *m = scene->mMeshes[i];
        auto mesh = iris::MeshPtr(new iris::Mesh(m));
        if (m->HasBones()) mesh->setSkeleton(iris::Mesh::extractSkeleton(m, scene));
        meshes.append(mesh);
    }
    return meshes;
}

QList<iris::MeshPtr> parseFile(const QString &path, const iris::ImportTransform &xf)
{
    Assimp::Importer importer;
    return fromScene(iris::readSceneFile(importer, path, iris::ImportFlags::Canonical, xf));
}

}   // namespace testmesh
