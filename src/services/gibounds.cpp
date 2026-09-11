#include "services/gibounds.h"

#include <algorithm>

#include "irisgl/core/geometry/aabb.h"
#include "irisgl/core/geometry/boundingsphere.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace gibounds {

namespace {
void grow(iris::Vec3 &mn, iris::Vec3 &mx, const iris::Vec3 &p)
{
    mn.setX(std::min(mn.x(), p.x())); mx.setX(std::max(mx.x(), p.x()));
    mn.setY(std::min(mn.y(), p.y())); mx.setY(std::max(mx.y(), p.y()));
    mn.setZ(std::min(mn.z(), p.z())); mx.setZ(std::max(mx.z(), p.z()));
}

void swallow(const iris::SceneNodePtr &node, bool &any, iris::Vec3 &mn, iris::Vec3 &mx)
{
    if (!node) return;
    // A HIDDEN SUBTREE IS NOT LIT (SMOKE_FIX S12). The engine drops a hidden
    // item's kGiGeometryBit, so it neither bounces light nor defines the
    // automatic volume; a pinned volume that still stretched to it would put
    // the user's Fit button at odds with what the renderer does.
    if (!node->isVisible()) return;
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto mesh = node.staticCast<iris::MeshNode>();
        if (mesh->getMesh()) {
            // THE MESH'S OWN BOX, TRANSFORMED — not its bounding SPHERE.
            // The sphere's radius is half the model's diagonal, so fitting the
            // default ground (then a 1024 m plane with no thickness, now 100 m)
            // used to pin a 1448 m CUBE: 1448 m of empty air on the vertical
            // axis, which at High is 11 m per voxel. "Deliberately generous" is the wrong
            // trade for the one control a user reaches for when the automatic
            // fit is wrong (LIGHTING_PIPELINE_AUDIT L4.5). Eight corners
            // through the world transform is still an over-estimate for a
            // rotated box, but a bounded one.
            const iris::AABB box = mesh->getMesh()->getAABB();
            const iris::Vec3 bmn = box.getMin(), bmx = box.getMax();
            // An AABB nothing ever merged into is still NEGATIVE INFINITY (its
            // ctor's state), and only the assimp and .jmb paths fill it — a
            // mesh built any other way falls back to the sphere rather than
            // pinning a float-max volume.
            if (bmx.x() >= bmn.x() && bmx.y() >= bmn.y() && bmx.z() >= bmn.z()) {
                const iris::Mat4 xf = node->getGlobalTransform();
                for (int corner = 0; corner < 8; ++corner) {
                    const iris::Vec3 local((corner & 1) ? bmx.x() : bmn.x(),
                                           (corner & 2) ? bmx.y() : bmn.y(),
                                           (corner & 4) ? bmx.z() : bmn.z());
                    grow(mn, mx, xf * local);
                }
            } else {
                const iris::BoundingSphere b = mesh->getTransformedBoundingSphere();
                const iris::Vec3 r(b.radius, b.radius, b.radius);
                grow(mn, mx, b.pos - r);
                grow(mn, mx, b.pos + r);
            }
            any = true;
            for (const auto &child : node->children()) swallow(child, any, mn, mx);
            return;
        }
    }
    grow(mn, mx, node->getGlobalPosition());
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
