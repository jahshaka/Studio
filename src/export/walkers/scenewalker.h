/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EXPORT_SCENEWALKER_H
#define EXPORT_SCENEWALKER_H

// Shared export walkers (ASSET_PIPELINE_SPEC §3.3, phase-5 front half).
//
// The document-walking core the web exporter proved, extracted so every
// exporter (glTF today; .jaf, USD later) drives ONE traversal with ONE set of
// document quirks encoded once:
//
//  - classifyNode: CameraNode NEVER sets SceneNodeType::Camera (nothing in
//    irisgl assigns it — the enum reads Empty on cameras forever), so cameras
//    are classified by dynamic_cast, never by the enum.
//  - shouldSkipForExport: SceneNode::exportable is the legacy "include in
//    model-file export" flag; Light/Camera/Viewer constructors hard-code it
//    false, so honoring it for every type would delete all lights and cameras
//    from every export. It keeps its historical meaning — an opt-out for MESH
//    nodes only.
//  - walkScene: post-order DFS below the root (the root itself is never
//    visited — it is the document's invisible container). The visitor returns
//    an integer handle for each node (a glTF node index, say); the handles of
//    a node's non-skipped children, in child order, are passed to its own
//    visit. Handles < 0 are dropped from child lists.
//
// Pure document consumers: no Ogre, no GL, no Qt widgets, no DB.

#include <QStringList>
#include <QVector>

#include <functional>

#include "irisgl/irisglfwd.h"

namespace iris {
class Material;
}

namespace exportwalk {

enum class NodeKind {
    Empty,
    Mesh,
    Light,
    Camera,
    ParticleSystem,
    Viewer
};

/// The export-side node classification (encodes the CameraNode enum quirk).
NodeKind classifyNode(const iris::SceneNodePtr &node);

/// True for nodes an export must not include (mesh nodes with the legacy
/// exportable flag cleared — and nothing else, see the header comment).
bool shouldSkipForExport(const iris::SceneNodePtr &node);

/// Visitor: (node, handles of already-visited children) -> handle for `node`.
using NodeVisitor =
    std::function<int(const iris::SceneNodePtr &node, const QVector<int> &childHandles)>;

/// Post-order DFS over every non-skipped node below scene->rootNode.
/// Returns the handles of the scene's root-level nodes (in child order,
/// negatives dropped). A null scene/root returns an empty list.
QVector<int> walkScene(const iris::ScenePtr &scene, const NodeVisitor &visit);

/// What a scene references — the inventory manifests and tests assert on.
struct SceneInventory
{
    int totalNodes = 0;          // every visited (non-skipped) node
    int meshNodes = 0;
    int lights = 0;
    int cameras = 0;
    int particleSystems = 0;
    int viewers = 0;
    int empties = 0;
    QVector<iris::Material *> materials;   // unique, discovery order
    QStringList textureSources;            // unique texture source paths/resources,
                                           // discovery order (materials, particles,
                                           // sky when file-backed)
};

/// Walks the scene and gathers the inventory: node counts by kind, the unique
/// materials, and every texture source they (and particle systems and a
/// file-backed sky) reference.
SceneInventory collectInventory(const iris::ScenePtr &scene);

} // namespace exportwalk

#endif // EXPORT_SCENEWALKER_H
