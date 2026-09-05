#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/vec.h"
#include "viewport/scenepicker.h"

#include <algorithm>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/decalnode.h"
#include "irisgl/document/scenegraph/viewernode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/core/geometry/trimesh.h"
#include "irisgl/core/geometry/boundingsphere.h"
#include "irisgl/core/math/intersectionhelper.h"

namespace {
iris::Vec3 unproject(const iris::CameraNodePtr &cam, int w, int h, const QPointF &pos, float depth)
{
    const float mousex = (2.0f * float(pos.x())) / float(w) - 1.0f;
    const float mousey = (2.0f * float(pos.y())) / float(h) - 1.0f;
    const iris::Vec4 hcc(mousex, -mousey, depth, 1.0f);
    const iris::Vec4 eye = cam->projMatrix.inverted() * hcc;
    const iris::Vec4 world = cam->viewMatrix.inverted() * eye;
    return world.toVector3D() / world.w();
}
}

void ScenePicker::screenSegment(iris::CameraNodePtr camera, int w, int h, const QPointF &point,
                                iris::Vec3 &segStart, iris::Vec3 &segEnd)
{
    if (!camera || w <= 0 || h <= 0) { segStart = segEnd = iris::Vec3(); return; }
    camera->setAspectRatio(float(w) / float(h));
    camera->update(0.0f);
    camera->updateCameraMatrices();
    segStart = unproject(camera, w, h, point, -1.0f);
    segEnd   = unproject(camera, w, h, point,  1.0f);
}

void ScenePicker::pickMeshes(const iris::SceneNodePtr &node, const iris::Vec3 &segStart, const iris::Vec3 &segEnd,
                             const iris::Vec3 &cameraPos, bool forcePickable, QList<ScenePick> &out)
{
    if (!node) return;
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh && (node->isPickable() || forcePickable)) {
        auto meshNode = node.staticCast<iris::MeshNode>();
        auto mesh = meshNode->getMesh();
        if (mesh && mesh->getTriMesh()) {
            // Segment into the mesh's local space, hits back to world space.
            const iris::Mat4 inv = meshNode->getGlobalTransform().inverted();
            const iris::Vec3 a = inv * segStart, b = inv * segEnd;
            // BROAD PHASE first: the mesh's own bounding sphere, in the same
            // local space. Without it every ray scanned every triangle of every
            // mesh in the scene — which V-hold vertex snapping does on every
            // mouse move (deep audit 2026-09, area 2: "no broad phase in
            // picking"). iris::Scene::rayCast has always done this; the picker
            // is the copy that did not.
            const iris::BoundingSphere sphere = mesh->getBoundingSphere();
            float t; iris::Vec3 sphereHit;
            if (iris::IntersectionHelper::raySphereIntersects(a, (b - a).normalized(),
                                                              sphere.pos, sphere.radius, t, sphereHit)) {
                QList<iris::TriangleIntersectionResult> results;
                if (mesh->getTriMesh()->getSegmentIntersections(a, b, results)) {
                    for (const auto &r : results) {
                        ScenePick p;
                        p.node = node;
                        p.hitPoint = meshNode->getGlobalTransform() * r.hitPoint;
                        p.distanceFromCameraSqrd = (p.hitPoint - cameraPos).lengthSquared();
                        p.triangleIndex = r.triangleIndex;
                        out.append(p);
                    }
                }
            }
        }
    }
    for (const auto &child : node->children())
        pickMeshes(child, segStart, segEnd, cameraPos, forcePickable, out);
}

