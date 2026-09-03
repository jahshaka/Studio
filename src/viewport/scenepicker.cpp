#include "viewport/scenepicker.h"

#include <QMatrix4x4>
#include <QVector4D>
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
#include "irisgl/core/math/intersectionhelper.h"

namespace {
QVector3D unproject(const iris::CameraNodePtr &cam, int w, int h, const QPointF &pos, float depth)
{
    const float mousex = (2.0f * float(pos.x())) / float(w) - 1.0f;
    const float mousey = (2.0f * float(pos.y())) / float(h) - 1.0f;
    const QVector4D hcc(mousex, -mousey, depth, 1.0f);
    const QVector4D eye = cam->projMatrix.inverted() * hcc;
    const QVector4D world = cam->viewMatrix.inverted() * eye;
    return world.toVector3D() / world.w();
}
}

void ScenePicker::screenSegment(iris::CameraNodePtr camera, int w, int h, const QPointF &point,
                                QVector3D &segStart, QVector3D &segEnd)
{
    if (!camera || w <= 0 || h <= 0) { segStart = segEnd = QVector3D(); return; }
    camera->setAspectRatio(float(w) / float(h));
    camera->update(0.0f);
    camera->updateCameraMatrices();
    segStart = unproject(camera, w, h, point, -1.0f);
    segEnd   = unproject(camera, w, h, point,  1.0f);
}

void ScenePicker::pickMeshes(iris::SceneNodePtr node, const QVector3D &segStart, const QVector3D &segEnd,
                             const QVector3D &cameraPos, bool forcePickable, QList<ScenePick> &out)
{
    if (!node) return;
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh && (node->isPickable() || forcePickable)) {
        auto meshNode = node.staticCast<iris::MeshNode>();
        auto mesh = meshNode->getMesh();
        if (mesh && mesh->getTriMesh()) {
            // Segment into the mesh's local space, hits back to world space.
            const QMatrix4x4 inv = meshNode->globalTransform.inverted();
            const QVector3D a = inv * segStart, b = inv * segEnd;
            QList<iris::TriangleIntersectionResult> results;
            if (mesh->getTriMesh()->getSegmentIntersections(a, b, results)) {
                for (const auto &r : results) {
                    ScenePick p;
                    p.node = node;
                    p.hitPoint = meshNode->globalTransform * r.hitPoint;
                    p.distanceFromCameraSqrd = (p.hitPoint - cameraPos).lengthSquared();
                    p.triangleIndex = r.triangleIndex;
                    out.append(p);
                }
            }
        }
    }
    for (auto &child : node->children)
        pickMeshes(child, segStart, segEnd, cameraPos, forcePickable, out);
}

QList<ScenePick> ScenePicker::pickAll(iris::ScenePtr scene, const QVector3D &segStart, const QVector3D &segEnd,
                                      const QVector3D &cameraPos, bool forcePickable,
                                      bool includeLights, bool includeViewers,
                                      bool includeDecals)
{
    QList<ScenePick> hits;
    if (!scene || !scene->getRootNode()) return hits;
    scene->getRootNode()->update(0.0f);   // fresh global transforms
    pickMeshes(scene->getRootNode(), segStart, segEnd, cameraPos, forcePickable, hits);

    const float sphereRadius = 0.5f;
    QVector3D rayDir = (segEnd - segStart).normalized();
    QVector3D hitPoint; float t;
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
    while (pickedRoot->isAttached()) pickedRoot = pickedRoot->parent;
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
