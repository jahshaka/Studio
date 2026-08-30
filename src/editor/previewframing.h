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

#include <QMatrix4x4>
#include <QVector3D>
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
            const QVector3D mn = local.getMin(), mx = local.getMax();
            const QMatrix4x4 xf = meshNode->getGlobalTransform();
            for (int i = 0; i < 8; ++i) {
                aabb.merge(xf.map(QVector3D(i & 1 ? mx.x() : mn.x(),
                                            i & 2 ? mx.y() : mn.y(),
                                            i & 4 ? mx.z() : mn.z())));
            }
        }
    }
    for (const auto &child : node->children) aabb.merge(worldBoundingBox(child));
    return aabb;
}

/// The legacy framing rule: back the camera off until 1.2 radii fill the
/// vertical FOV. AssetViewer::addNodeToScene / ThumbnailGenerator.
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

} // namespace preview

#endif // PREVIEWFRAMING_H
