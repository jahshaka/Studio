/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DEFAULTFLOOR_H
#define DEFAULTFLOOR_H

// THE DEFAULT FLOOR (owner, 2026-09-12): "the floor is a unique asset; it
// should have a unique material only it has; since users can add a material to
// it, it needs a reset that clears the user-added material."
//
// Every new scene stands on it (MainWindow::createDefaultScene) and every
// shipped demo stands on one. It is ONE factory, here, used by the scene
// builder AND by the material reset (services/materialdefaults.h — the floor is
// the reset's first default provider), so the two can never drift:
//
//   * the mesh: the 100 m ground plane, a hair above y = 0 (the default plane
//     reset z-fights at exactly 0), named "Ground", not pickable in the
//     viewport (select it in the outliner), no face culling, casting no shadow,
//     a static physics plane;
//   * its OWN material — never a shared library row, never a library tile: the
//     checker (the shipped tile.png, pinned into the project through the one
//     import pipeline and content-identified, so every project reuses the same
//     row), textureScale 4, roughness 1, metallic 0;
//   * the document flag `defaultFloor` (irisgl MeshNode) — what the reset and
//     the demos look for, never the name or the mesh path.

#include <QString>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/meshnode.h"

class Database;
class Project;

namespace defaultfloor {

/// The floor's authored values — the only place they are written.
constexpr float kTextureScale = 4.0f;
constexpr float kRoughness = 1.0f;
constexpr float kMetallic = 0.0f;
QString meshPath();          ///< ":/models/ground.obj"
QString shippedTilePath();   ///< the shipped checker the tile row is minted from

/// A FRESH instance of the floor's own default material. In a real project
/// (`project` with a guid) the checker is pinned into it and `tileGuid`, when
/// given, names the pinned row, and `tileNewlyPinned` says whether THIS call
/// pinned it (the project had no pin on the tile before); the startup
/// placeholder (no guid) renders the shipped file and writes nothing.
iris::PbrMaterialPtr createMaterial(Database *db, Project *project, QString *tileGuid = nullptr,
                                    bool *tileNewlyPinned = nullptr);

/// The default floor NODE, with its material. In a real project it also records
/// the node's own Object row (the built-in marker: a node is not a tray tile)
/// and the node -> tile use edge.
iris::MeshNodePtr createNode(Database *db, Project *project);

/// Whether `node` is a scene's default floor (the document flag). Inline so a
/// panel can ask without linking the factory.
inline bool isDefaultFloor(const iris::SceneNodePtr &node)
{
    return node && node->getSceneNodeType() == iris::SceneNodeType::Mesh
           && node.staticCast<iris::MeshNode>()->defaultFloor;
}

}   // namespace defaultfloor

#endif   // DEFAULTFLOOR_H
