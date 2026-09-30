/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DEFAULTFLOORMATERIAL_H
#define DEFAULTFLOORMATERIAL_H

// THE DEFAULT FLOOR MATERIAL (owner, 2026-09-12): "the floor is a unique asset;
// it should have a unique material only it has; since users can add a material
// to it, it needs a reset that clears the user-added material."
//
// The floors every scene template builds (services/scenetemplate.h,
// MainWindow::createDefaultScene) are ordinary cube nodes; what they share is
// THIS material, made fresh for each by the one factory here, which is also
// the material reset's first default provider (services/materialdefaults.h)
// and the editor's Ground plane widget's material (SceneMirror::
// setGroundPlane) — so a floor, its reset and the plane can never drift:
//
//   * its OWN material — never a shared library row, never a library tile: the
//     checker (the shipped tile.png, pinned into the project through the one
//     import pipeline and content-identified, so every project reuses the same
//     row), textureScale 25 (one 4 m checker per repeat over a 100 m floor
//     face), roughness 1, metallic 0, and NO SPECULAR AT ALL (owner,
//     2026-09-13: "the ground should not be reflective, it should have 0
//     specular") — see kWorkflow/kIor/specularColor() below;
//   * the document flag `defaultFloor` (irisgl MeshNode) says which nodes wear
//     it by default — what the reset and the Player's floor switch look for,
//     never the name or the mesh path.

#include <QColor>
#include <QString>

#include "irisgl/core/irisutils.h"
#include "irisgl/document/materials/pbrmaterial.h"

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/meshnode.h"

class Database;
class Project;

namespace defaultfloormaterial {

/// The floor's authored values — the only place they are written.
/// The checker's density: the cube floor's top face maps ONE UV repeat over its
/// 100 m (the cube primitive's own map), so 25 repeats is one tile image per
/// 4 m. The Ground plane widget maps the same 1/100 UV per metre, so the two
/// register where they meet.
constexpr float kTextureScale = 25.0f;
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
// WHAT THE WORKFLOW COSTS: the floor is now the only object in a default scene
// on the SPECULAR workflow, and the workflow is part of the Hlms shader hash —
// so a cold shader cache compiles one extra PSO family for it (paid once per
// build id, like every other first-frame compile; the disk cache carries it
// after that).
constexpr int   kWorkflow = 1;     ///< PbrMaterial's Specular workflow: the fresnel rows are the read ones
constexpr float kIor = 1.0f;       ///< -> setFresnel(0): no reflectance at any angle
/// kS: black, the master switch on every specular path.
inline QColor specularColor() { return QColor(0, 0, 0); }

/// The shipped checker the tile row is minted from.
inline QString shippedTilePath()
{
    return IrisUtils::getAbsoluteAssetPath("app/content/textures/tile.png");
}

/// THE VALUES, written onto a fresh material with `tilePath` as its base-colour
/// map — the one place they are applied. Header-only so a database-free
/// surface (the Assets module's preview scene, the editor's Ground plane
/// widget) wears exactly the floor's material without linking the pinning
/// half below.
inline iris::PbrMaterialPtr createUnpinned(const QString &tilePath = shippedTilePath())
{
    // A PbrMaterial (HLMS_ADOPTION P4b). The workflow/ior/specular trio is the
    // floor's ZERO SPECULAR, and the notes above state why it takes two values
    // rather than one.
    auto material = iris::PbrMaterial::create();
    // THE TILE, UNTINTED — stated rather than inherited (DRAG-1). baseColor
    // MULTIPLIES the base-colour map; the constructor's default is a physical
    // grey (RENDER_AUDIT I-1), which would dim the checker by 0.58.
    material->setValue("baseColor", QColor(255, 255, 255));
    material->setValue("baseColorMap", tilePath);
    material->setValue("textureScale", kTextureScale);
    material->setValue("roughness", kRoughness);
    material->setValue("metallic", kMetallic);
    material->setValue("workflow", kWorkflow);
    material->setValue("ior", kIor);
    material->setValue("specularColor", specularColor());
    return material;
}

/// A FRESH instance of the floor's own default material. In a real project
/// (`project` with a guid) the checker is pinned into it and `tileGuid`, when
/// given, names the pinned row, and `tileNewlyPinned` says whether THIS call
/// pinned it (the project had no pin on the tile before); the startup
/// placeholder (no guid) renders the shipped file and writes nothing.
iris::PbrMaterialPtr create(Database *db, Project *project, QString *tileGuid = nullptr,
                            bool *tileNewlyPinned = nullptr);

/// Whether `node` wears the default floor material by default (the document
/// flag every template floor carries). Inline so a
/// panel can ask without linking the factory.
inline bool isDefaultFloor(const iris::SceneNodePtr &node)
{
    return node && node->getSceneNodeType() == iris::SceneNodeType::Mesh
           && node.staticCast<iris::MeshNode>()->defaultFloor;
}

}   // namespace defaultfloormaterial

#endif   // DEFAULTFLOORMATERIAL_H
