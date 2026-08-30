#ifndef GIZMOOVERLAY_H
#define GIZMOOVERLAY_H

// GizmoOverlay — draws a Gizmo's items through the engine's on-top overlay verbs.
//
// Each frame: ask the gizmo what it would draw (GizmoDrawItem), keep one engine
// node per item slot, convert each handle mesh once (SceneMirror::toMeshData), give
// every slot its own unlit depth-test-off material and push transform + colour.
// Studio code: iris + engine abstraction, never Ogre.
#include <QHash>
#include <QVector>
#include "viewport/gizmo.h"
#include "jahshaka/engine/Engine.h"

class GizmoOverlay
{
public:
    explicit GizmoOverlay(jahshaka::engine::Scene *target);
    ~GizmoOverlay();

    /// Pushes this frame's items. A null gizmo (or nothing selected) hides everything.
    void update(Gizmo *gizmo, const QVector3D &rayPos, const QVector3D &rayDir, const QVector3D &viewDir);
    void clear();
    int visibleItems() const { return mVisible; }

private:
    struct Slot {
        jahshaka::engine::NodeId node = 0;
        jahshaka::engine::MaterialId material = 0;
        jahshaka::engine::MeshId mesh = 0;
        iris::Mesh *source = nullptr;
        bool shown = false;
    };
    jahshaka::engine::MeshId meshFor(iris::Mesh *mesh);

    jahshaka::engine::Scene *mTarget;
    QVector<Slot> mSlots;
    QHash<iris::Mesh *, jahshaka::engine::MeshId> mMeshes;
    int mVisible = 0;
};

#endif // GIZMOOVERLAY_H
