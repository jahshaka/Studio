#include "services/sceneextents.h"

#include <algorithm>

#include "irisgl/core/geometry/aabb.h"
#include "irisgl/core/math/mat4.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace sceneextents {

namespace {
void merge(const iris::Vec3 &p, bool &any, iris::Vec3 &mn, iris::Vec3 &mx)
{
    mn.setX(std::min(mn.x(), p.x())); mx.setX(std::max(mx.x(), p.x()));
    mn.setY(std::min(mn.y(), p.y())); mx.setY(std::max(mx.y(), p.y()));
    mn.setZ(std::min(mn.z(), p.z())); mx.setZ(std::max(mx.z(), p.z()));
    any = true;
}

void swallow(const iris::SceneNodePtr &node, bool includePoints, bool &any,
             iris::Vec3 &mn, iris::Vec3 &mx, int &counted)
{
    if (!node) return;
    bool contributed = false;
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto meshNode = node.staticCast<iris::MeshNode>();
        if (meshNode->getMesh()) {
            const iris::AABB local = meshNode->getMesh()->getAABB();
            const iris::Vec3 lo = local.getMin(), hi = local.getMax();
            const iris::Mat4 m = node->getGlobalTransform();
            // Eight corners through the global matrix: the node's scale,
            // rotation and its whole parent chain all ride in there, which is
            // why this is not "position + local size".
            for (int c = 0; c < 8; ++c) {
                const iris::Vec3 corner((c & 1) ? hi.x() : lo.x(),
                                        (c & 2) ? hi.y() : lo.y(),
                                        (c & 4) ? hi.z() : lo.z());
                merge(m * corner, any, mn, mx);
            }
            contributed = true;
        }
    }
    if (!contributed && includePoints) {
        merge(node->getGlobalPosition(), any, mn, mx);
        contributed = true;
    }
    if (contributed) ++counted;
    for (const auto &child : node->children())
        swallow(child, includePoints, any, mn, mx, counted);
}
}  // namespace

bool worldAabb(const QList<iris::SceneNodePtr> &nodes, bool includePoints,
               iris::Vec3 &outMin, iris::Vec3 &outMax, int *outNodes)
{
    bool any = false;
    int counted = 0;
    iris::Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
    for (const auto &n : nodes) swallow(n, includePoints, any, mn, mx, counted);
    if (outNodes) *outNodes = counted;
    if (!any) return false;
    outMin = mn;
    outMax = mx;
    return true;
}

exportformat::ManifestScene describe(const iris::ScenePtr &scene,
                                    const iris::CameraNodePtr &camera)
{
    exportformat::ManifestScene out;
    if (!scene || !scene->getRootNode()) return out;

    QList<iris::SceneNodePtr> subjects;
    for (const auto &n : scene->getRootNode()->children()) {
        if (n && n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            const auto mn = n.staticCast<iris::MeshNode>();
            // The 100 m backdrop, not the scene's size (1024 m before the
            // SMOKE_FIX S14 re-stage).
            //
            // KEYED ON THE MESH PATH ALONE, deliberately: `isBuiltIn` is set by
            // SceneEditService when the user ADDS a primitive and is never
            // restored by the reader (SceneNode's ctor leaves it false, and
            // scenereader.cpp writes meshPath but not the flag), so on a scene
            // LOADED from a file — which is every sample, and every project a
            // user reopens — `isBuiltIn` is false for the ground plane too.
            // Anything testing both, as the viewport's selection-outline filter
            // does (enginesceneviewport.cpp), silently stops filtering after a
            // reopen. Reported 2026-09-09.
            if (mn->meshPath == QStringLiteral(":/models/ground.obj"))
                continue;
        }
        subjects.append(n);
    }

    iris::Vec3 mn, mx;
    if (!worldAabb(subjects, false, mn, mx)) return out;
    out.present = true;
    out.extentMin[0] = mn.x(); out.extentMin[1] = mn.y(); out.extentMin[2] = mn.z();
    out.extentMax[0] = mx.x(); out.extentMax[1] = mx.y(); out.extentMax[2] = mx.z();
    if (camera) {
        out.cameraFov = camera->angle;
        out.cameraHeight = camera->getGlobalPosition().y();
    }
    return out;
}

}  // namespace sceneextents
