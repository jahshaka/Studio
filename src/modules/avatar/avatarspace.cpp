/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "irisgl/core/math/vec.h"
#include "modules/avatar/avatarspace.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/assets/mesh.h"

#include <QColor>

namespace avatar {
namespace space {

namespace {

// The room, in metres. The floor is the owner-specified 10x10 tile grid with
// a 1m tile; the walls reuse the same tile so the grids line up at the skirt.
constexpr float kTile       = 1.0f;   // tile pitch
constexpr int   kFloorTiles = 10;     // 10x10 (owner spec)
constexpr int   kWallRows   = 4;      // wall height in tiles
constexpr float kFaceFloor  = 0.92f;  // floor tile face fraction -> 8cm grey lines
constexpr float kFaceWall   = 0.97f;  // wall tile face fraction -> 3cm glow lines


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

} // namespace

iris::SceneNodePtr buildModernRoom(const iris::ScenePtr &scene)
{
    if (!scene || !scene->rootNode) return iris::SceneNodePtr();

    // EVERY piece of the room is an UNROTATED thin cube (cube.obj, 2x2x2 ->
    // scale 0.5 == 1m). v1 built the walls from rotated planes and the local
    // scale did not survive the 90-degree roll: the east/west glow planes came
    // out 10m TALL and 4m wide, jutting through the floor and past the ceiling
    // (the owner's "white planes at an angle", 2026-09-05). Axis-aligned cubes
    // cannot have that bug, and their bevelled edges catch the light the way
    // flat quads never did.
    auto mesh = iris::Mesh::loadMesh(QStringLiteral(":/content/primitives/cube.obj"));
    if (!mesh) return iris::SceneNodePtr();   // headless tests have no qrc models

    // Materials, shared by role.
    auto floorTile = iris::PbrMaterial::create();     // BLACK, glossy (owner flip)
    floorTile->setBaseColor(QColor(6, 6, 9));
    floorTile->setMetallicFactor(0.1f);
    floorTile->setRoughnessFactor(0.08f);             // the reflection carries the look

    auto floorLines = iris::PbrMaterial::create();    // GREY lines (owner flip)
    floorLines->setBaseColor(QColor(126, 130, 140));
    floorLines->setMetallicFactor(0.0f);
    floorLines->setRoughnessFactor(0.9f);
    floorLines->setEmissiveColor(QColor(126, 130, 140));
    floorLines->setEmissiveIntensity(0.22f);          // legible from every angle

    auto wallTile = iris::PbrMaterial::create();      // truly WHITE wall tiles
    wallTile->setBaseColor(QColor(240, 242, 246));
    wallTile->setMetallicFactor(0.0f);
    wallTile->setRoughnessFactor(0.6f);
    wallTile->setEmissiveColor(QColor(255, 255, 255));
    wallTile->setEmissiveIntensity(0.30f);            // verticals get no key light

    auto wallGlow = iris::PbrMaterial::create();      // the seam light, a clean line
    wallGlow->setBaseColor(QColor(255, 255, 255));
    wallGlow->setEmissiveColor(QColor(228, 236, 255));
    wallGlow->setEmissiveIntensity(1.6f);
    wallGlow->setRoughnessFactor(1.0f);

    auto ceiling = iris::PbrMaterial::create();       // soft light panel
    ceiling->setBaseColor(QColor(246, 248, 251));
    ceiling->setEmissiveColor(QColor(255, 255, 255));
    ceiling->setEmissiveIntensity(0.30f);
    ceiling->setRoughnessFactor(0.8f);

    auto group = iris::SceneNode::create();
    group->setName(QStringLiteral("avatar-space"));
    group->setPickable(false);
    group->isBuiltIn = true;

    const float half   = kFloorTiles * kTile * 0.5f;  // 5m
    const float height = kWallRows * kTile;           // 4m
    const float mid    = height * 0.5f;

    auto slab = [&](const iris::PbrMaterialPtr &mat, const char *name,
                    const iris::Vec3 &pos, const iris::Vec3 &halfExtents) {
        auto node = makePanel(mesh, mat, name);
        node->setLocalPos(pos);
        node->setLocalScale(halfExtents);             // cube is 2x2x2: scale = half-extents
        group->addChild(node);
        return node;
    };

    // Floor, inverted construction (owner, 2026-09-05): ONE continuous black
    // gloss plate — the PLANAR REFLECTOR, so the character and the glowing
    // walls mirror in it — with the grey grid LINES laid on top as thin
    // strips. One reflector total (each active plane is a whole extra scene
    // render; a hundred tile reflectors would be absurd), and the Tron floor
    // is a continuous mirror anyway.
    {
        auto plate = slab(floorTile, "avatar-floor", iris::Vec3(0, -0.02f, 0),
                          iris::Vec3(half, 0.02f, half));
        plate->setPlanarReflector(true);
        const float lw = kTile * (1.0f - kFaceFloor) * 0.5f;   // line half-width
        for (int i = 0; i <= kFloorTiles; ++i) {
            const float o = (i - kFloorTiles * 0.5f) * kTile;
            slab(floorLines, "avatar-floor-line", iris::Vec3(o, 0.0015f, 0),
                 iris::Vec3(lw, 0.001f, half));                 // north-south
            slab(floorLines, "avatar-floor-line", iris::Vec3(0, 0.0015f, o),
                 iris::Vec3(half, 0.001f, lw));                 // east-west
        }
    }

    // Walls: a thin glowing slab just OUTSIDE each wall line, then 10x4 white
    // tiles standing 3cm proud of it; the glow shows only in the 3cm gaps.
    struct Wall { iris::Vec3 glowPos, glowHalf, axisRight, axisUp; iris::Vec3 tileNormalOffset; iris::Vec3 tileHalf; };
    const float tw = kTile * kFaceWall * 0.5f;        // tile half-size on the wall
    const float t  = 0.015f;                          // tile thickness (half)
    const Wall walls[4] = {
        { {0, mid, -half - 0.01f}, {half, mid, 0.01f}, {1,0,0}, {0,1,0}, {0, 0,  t}, {tw, tw, t} },   // north
        { {0, mid,  half + 0.01f}, {half, mid, 0.01f}, {1,0,0}, {0,1,0}, {0, 0, -t}, {tw, tw, t} },   // south
        { { half + 0.01f, mid, 0}, {0.01f, mid, half}, {0,0,1}, {0,1,0}, {-t, 0, 0}, {t, tw, tw} },   // east
        { {-half - 0.01f, mid, 0}, {0.01f, mid, half}, {0,0,1}, {0,1,0}, { t, 0, 0}, {t, tw, tw} },   // west
    };
    for (const Wall &w : walls) {
        slab(wallGlow, "avatar-wall-glow", w.glowPos, w.glowHalf);
        for (int r = 0; r < kWallRows; ++r)
            for (int c = 0; c < kFloorTiles; ++c) {
                const float u = (c - (kFloorTiles - 1) * 0.5f) * kTile;
                const float v = (r - (kWallRows - 1) * 0.5f) * kTile;
                slab(wallTile, "avatar-wall-tile",
                     w.glowPos + w.axisRight * u + w.axisUp * v + w.tileNormalOffset,
                     w.tileHalf);
            }
    }

    // Ceiling: one soft light slab.
    slab(ceiling, "avatar-ceiling", iris::Vec3(0, height + 0.01f, 0),
         iris::Vec3(half, 0.01f, half));

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
