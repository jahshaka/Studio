/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/scenestats.h"

#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace scenestats {

namespace {

/// One node's own authored triangles, 0 for everything that is not a triangle
/// mesh. `Mesh::numFaces` is the face count of the AUTHORED index buffer
/// (level 0 of the ATOM chain) — see the header for why the drawn level is
/// deliberately not what this counts.
quint64 trianglesOf(const iris::SceneNodePtr &node)
{
    if (!node || node->getSceneNodeType() != iris::SceneNodeType::Mesh) return 0;
    const auto meshNode = node.staticCast<iris::MeshNode>();
    if (!meshNode) return 0;
    const iris::MeshPtr mesh = meshNode->getMesh();
    if (!mesh) return 0;
    // A LINE MESH IS NOT TRIANGLES, and this is the one place the two numbers
    // have to agree: Ogre's face metric counts a line primitive as zero faces,
    // so counting one here would put a triangle in `sceneTriangles` that could
    // never appear in `submittedTriangles`.
    if (mesh->primitiveMode != iris::PrimitiveMode::Triangles) return 0;
    return mesh->numFaces > 0 ? quint64(mesh->numFaces) : 0;
}

/// The walk. `visible` is the EFFECTIVE visibility of the parent chain, carried
/// down rather than re-derived per node: isVisibleInScene() is O(depth) and
/// this would make the walk O(n·depth) for an answer it already knows.
void walk(const iris::SceneNodePtr &node, bool visible, SceneGeometry &out)
{
    if (!node) return;
    const bool shown = visible && node->isVisible();
    if (const quint64 tris = trianglesOf(node)) {
        if (shown) { out.triangles += tris; ++out.meshNodes; }
        else       { out.hiddenTriangles += tris; ++out.hiddenMeshNodes; }
    }
    // The subtree is walked either way: a hidden parent's children are part of
    // the scene, they are simply part of the hidden half of it.
    const QList<iris::SceneNodePtr> kids = node->children();
    for (const iris::SceneNodePtr &child : kids) walk(child, shown, out);
}

}   // namespace

SceneGeometry geometryUnder(const iris::SceneNodePtr &root)
{
    SceneGeometry out;
    // The ROOT's own flag counts like any other: hiding the root hides the
    // world, and the readout must say so.
    walk(root, true, out);
    return out;
}

SceneGeometry sceneGeometry(const iris::ScenePtr &scene)
{
    if (!scene) return SceneGeometry{};
    return geometryUnder(scene->getRootNode());
}

}   // namespace scenestats
