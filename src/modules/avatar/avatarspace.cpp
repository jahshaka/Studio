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

    // WALL TILES: a MID GREY that is LIT, not a white sheet that emits (owner
    // smoke S10, 2026-09-11: "the Avatar Arena is far too bright, the bottom
    // half of the walls blows out white"). The old tile was base (240,242,246)
    // PLUS emissive white 0.30 "because verticals get no key light". They do
    // get it — unevenly: the room's light is a 7.5 m ceiling AREA panel, whose
    // downward cosine leaves a wall dim under the ceiling and brightest at the
    // skirt. The page renders LDR with no tonemapper (pinWorkspaceGrade), so
    // albedo 0.94 under the skirt's light already sat at 1.0 and the +0.30
    // constant — added to lift the dim top — clamped the whole lower half to
    // 255/255/255 (tests/avatar S12 measures exactly that strip). Emissive on
    // a large surface is the one thing an LDR chain cannot roll off; the
    // key light itself is set in AvatarPreviewModel::buildDocument.
    auto wallTile = iris::PbrMaterial::create();
    wallTile->setBaseColor(QColor(150, 152, 158));
    wallTile->setMetallicFactor(0.0f);
    wallTile->setRoughnessFactor(0.6f);               // lit by the ceiling panel, never self-lit

    auto wallSeam = iris::PbrMaterial::create();      // the seam light, a clean line
    wallSeam->setBaseColor(QColor(255, 255, 255));
    wallSeam->setEmissiveColor(QColor(228, 236, 255));
    // 0.55, not 1.6: the seam is the brightest thing in the room and has to
    // read as a light, but a 1.6 emitter is 60% of the way past white before
    // any lighting is added — it clamped to 255/255/255, and so did its image
    // in the floor's planar reflection. 0.55 plus the light the strip catches
    // lands just under white, so the line is a LIGHT rather than a hole.
    wallSeam->setEmissiveIntensity(0.55f);
    wallSeam->setRoughnessFactor(1.0f);

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
    // The published interior and the wall rows are one room: if the walls ever
    // change height, the number the page reports changes with them or this
    // stops compiling.
    static_assert(kRoomInteriorHeight == kWallRows * kTile,
                  "kRoomInteriorHeight must be the wall height");
    static_assert(kRoomFloorSize == kFloorTiles * kTile,
                  "kRoomFloorSize must match the floor tiling");
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

    // Walls: glowing SEAM STRIPS in the plane just OUTSIDE each wall line, then
    // 10x4 tiles standing 3cm proud of them; the glow shows in the 3cm gaps.
    //
    // The strips replace v2's single emissive slab spanning the WHOLE wall
    // (10 x 4 m of emitter per wall, 97% of it hidden behind tiles). Same
    // picture — every ray through a gap lands inside the gap's own footprint on
    // the strip plane, and the strip is 1 cm wider than that on each side — with
    // 90% less emitting surface. Hidden emitters are not free: a voxel GI
    // solver injects every emissive surface whether or not a camera sees it, so
    // the slab was 160 m² of light waiting for anyone who enabled GI on this
    // document (pinWorkspaceGrade keeps it off). The emitter that remains is
    // exactly the line the design asks to see (owner smoke S10).
    struct Wall {
        iris::Vec3 origin, axisRight, axisUp;         // the seam plane and its frame
        iris::Vec3 seamVertHalf, seamHorizHalf;       // strip half-extents, world axes
        iris::Vec3 tileNormalOffset, tileHalf;
    };
    const float tw = kTile * kFaceWall * 0.5f;        // tile half-size on the wall
    const float t  = 0.015f;                          // tile thickness (half)
    // The visible gap is 3cm (1 - kFaceWall); the strip is a centimetre wider
    // on each side so a grazing view never catches a dark slit at its edge.
    const float sw = kTile * (1.0f - kFaceWall) * 0.5f + 0.01f;
    const Wall walls[4] = {
        { {0, mid, -half - 0.01f}, {1,0,0}, {0,1,0},
          {sw, mid, 0.01f}, {half, sw, 0.01f}, {0, 0,  t}, {tw, tw, t} },   // north
        { {0, mid,  half + 0.01f}, {1,0,0}, {0,1,0},
          {sw, mid, 0.01f}, {half, sw, 0.01f}, {0, 0, -t}, {tw, tw, t} },   // south
        { { half + 0.01f, mid, 0}, {0,0,1}, {0,1,0},
          {0.01f, mid, sw}, {0.01f, sw, half}, {-t, 0, 0}, {t, tw, tw} },   // east
        { {-half - 0.01f, mid, 0}, {0,0,1}, {0,1,0},
          {0.01f, mid, sw}, {0.01f, sw, half}, { t, 0, 0}, {t, tw, tw} },   // west
    };
    for (const Wall &w : walls) {
        // One strip per seam: the 11 tile columns' edges, the 5 row edges
        // (the lowest is the skirt at the floor, the highest the ceiling line).
        for (int c = 0; c <= kFloorTiles; ++c)
            slab(wallSeam, "avatar-wall-seam",
                 w.origin + w.axisRight * ((c - kFloorTiles * 0.5f) * kTile), w.seamVertHalf);
        for (int r = 0; r <= kWallRows; ++r)
            slab(wallSeam, "avatar-wall-seam",
                 w.origin + w.axisUp * ((r - kWallRows * 0.5f) * kTile), w.seamHorizHalf);
        for (int r = 0; r < kWallRows; ++r)
            for (int c = 0; c < kFloorTiles; ++c) {
                const float u = (c - (kFloorTiles - 1) * 0.5f) * kTile;
                const float v = (r - (kWallRows - 1) * 0.5f) * kTile;
                slab(wallTile, "avatar-wall-tile",
                     w.origin + w.axisRight * u + w.axisUp * v + w.tileNormalOffset,
                     w.tileHalf);
            }
    }

    // Ceiling: one soft light slab.
    // Centred 1 cm above the interior height, 2 cm thick: its UNDERSIDE is
    // kRoomInteriorHeight, flush with the top of the walls.
    slab(ceiling, "avatar-ceiling", iris::Vec3(0, kRoomInteriorHeight + 0.01f, 0),
         iris::Vec3(half, 0.01f, half));

    scene->rootNode->addChild(group);
    return group;
}

void pinWorkspaceGrade(const iris::ScenePtr &scene)
{
    if (!scene) return;
    // Custom: no World Mode tier resolves rows through this document, ever.
    scene->worldMode = -1;
    // GI off, and the tier it would come back at is Medium rather than the
    // document default's Epic — a workspace room is never the place to pay for
    // three bounces, and the binding is process-wide (R0.5).
    scene->giMode = iris::GiMode::OFF;
    scene->giTier = 1;
    // No tonemapper, no bloom: the room is authored in the numbers it renders
    // with. (Both are iris::Scene defaults; stated here so a change to those
    // defaults cannot silently re-grade the page.)
    scene->hdrEnabled  = false;
    scene->bloomEnabled = false;
    // If HDR is ever switched on for this surface, the exposure is PINNED
    // (min == max) — a workspace that breathed with auto-exposure would make
    // every avatar screenshot a different picture.
    scene->exposureMin = scene->exposure;
    scene->exposureMax = scene->exposure;
    scene->ssaoEnabled = false;
    scene->smaaPreset  = -1;
    scene->ssrMode     = 0;
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
