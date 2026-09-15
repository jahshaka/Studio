/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// document.mesh_cache — ONE FILE IS PARSED ONCE (ADD-1, 2026-09-15).
//
// THE MEASUREMENT BEHIND IT: every `scene.addPrimitive` ran assimp over the
// primitive's .obj again — 2 ms for a cube, 15 for a sphere, 16 for a teapot,
// on every add, for a compiled-in resource that is byte-identical every time.
// The owner's 64-sphere script spent ~1 s of its wall on nothing but reading
// one file sixty-four times, and because the engine mirror keys its engine
// meshes by the DOCUMENT Mesh pointer, sixty-four parses also meant sixty-four
// identical vertex buffers uploaded to the GPU.
//
// WHY SHARING IS SAFE, which is the part that needs pinning rather than
// measuring. A Mesh is immutable after construction: the importers fill one and
// nothing in the document, the mirror or the editor edits it afterwards. Node
// duplication has ALWAYS shared a MeshPtr (SceneNode::createDuplicate ->
// MeshNode::setMesh(getMesh())), so this cache applies the model the document
// already had to the path that was re-parsing. The one piece of per-node state
// that lives on a mesh — the SKELETON — is cloned per node by
// adoptSkeletonFromMesh (GPU_SKINNING_SPEC §7), and that is asserted here too,
// because it is exactly what a shared mesh would otherwise alias.
//
// WHY THE REFERENCES ARE WEAK: a cache that kept meshes alive would be a leak
// with a nice name — a session that imports a hundred models would hold all
// hundred forever. A file whose last node is gone is parsed again next time,
// and this suite proves that rather than assuming it.

#include <QCoreApplication>
#include <QFile>
#include <cstdio>

#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/scenegraph/meshnode.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("ok:   %s\n", name); } \
    else { std::printf("FAIL: %s\n", name); ++failures; } \
} while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString path = QStringLiteral(":/document-fixtures/commented_header.obj");
    CHECK(QFile::exists(path), "fixture: the model resource is there");

    iris::Mesh::clearLoadCache();
    CHECK(iris::Mesh::loadCacheSize() == 0, "cache: it starts empty");

    // ---- 1. the same path is parsed once --------------------------------
    iris::MeshPtr a = iris::Mesh::loadMesh(path);
    CHECK(!a.isNull(), "load: the fixture parses");
    CHECK(iris::Mesh::loadCacheSize() == 1, "cache: one parse is one entry");
    iris::MeshPtr b = iris::Mesh::loadMesh(path);
    CHECK(a.data() == b.data(), "load: the second load is the SAME mesh, not a second parse");
    CHECK(iris::Mesh::loadCacheSize() == 1, "cache: ...and it did not add an entry");

    // ---- 2. the nodes share it ------------------------------------------
    {
        auto n1 = iris::MeshNode::create();
        auto n2 = iris::MeshNode::create();
        n1->setMesh(path);
        n2->setMesh(path);
        CHECK(n1->getMesh().data() == a.data() && n2->getMesh().data() == a.data(),
              "nodes: two nodes naming one file hold one iris::Mesh");

        // ...AND A NODE'S OWN MESH IS STILL ITS OWN CHOICE. Replacing one
        // node's mesh replaces the POINTER on that node; the sibling is
        // untouched. (This is what "an edited primitive is not aliased" means
        // in this document model: a mesh is never edited in place, it is
        // replaced — so there is nothing to copy on write.)
        n1->setMesh(iris::Mesh::create());
        CHECK(n1->getMesh().data() != a.data() && n2->getMesh().data() == a.data(),
              "nodes: changing one node's mesh leaves the other's alone");
    }

    // ---- 3. the SKELETON is per node, never shared -----------------------
    {
        auto rigged = iris::Mesh::create();
        rigged->setSkeleton(iris::Skeleton::create());
        auto n1 = iris::MeshNode::create();
        auto n2 = iris::MeshNode::create();
        n1->setMesh(rigged);
        n2->setMesh(rigged);
        CHECK(n1->getMesh().data() == n2->getMesh().data(),
              "skeleton: both nodes carry the same mesh");
        CHECK(n1->hasSkeleton() && n2->hasSkeleton(),
              "skeleton: ...and each has one");
        CHECK(n1->getSkeleton().data() != n2->getSkeleton().data(),
              "skeleton: ...but NOT the same one — the pose is per node, so a shared "
              "mesh cannot make two characters fight over one bone array");
    }

    // ---- 4. the cache holds nothing alive --------------------------------
    {
        const iris::Mesh *was = a.data();
        a.clear();
        b.clear();
        CHECK(iris::Mesh::loadCacheSize() == 0,
              "weak: with no node and no caller holding it, the mesh is GONE and the "
              "cache cannot answer for it");
        iris::MeshPtr again = iris::Mesh::loadMesh(path);
        CHECK(!again.isNull(), "weak: ...and the next load parses it again");
        CHECK(iris::Mesh::loadCacheSize() == 1, "weak: ...into one entry");
        // Not an assertion about the allocator (malloc may well hand back the
        // same address): what matters is that the load SUCCEEDED after the
        // cache had let go, which the two lines above already say.
        (void)was;
    }

    // ---- 5. a file that does not exist is not cached ---------------------
    {
        const int before = iris::Mesh::loadCacheSize();
        iris::MeshPtr missing = iris::Mesh::loadMesh(QStringLiteral(":/nope/not-a-model.obj"));
        CHECK(missing.isNull(), "missing: a model that is not there loads to nothing");
        CHECK(iris::Mesh::loadCacheSize() == before,
              "missing: ...and a failure is never cached (a file that appears later loads)");
    }

    std::printf(failures == 0 ? "\ndocument.mesh_cache: PASS\n"
                              : "\ndocument.mesh_cache: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
