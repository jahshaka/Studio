#pragma once
// Fitting the global-illumination bounds to objects (REFLECTIONS_ADOPTION_SPEC.md
// P1a.3). ONE implementation, two callers: the `world.fitGiBounds` verb and the
// World panel's Fit button, so the button can never drift from the verb.
//
// The renderer does its OWN automatic fit (OgreGi.cpp computeGiBounds, with
// extent-outlier rejection and the per-node exclude flag) and reports what it
// decided through `world.giStatus()`. This is the other half: PINNING a volume
// the user chose, which switches the automatic fit off for that scene.
//
// The box is the union of each mesh's own AABB pushed through its node's world
// transform, and it SKIPS HIDDEN SUBTREES — the two things that make the Fit
// button agree with what the renderer actually lights (LIGHTING_PIPELINE_AUDIT
// L4.5, SMOKE_FIX S12). It used to union bounding SPHERES, whose radius is half
// the model's diagonal: fitting the default ground — a flat plane with no
// thickness — pinned a 1448 m CUBE, i.e. 1448 m of empty air on the vertical
// axis, at 11 m per voxel. Eight transformed corners are still an over-estimate
// for a rotated box, but a bounded one.
#include <QList>

#include "irisgl/core/math/vec.h"
#include "irisgl/irisglfwd.h"

namespace gibounds {

/// Unions `nodes` and everything under them. False when nothing had any extent
/// (an empty list, or only nodes with no geometry and no position of interest).
/// Children are always included, so fitting an imported model's root node fits
/// the model rather than its origin.
bool fit(const QList<iris::SceneNodePtr> &nodes, float margin,
         iris::Vec3 &outMin, iris::Vec3 &outMax);

}  // namespace gibounds
