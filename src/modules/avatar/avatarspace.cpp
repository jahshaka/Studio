/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "modules/avatar/avatarspace.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/assets/mesh.h"

#include <QColor>
#include <QQuaternion>
#include <QVector3D>

namespace avatar {
namespace space {

namespace {

// The room, in metres. The floor is the owner-specified 10x10 tile grid with
// a 1m tile; the walls reuse the same tile so the grids line up at the skirt.
constexpr float kTile       = 1.0f;   // tile pitch
constexpr int   kFloorTiles = 10;     // 10x10 (owner spec)
constexpr int   kWallRows   = 4;      // wall height in tiles
constexpr float kFace       = 0.92f;  // tile face fraction -> 8cm glowing seam
constexpr float kLift       = 0.012f; // tiles float this far off the base plane

// plane.obj is 2x2 units (-1..+1 in XZ, +Y normal): scale 0.5 == 1 metre.
constexpr float kPlaneHalf = 0.5f;

iris::MeshNodePtr makePanel(const iris::MeshPtr &mesh, const iris::PbrMaterialPtr &mat,
                            const char *name)
{
    auto node = iris::MeshNode::create();
    node->setMesh(mesh);
    node->setMaterial(mat);
    node->setName(QLatin1String(name));
    node->setPickable(false);
    node->isBuiltIn = true;
    node->setShadowCastingEnabled(false);
    return node;
}

// One wall: the glowing base plane (its emissive face IS the seam light — it
// shows through the tile gaps) plus rows x cols of matte white tiles floating
// kLift in front of it. `rot` orients a +Y plane to face INTO the room;
// `center` is the wall's centre; `right`/`up` span the wall in world space.
void buildWall(const iris::SceneNodePtr &group, const iris::MeshPtr &mesh,
               const iris::PbrMaterialPtr &glow, const iris::PbrMaterialPtr &tile,
               const QQuaternion &rot, const QVector3D &center,
               const QVector3D &right, const QVector3D &up,
               const QVector3D &inward)
{
    const float w = kFloorTiles * kTile, h = kWallRows * kTile;

    auto base = makePanel(mesh, glow, "avatar-wall-glow");
    base->setLocalRot(rot);
    base->setLocalPos(center);
    base->setLocalScale(QVector3D(w * kPlaneHalf, 1, h * kPlaneHalf));
    group->addChild(base);

    for (int r = 0; r < kWallRows; ++r) {
        for (int c = 0; c < kFloorTiles; ++c) {
            const float x = (c - (kFloorTiles - 1) * 0.5f) * kTile;
            const float y = (r - (kWallRows - 1) * 0.5f) * kTile;
            auto t = makePanel(mesh, tile, "avatar-wall-tile");
            t->setLocalRot(rot);
            t->setLocalPos(center + right * x + up * y + inward * kLift);
            t->setLocalScale(QVector3D(kTile * kFace * kPlaneHalf, 1,
                                       kTile * kFace * kPlaneHalf));
            group->addChild(t);
        }
    }
}

} // namespace

iris::SceneNodePtr buildModernRoom(const iris::ScenePtr &scene)
{
    if (!scene || !scene->rootNode) return iris::SceneNodePtr();

    // ONE mesh for every panel in the room: sharing the iris::Mesh means the
    // mirror uploads a single engine mesh for ~270 nodes.
    auto mesh = iris::Mesh::loadMesh(QStringLiteral(":/content/primitives/plane.obj"));
    if (!mesh) return iris::SceneNodePtr();   // headless tests have no qrc models

    // Five materials, shared by role — five engine datablocks total.
    auto floorTile = iris::PbrMaterial::create();
    // Mirror-black is a LIE in this scene: there is no IBL here (R0.5 — no GI,
    // no sky), and a metal with nothing to reflect renders as a void. A dark
    // dielectric with a tight gloss reads as the Grid's polished floor while
    // still taking the ambient + the wall glow.
    floorTile->setBaseColor(QColor(36, 37, 44));
    floorTile->setMetallicFactor(0.15f);
    floorTile->setRoughnessFactor(0.18f);
    // The faintest self-light: keeps the tile faces legible against the dead-
    // black seams from every angle, without reading as anything but polish.
    floorTile->setEmissiveColor(QColor(58, 60, 72));
    floorTile->setEmissiveIntensity(0.35f);

    auto floorSeam = iris::PbrMaterial::create();
    floorSeam->setBaseColor(QColor(0, 0, 0));           // dead black, NO sheen
    floorSeam->setMetallicFactor(0.0f);
    floorSeam->setRoughnessFactor(1.0f);

    auto wallTile = iris::PbrMaterial::create();
    wallTile->setBaseColor(QColor(238, 240, 244));
    wallTile->setMetallicFactor(0.0f);
    wallTile->setRoughnessFactor(0.55f);
    // A whisper of emissive lifts the white tiles off the ambient floor —
    // vertical faces get almost nothing from the two directionals.
    wallTile->setEmissiveColor(QColor(255, 255, 255));
    wallTile->setEmissiveIntensity(0.10f);

    auto wallGlow = iris::PbrMaterial::create();        // the Tron seam light
    wallGlow->setBaseColor(QColor(255, 255, 255));
    wallGlow->setEmissiveColor(QColor(223, 232, 255));  // cool white
    wallGlow->setEmissiveIntensity(2.4f);
    wallGlow->setRoughnessFactor(1.0f);

    auto ceiling = iris::PbrMaterial::create();         // soft light panel
    ceiling->setBaseColor(QColor(246, 248, 251));
    ceiling->setEmissiveColor(QColor(255, 255, 255));
    ceiling->setEmissiveIntensity(0.35f);
    ceiling->setRoughnessFactor(0.8f);

    auto group = iris::SceneNode::create();
    group->setName(QStringLiteral("avatar-space"));
    group->setPickable(false);
    group->isBuiltIn = true;

    const float half = kFloorTiles * kTile * 0.5f;      // 5m
    const float height = kWallRows * kTile;             // 4m

    // Floor: the seam plane, then the 10x10 mirror-black tiles above it.
    {
        auto seam = makePanel(mesh, floorSeam, "avatar-floor-seams");
        seam->setLocalScale(QVector3D(half, 1, half));
        group->addChild(seam);

        for (int r = 0; r < kFloorTiles; ++r) {
            for (int c = 0; c < kFloorTiles; ++c) {
                auto t = makePanel(mesh, floorTile, "avatar-floor-tile");
                t->setLocalPos(QVector3D((c - (kFloorTiles - 1) * 0.5f) * kTile, kLift,
                                         (r - (kFloorTiles - 1) * 0.5f) * kTile));
                t->setLocalScale(QVector3D(kTile * kFace * kPlaneHalf, 1,
                                           kTile * kFace * kPlaneHalf));
                group->addChild(t);
            }
        }
    }

    // Walls. fromEulerAngles pitches the +Y plane upright; each faces inward.
    const QVector3D X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
    const float mid = height * 0.5f;
    buildWall(group, mesh, wallGlow, wallTile, QQuaternion::fromEulerAngles(90, 0, 0),
              QVector3D(0, mid, -half), X, Y, Z);                       // north, faces +Z
    buildWall(group, mesh, wallGlow, wallTile, QQuaternion::fromEulerAngles(-90, 0, 0),
              QVector3D(0, mid, half), X, Y, -Z);                       // south, faces -Z
    buildWall(group, mesh, wallGlow, wallTile, QQuaternion::fromEulerAngles(0, 0, 90),
              QVector3D(half, mid, 0), Z, Y, -X);                       // east, faces -X
    buildWall(group, mesh, wallGlow, wallTile, QQuaternion::fromEulerAngles(0, 0, -90),
              QVector3D(-half, mid, 0), Z, Y, X);                       // west, faces +X

    // Ceiling: one soft light panel, face down.
    {
        auto lid = makePanel(mesh, ceiling, "avatar-ceiling");
        lid->setLocalRot(QQuaternion::fromEulerAngles(180, 0, 0));
        lid->setLocalPos(QVector3D(0, height, 0));
        lid->setLocalScale(QVector3D(half, 1, half));
        group->addChild(lid);
    }

    scene->rootNode->addChild(group);
    return group;
}

const char *modeName(SpaceMode mode)
{
    return mode == SpaceMode::Modern ? "modern" : "grid";
}

bool parseMode(const QString &name, SpaceMode *out)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("grid"))   { *out = SpaceMode::Grid;   return true; }
    if (n == QLatin1String("modern")) { *out = SpaceMode::Modern; return true; }
    return false;
}

} // namespace space
} // namespace avatar
