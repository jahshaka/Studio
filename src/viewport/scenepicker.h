#ifndef SCENEPICKER_H
#define SCENEPICKER_H

// ScenePicker — object picking on the iris:: DOCUMENT, independent of any renderer.
//
// Ported from SceneViewWidget's CPU picking (segment vs. mesh triangles, ray vs.
// light/viewer spheres, root-vs-child selection rule) so the engine-backed
// viewport picks exactly what the legacy one did. Needs no GL: triangle data lives
// in the document's TriMesh. VIEWPORT_MIGRATION_PLAN.md step 7.
#include "irisgl/core/math/vec.h"
#include <QList>
#include <QPointF>
#include "irisgl/irisglfwd.h"

struct ScenePick
{
    iris::SceneNodePtr node;
    iris::Vec3 hitPoint;
    float distanceFromCameraSqrd = 0.0f;
    /// Index into the mesh's TriMesh triangle list for mesh hits, -1 for
    /// light/viewer sphere hits — V-hold vertex snapping reads the triangle's
    /// corners back (EDITOR_SHORTCUTS_SPEC §4).
    int triangleIndex = -1;
};

class ScenePicker
{
public:
    /// World-space segment through a viewport pixel, from the near to the far plane,
    /// for a document camera drawn at `viewportWidth` x `viewportHeight`.
    static void screenSegment(iris::CameraNodePtr camera, int viewportWidth, int viewportHeight,
                              const QPointF &point, iris::Vec3 &segStart, iris::Vec3 &segEnd);

    /// Every hit along the segment, unsorted. Meshes are tested against their
    /// triangles in local space; lights, viewers, decals and CAMERAS as
    /// 0.5-unit spheres (none of them has geometry to hit — the icon, the
    /// projector box and the camera body are helpers, and clicking near the
    /// origin marker selects the node).
    ///
    /// Cameras join that list in CAMERAS_SPEC phase 2b, on the same route the
    /// light icons take: the body the engine draws is engine-side only, so
    /// document picking cannot see it and hits the node's origin sphere
    /// instead. The body is authored around the origin (its case spans
    /// z = -0.3..0.3), so the sphere is under the pixels the user aims at.
    ///
    /// `refreshTransforms` runs the document's global-transform update first.
    /// Callers that already ran it this frame — anything inside a live drag,
    /// where the mirror's sync() updates the tree every frame — pass false:
    /// the update is a full recursive walk of the scene, and V-hold vertex
    /// snapping calls this on every mouse move.
    static QList<ScenePick> pickAll(iris::ScenePtr scene, const iris::Vec3 &segStart, const iris::Vec3 &segEnd,
                                    const iris::Vec3 &cameraPos, bool forcePickable = false,
                                    bool includeLights = true, bool includeViewers = true,
                                    bool includeDecals = true, bool refreshTransforms = true,
                                    bool includeCameras = true);

    /// The nearest hit, or a null node.
    static ScenePick nearest(const QList<ScenePick> &hits);

    /// The selection rule: with `selectRootObject`, clicking an attached child
    /// selects its root (the whole asset). Only a click on the root that is
    /// ALREADY the current selection drills down to the part under the cursor;
    /// re-clicking a selected part keeps it.
    static iris::SceneNodePtr resolveRootSelection(iris::SceneNodePtr picked,
                                                   iris::SceneNodePtr lastSelected,
                                                   bool selectRootObject);

private:
    /// Delegates to iris::picking::raycastMeshes — the ONE segment/mesh
    /// implementation (Ogre RaySceneQuery broad phase + our TriMesh narrow
    /// phase); this converts its hits to the picker's camera-relative ranking.
    static void pickMeshes(iris::ScenePtr scene, const iris::Vec3 &a, const iris::Vec3 &b,
                           const iris::Vec3 &cameraPos, bool forcePickable, QList<ScenePick> &out);
};

#endif // SCENEPICKER_H
