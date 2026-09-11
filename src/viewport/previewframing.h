#ifndef PREVIEWFRAMING_H
#define PREVIEWFRAMING_H

// Framing math shared by the asset previews (EngineAssetScene, the legacy
// AssetViewer) and the thumbnail renderer. Two audited bugs live here when the
// maths is done ad hoc (ASSETS_AUDIT.md findings 3 and 4):
//
//  - the preview cameras kept iris's default farClip of 500 while framing a
//    subject at ~2.9 * radius, so any model with a radius over ~170 units
//    (e.g. a cm-scaled Sketchfab glb) sat entirely beyond its own far plane
//    and rendered NOTHING;
//  - bounding boxes were taken from the raw unscaled mesh AABB offset by node
//    position, ignoring node scale/rotation, so a model with a 0.0143 root
//    scale was framed at its unscaled radius — sub-pixel AND far-clipped.
//
// Everything here is world-space and adapts the clip planes to the framing.

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/vec.h"
#include <QtMath>

#include "irisgl/core/geometry/aabb.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"

namespace preview {

/// World-space AABB of every mesh under `node`: each mesh-local AABB's eight
/// corners through the node's global transform (position, rotation AND scale).
/// getGlobalTransform() recomputes up the parent chain, so this is never stale.
inline iris::AABB worldBoundingBox(const iris::SceneNodePtr &node)
{
    iris::AABB aabb;
    if (node->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto meshNode = node.staticCast<iris::MeshNode>();
        if (meshNode->getMesh()) {
            const iris::AABB local = meshNode->getMesh()->getAABB();
            const iris::Vec3 mn = local.getMin(), mx = local.getMax();
            const iris::Mat4 xf = meshNode->getGlobalTransform();
            for (int i = 0; i < 8; ++i) {
                aabb.merge(xf.map(iris::Vec3(i & 1 ? mx.x() : mn.x(),
                                            i & 2 ? mx.y() : mn.y(),
                                            i & 4 ? mx.z() : mn.z())));
            }
        }
    }
    for (const auto &child : node->children()) aabb.merge(worldBoundingBox(child));
    return aabb;
}

/// The legacy framing rule: back the camera off until 1.2 radii fill the
/// vertical FOV. AssetViewer::addNodeToScene / ThumbnailGenerator.
///
/// `fovDegrees` IS THE RENDERED VERTICAL ANGLE, never `CameraNode::angle`.
/// A free camera on a window wider than its framing aspect is drawn at a
/// NARROWED vertical angle (viewport/freecamerapolicy.h), so the authored
/// number is not the frustum on screen and framing against it puts the subject
/// outside the picture — F focused too tight on every window wider than 16:9.
/// `CameraNode::effectiveFovDegrees()` is the one function that answers this,
/// and it is what the gizmo's screen-constant scale already reads.
inline float framingDistance(float radius, float fovDegrees)
{
    return (radius * 1.2f) / qTan(qDegreesToRadians(fovDegrees / 2.0f));
}

/// Clip planes that always contain a subject framed at `dist` with radius
/// `radius`: the far plane covers the whole subject with headroom for orbiting
/// and zooming out, never below iris's default 500 (small scenes keep their
/// exact legacy planes); the near plane scales with the far one to preserve
/// depth precision but never rises above what a close orbit needs.
inline void clipPlanesForFraming(float dist, float radius, float &nearClip, float &farClip)
{
    farClip = qMax(500.0f, (dist + 2.0f * radius) * 1.5f);
    nearClip = qBound(0.1f, farClip / 50000.0f, 100.0f);
}

/// THE FRAMING OF A SUBJECT — the editor's F, as data.
///
/// Every surface that "frames a model" asks the same three questions (what is
/// the middle of it, how big is it, how far back does the camera go) and three
/// call sites answered them three ways: the editor's F (world AABB -> centre +
/// half-diagonal), the Assets preview (the same box through
/// getMinimalEnclosingSphere, which IS centre + half-diagonal) and the
/// thumbnail renderer (a MERGE of per-mesh bounding spheres seeded with a unit
/// sphere at the ORIGIN, then a camera parked at x = 0 whatever the subject's
/// x — which is why a 50 m city thumbnailed tiny and off-centre, smoke S6).
/// One function, so a thumbnail, a preview and F all show the same picture.
struct Framing
{
    iris::Vec3 target;      ///< what the camera looks at (the box's centre)
    float radius = 1.0f;    ///< half the box's diagonal
    float distance = 1.0f;  ///< how far back the camera sits along its view axis
    float nearClip = 0.1f;
    float farClip = 500.0f;
    bool hasGeometry = false;   ///< false = nothing under the node has a mesh
};

inline Framing frameBounds(const iris::AABB &bounds, const iris::Vec3 &fallbackTarget,
                           float fovDegrees)
{
    Framing f;
    f.target = fallbackTarget;
    if (bounds.getMin().x() <= bounds.getMax().x()) {   // non-empty (meshes exist)
        f.target = bounds.getCenter();
        f.radius = qMax(0.05f, bounds.getSize().length() * 0.5f);
        f.hasGeometry = true;
    }
    f.distance = qMax(1.0f, framingDistance(f.radius, fovDegrees));
    clipPlanesForFraming(f.distance, f.radius, f.nearClip, f.farClip);
    return f;
}

/// The framing of a node's whole subtree, measured where the node stands.
inline Framing frameSubject(const iris::SceneNodePtr &node, float fovDegrees)
{
    if (!node) return Framing();
    return frameBounds(worldBoundingBox(node), node->getGlobalPosition(), fovDegrees);
}

} // namespace preview

#endif // PREVIEWFRAMING_H