QList<ScenePick> ScenePicker::pickAll(iris::ScenePtr scene, const iris::Vec3 &segStart, const iris::Vec3 &segEnd,
                                      const iris::Vec3 &cameraPos, bool forcePickable,
                                      bool includeLights, bool includeViewers,
                                      bool includeDecals, bool refreshTransforms,
                                      bool includeCameras)
{
    QList<ScenePick> hits;
    if (!scene || !scene->getRootNode()) return hits;
    if (refreshTransforms) scene->getRootNode()->update(0.0f);   // fresh global transforms
    pickMeshes(scene->getRootNode(), segStart, segEnd, cameraPos, forcePickable, hits);

    const float sphereRadius = 0.5f;
    iris::Vec3 rayDir = (segEnd - segStart).normalized();
    iris::Vec3 hitPoint; float t;
    if (includeLights) {
        for (auto &light : scene->lights) {
            if (light->isPickable() &&
                iris::IntersectionHelper::raySphereIntersects(segStart, rayDir, light->getGlobalPosition(),
                                                              sphereRadius, t, hitPoint)) {
                ScenePick p; p.node = light.staticCast<iris::SceneNode>(); p.hitPoint = hitPoint;
                p.distanceFromCameraSqrd = (hitPoint - cameraPos).lengthSquared();
                hits.append(p);
            }
        }
    }
    if (includeDecals) {
        for (auto &decal : scene->decals) {
            if (decal->isPickable() &&
                iris::IntersectionHelper::raySphereIntersects(segStart, rayDir, decal->getGlobalPosition(),
                                                              sphereRadius, t, hitPoint)) {
                ScenePick p; p.node = decal.staticCast<iris::SceneNode>(); p.hitPoint = hitPoint;
                p.distanceFromCameraSqrd = (hitPoint - cameraPos).lengthSquared();
                hits.append(p);
            }
        }
    }
    if (includeCameras) {
        // CAMERAS_SPEC phase 2b: the body is drawn engine-side, so this is the
        // hit test for it — the same origin sphere lights and decals use, plus
        // one rule they do not need.
        //
        // TWO RULES a light or a decal does not need, because nobody parks one
        // of those on the viewer's eye:
        //
        //  1. YOU CANNOT PICK THE CAMERA YOU ARE LOOKING THROUGH. `cameraPos`
        //     is the eye the segment was cast from, so a scene camera sitting
        //     there IS that eye — piloting it (CAMERAS_SPEC phase 3), or a
        //     preview rendered through it. Without this, every single click
        //     while piloting would select the piloted camera instead of the
        //     scene in front of it, and an orthographic view would pick its own
        //     camera on every centre click (its ray starts a hundred units
        //     BEHIND the eye, so an in-front test cannot catch that one).
        //  2. Nothing behind the ray's start is a hit. raySphereIntersects does
        //     not require the sphere to be ahead of the origin.
        for (auto &camera : scene->cameras) {
            if (!camera || !camera->isPickable()) continue;
            const iris::Vec3 centre = camera->getGlobalPosition();
            if ((centre - cameraPos).lengthSquared() < sphereRadius * sphereRadius) continue;
            if (iris::Vec3::dotProduct(centre - segStart, rayDir) < 0.0f) continue;
            if (iris::IntersectionHelper::raySphereIntersects(segStart, rayDir, centre,
                                                              sphereRadius, t, hitPoint)) {
                ScenePick p; p.node = camera.staticCast<iris::SceneNode>(); p.hitPoint = hitPoint;
                p.distanceFromCameraSqrd = (hitPoint - cameraPos).lengthSquared();
                hits.append(p);
            }
        }
    }
    if (includeViewers) {
        for (auto &viewer : scene->viewers) {
            if (viewer->isPickable() &&
                iris::IntersectionHelper::raySphereIntersects(segStart, rayDir, viewer->getGlobalPosition(),
                                                              sphereRadius, t, hitPoint)) {
                ScenePick p; p.node = viewer.staticCast<iris::SceneNode>(); p.hitPoint = hitPoint;
                p.distanceFromCameraSqrd = (hitPoint - cameraPos).lengthSquared();
                hits.append(p);
            }
        }
    }
    return hits;
}

ScenePick ScenePicker::nearest(const QList<ScenePick> &hits)
{
    ScenePick best;
    for (const auto &h : hits)
        if (!best.node || h.distanceFromCameraSqrd < best.distanceFromCameraSqrd) best = h;
    return best;
}

iris::SceneNodePtr ScenePicker::resolveRootSelection(iris::SceneNodePtr picked, iris::SceneNodePtr lastSelected,
                                                     bool selectRootObject)
{
    if (!picked || !selectRootObject) return picked;
    iris::SceneNodePtr pickedRoot = picked;
    // hasParent() as well as isAttached(): `parent` is a weak reference now,
    // so "attached but the parent is gone" is a reachable state and used to be
    // an infinite loop on a null pointer.
    while (pickedRoot->isAttached() && pickedRoot->hasParent()) pickedRoot = pickedRoot->getParent();
    // A click selects the whole asset (its root) — even when a part of it is
    // already selected (a just-dropped asset arrives selected, and the old
    // "same root drills down" rule then sent the FIRST viewport click straight
    // to a sub-mesh). Drilling down needs deliberate aim: only a click on the
    // root that IS the current selection descends to the part under the cursor,
    // and re-clicking the selected part keeps it.
    if (lastSelected == pickedRoot) return picked;
    if (lastSelected == picked) return picked;
    return pickedRoot;
}
