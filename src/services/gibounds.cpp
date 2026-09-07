#include "services/gibounds.h"

#include <algorithm>

#include "irisgl/core/geometry/boundingsphere.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace gibounds {

namespace {
void swallow(const iris::SceneNodePtr &node, bool &any, iris::Vec3 &mn, iris::Vec3 &mx)
{
    if (!node) return;
    iris::Vec3 c = node->getGlobalPosition();
    float r = 0.0f;
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto mesh = node.staticCast<iris::MeshNode>();
        if (mesh->getMesh()) {
            const iris::BoundingSphere b = mesh->getTransformedBoundingSphere();
            c = b.pos;
            r = b.radius;
        }
    }
    mn.setX(std::min(mn.x(), c.x() - r)); mx.setX(std::max(mx.x(), c.x() + r));
    mn.setY(std::min(mn.y(), c.y() - r)); mx.setY(std::max(mx.y(), c.y() + r));
    mn.setZ(std::min(mn.z(), c.z() - r)); mx.setZ(std::max(mx.z(), c.z() + r));
    any = true;
    for (const auto &child : node->children()) swallow(child, any, mn, mx);
}
}  // namespace

bool fit(const QList<iris::SceneNodePtr> &nodes, float margin,
         iris::Vec3 &outMin, iris::Vec3 &outMax)
{
    bool any = false;
    iris::Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
    for (const auto &n : nodes) swallow(n, any, mn, mx);
    if (!any) return false;
    if (margin != 0.0f) {
        const iris::Vec3 m(margin, margin, margin);
        mn = mn - m;
        mx = mx + m;
    }
    // A pinned volume is only a volume if it HAS one: a single point-like node
    // would otherwise write min == max, which is the document's spelling of
    // "automatic" and would silently do the opposite of what the caller asked.
    const float eps = 1e-3f;
    if (mx.x() - mn.x() < eps) { mn.setX(mn.x() - eps); mx.setX(mx.x() + eps); }
    if (mx.y() - mn.y() < eps) { mn.setY(mn.y() - eps); mx.setY(mx.y() + eps); }
    if (mx.z() - mn.z() < eps) { mn.setZ(mn.z() - eps); mx.setZ(mx.z() + eps); }
    outMin = mn;
    outMax = mx;
    return true;
}

}  // namespace gibounds
