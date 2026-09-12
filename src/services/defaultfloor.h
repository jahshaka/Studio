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
//     row), textureScale 4, roughness 1, metallic 0, and NO SPECULAR AT ALL
//     (owner, 2026-09-13: "the ground should not be reflective, it should have
//     0 specular") — see kWorkflow/kIor/specularColor() below;
//   * the document flag `defaultFloor` (irisgl MeshNode) — what the reset and
//     the demos look for, never the name or the mesh path.

#include <QColor>
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

// A FLOOR WITH NO SPECULAR (owner, 2026-09-13, testing push #18: "the ground
// should not be reflective, it should have 0 specular"). Two values carry it,
// and BOTH are needed — measured on this pin, grazing camera, the default
// scene, 5x5 probe averages (spikes/gf1-ground/):
//
//   today (metallic workflow, F0 0.04, kS white)   66 65 67 81
//   F0 = 0 alone                                   61 59 59 74
//   kS = 0 alone                                   61 59 59 72
//   both                                           61 59 59 72
//
//   * kIor = 1.0 in the SPECULAR workflow: `setFresnel` is fed
//     ((1 - ior) / (1 + ior))^2 = 0 (OgreMaterials.cpp applyPbr), so the floor's
//     F0 is exactly zero — the index of refraction of air. In the METALLIC
//     workflow F0 is not authorable at all: the shader computes
//     lerp(0.04, albedo, metalness) (Hlms/Pbs/Any/Main/800.PixelShader_piece_ps
//     .any:330), i.e. every dielectric reflects 4% of the sky, always. That 4%
//     IS the sheen the owner saw.
//   * specularColor (kS) BLACK: F0 = 0 is not the whole story. Direct light
//     still carries Schlick's (1 - VdotH)^5 rim (200.BRDFs_piece_ps.any:9) and,
//     once the LTC matrix is loaded, the environment term keeps an additive
//     envBRDF.y bias (:306-308). kS multiplies EVERY specular path in every
//     workflow (OgreHlmsPbsDatablock.h:441-447), so zeroing it is the only
//     complete answer — worth 2/255 at the near probe above.
//
// MAKING A FLOOR REFLECTIVE AGAIN (it must stay possible — a mirror floor is a
// legitimate scene): raise Specular Color back to white in the material panel —
// that one row is the master switch — and give it an IOR (1.5 is the dielectric
// default) or switch Workflow to Metallic, then lower Roughness. Applying any
// other material to the floor (drag a library material onto it, or
// material.apply) is untouched by all of this; `material.reset` brings the
// matte default back.
constexpr int   kWorkflow = 1;     ///< PbrMaterial's Specular workflow: the fresnel rows are the read ones
constexpr float kIor = 1.0f;       ///< -> setFresnel(0): no reflectance at any angle
QColor specularColor();            ///< kS: black, the master switch on every specular path

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
