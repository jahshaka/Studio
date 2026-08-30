#ifndef SCENEPICKER_H
#define SCENEPICKER_H

// ScenePicker — object picking on the iris:: DOCUMENT, independent of any renderer.
//
// Ported from SceneViewWidget's CPU picking (segment vs. mesh triangles, ray vs.
// light/viewer spheres, root-vs-child selection rule) so the engine-backed
// viewport picks exactly what the legacy one did. Needs no GL: triangle data lives
// in the document's TriMesh. VIEWPORT_MIGRATION_PLAN.md step 7.
#include <QList>
#include <QPointF>
#include <QVector3D>
#include "irisgl/irisglfwd.h"

struct ScenePick
{
    iris::SceneNodePtr node;
    QVector3D hitPoint;
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
                              const QPointF &point, QVector3D &segStart, QVector3D &segEnd);

    /// Every hit along the segment, unsorted. Meshes are tested against their
    /// triangles in local space; lights and viewers as 0.5-unit spheres.
    static QList<ScenePick> pickAll(iris::ScenePtr scene, const QVector3D &segStart, const QVector3D &segEnd,
                                    const QVector3D &cameraPos, bool forcePickable = false,
                                    bool includeLights = true, bool includeViewers = true);

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
    static void pickMeshes(iris::SceneNodePtr node, const QVector3D &a, const QVector3D &b,
                           const QVector3D &cameraPos, bool forcePickable, QList<ScenePick> &out);
};

#endif // SCENEPICKER_H
