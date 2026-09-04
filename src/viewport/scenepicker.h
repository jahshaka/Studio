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
    /// triangles in local space; lights, viewers and decals as 0.5-unit spheres
    /// (a decal has no geometry to hit — its projector box is a helper, and
    /// clicking anywhere near the origin marker selects it).
    ///
    /// `refreshTransforms` runs the document's global-transform update first.
    /// Callers that already ran it this frame — anything inside a live drag,
    /// where the mirror's sync() updates the tree every frame — pass false:
    /// the update is a full recursive walk of the scene, and V-hold vertex
    /// snapping calls this on every mouse move.
    static QList<ScenePick> pickAll(iris::ScenePtr scene, const iris::Vec3 &segStart, const iris::Vec3 &segEnd,
                                    const iris::Vec3 &cameraPos, bool forcePickable = false,
                                    bool includeLights = true, bool includeViewers = true,
                                    bool includeDecals = true, bool refreshTransforms = true);

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
    static void pickMeshes(const iris::SceneNodePtr &node, const iris::Vec3 &a, const iris::Vec3 &b,
                           const iris::Vec3 &cameraPos, bool forcePickable, QList<ScenePick> &out);
};

#endif // SCENEPICKER_H
