/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SURFACEPLACEMENT_H
#define SURFACEPLACEMENT_H

// A DROPPED OBJECT RESTS ON THE SURFACE IT WAS DROPPED ON (owner report,
// 2026-09-14: "when we drag and drop an asset or primitive into the scene it
// should be placed ON TOP of the floor; it is placed INSIDE the floor").
//
// The viewport's drop carries the point where the cursor's ray met the scene
// (EngineSceneViewport::dropPositionAt) and the node was born with its PIVOT
// there. A pivot is not a base: every built-in primitive is modelled around its
// own centre, and so is most of what an artist exports, so half of every
// dropped object was below the floor. (The drop point itself is right — it is
// what smoke S2 added so that primitives land under the cursor instead of in
// front of the camera. What was missing is this.)
//
// THE RULE, in one line: the bottom of the node's own bounding box goes on the
// hit point, so an object with a CENTRE pivot is lifted by half its height, an
// object already modelled on its base is lifted by nothing, and neither is
// lifted twice. `lift` is a pure function of the node and the point — no
// document, no engine — which is what lets a test state the rule directly.
//
// ALONG THE PARENT'S +Y, which for a drop is world up: a drop lands at the
// scene root, whose transform is the identity, so the node's own local frame
// and the world agree — and `setLocalPos` writes in that frame, which is why
// everything here is measured in it. (A caller that places a node under a
// ROTATED parent would be lifting it along that parent's Y, not the world's;
// no caller does, and the day one does this has to take the parent's global
// transform into account.)
//
// NOT the surface normal, either: the drop resolves a point and not a plane
// (dropPositionAt intersects the picked geometry or the y=0 ground), and the
// surfaces a user drops onto — the floor, a table top, the top of a crate —
// are horizontal. A sloped surface leaves the object standing upright with its
// lowest corner on the slope, which is the same thing every editor does until
// it grows surface alignment.

#include <algorithm>
#include <functional>
#include <limits>

#include "irisgl/core/geometry/aabb.h"
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace surfaceplacement
{

/// How a caller's position is meant.
enum class Placement
{
    Pivot,      ///< the node's ORIGIN goes exactly there — what a script's
                ///< coordinates mean, and what every add did before this.
    OnSurface   ///< the node RESTS there: the bottom of its bounding box
                ///< touches the point. What a DROP means.
};

/// The node's axis-aligned bounds in its PARENT's space, or `valid == false`
/// when there is no geometry to measure (an empty, a light, a model whose mesh
/// has not arrived yet — all of which are placed at the pivot, unchanged).
struct Bounds
{
    iris::Vec3 min;
    iris::Vec3 max;
    bool valid = false;
};

inline Bounds parentSpaceBounds(const iris::SceneNodePtr &node)
{
    Bounds out;
    if (!node) return out;

    float mn[3] = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max() };
    float mx[3] = { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max() };
    bool any = false;

    std::function<void(iris::SceneNode *, const iris::Mat4 &)> walk =
        [&](iris::SceneNode *n, const iris::Mat4 &parent) {
            if (!n) return;
            const iris::Mat4 here = parent * n->getLocalTransform();
            if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
                if (auto mesh = static_cast<iris::MeshNode *>(n)->getMesh()) {
                    const iris::AABB local = mesh->getAABB();
                    const iris::Vec3 lo = local.getMin();
                    const iris::Vec3 hi = local.getMax();
                    for (int c = 0; c < 8; ++c) {
                        const iris::Vec3 w = here * iris::Vec3((c & 1) ? hi.x() : lo.x(),
                                                               (c & 2) ? hi.y() : lo.y(),
                                                               (c & 4) ? hi.z() : lo.z());
                        const float p[3] = { w.x(), w.y(), w.z() };
                        for (int a = 0; a < 3; ++a) {
                            mn[a] = std::min(mn[a], p[a]);
                            mx[a] = std::max(mx[a], p[a]);
                        }
                        any = true;
                    }
                }
            }
            const int kids = n->childCount();
            for (int i = 0; i < kids; ++i) walk(n->childAt(i), here);
        };
    // The node's OWN local transform is the first step of the walk, so the
    // bounds come out in the frame its position is written in.
    walk(node.data(), iris::Mat4());

    if (!any) return out;
    out.min = iris::Vec3(mn[0], mn[1], mn[2]);
    out.max = iris::Vec3(mx[0], mx[1], mx[2]);
    out.valid = true;
    return out;
}

/// How far `node` — sitting where it sits — must rise for the bottom of its
/// bounding box to rest on `surfaceY`. Zero when it cannot be measured, so a
/// node with no geometry is never moved by this.
inline float lift(const iris::SceneNodePtr &node, float surfaceY)
{
    const Bounds bounds = parentSpaceBounds(node);
    if (!bounds.valid) return 0.0f;
    return surfaceY - bounds.min.y();
}

/// Puts `node` at `point` the way `placement` means it. The node must already
/// carry its geometry — a mesh that arrives later has nothing to measure, and
/// the caller places it again when it does.
inline void place(const iris::SceneNodePtr &node, const iris::Vec3 &point, Placement placement)
{
    if (!node) return;
    node->setLocalPos(point);
    if (placement != Placement::OnSurface) return;
    const float rise = lift(node, point.y());
    if (rise != 0.0f) node->setLocalPos(iris::Vec3(point.x(), point.y() + rise, point.z()));
}

}   // namespace surfaceplacement

#endif // SURFACEPLACEMENT_H
