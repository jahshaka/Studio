#pragma once
// Measuring what a scene actually OCCUPIES, in world units (SAMPLE_SCALE lane,
// 2026-09-09). One implementation, two callers: the `scene.bounds` verb and the
// samples.scale suite that reads it.
//
// WHY NOT gibounds::fit. That one unions bounding SPHERES on purpose — a pinned
// GI volume that clipped its own subject is the worse failure, so it is
// deliberately generous. It is useless for measuring a ROOM: the Showroom's
// 24 x 0.5 x 24 floor slab has a 17 m bounding sphere, so a sphere union
// reports a 34 m tall scene for a 3.4 m ceiling. This one transforms each
// mesh's LOCAL AABB (iris::Mesh::aabb) by the node's global matrix and unions
// the eight corners, which is the number a human means by "how big is it".
//
// The transformed box is the AABB of the ROTATED box, so a rotated node reads
// slightly larger than its own geometry — the standard, honest approximation;
// nothing here inflates by radius.
#include <QList>

#include "export/exportmanifest.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/irisglfwd.h"

namespace sceneextents {

/// Unions `nodes` and everything under them.
/// Mesh nodes contribute their transformed AABB. Every OTHER node type (lights,
/// empties, particle systems, cameras) contributes nothing unless
/// `includePoints` is true, in which case it contributes its global position as
/// a degenerate point — because "how big is the room" and "how far out does the
/// rig reach" are different questions and the caller has to say which it asked.
/// Returns false when nothing contributed; `outNodes` (optional) receives how
/// many nodes did.
bool worldAabb(const QList<iris::SceneNodePtr> &nodes, bool includePoints,
               iris::Vec3 &outMin, iris::Vec3 &outMax, int *outNodes = nullptr);

/// The archive manifest's scene-scale block for `scene`, measured with
/// worldAabb over everything EXCEPT the built-in ground plane — a 1024 m
/// backdrop that is present in every scene and would drown every measurement
/// (the viewport's selection code identifies it the same way: isBuiltIn plus
/// the ":/models/ground.obj" mesh path). `camera` is the saved editor camera,
/// or null when the caller has none; its vertical fov and eye height are what a
/// consumer needs to say "this scene is authored to the convention".
/// `present` comes back false when the scene has no geometry at all.
exportformat::ManifestScene describe(const iris::ScenePtr &scene,
                                     const iris::CameraNodePtr &camera);

}  // namespace sceneextents
