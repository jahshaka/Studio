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
// The document model carries a bounding SPHERE per mesh node rather than an
// AABB, so the box is the union of those spheres' boxes — deliberately generous
// rather than deliberately wrong: a pinned volume that clipped its own subject
// would be the worse failure.
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
