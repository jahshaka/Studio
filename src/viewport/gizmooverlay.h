#ifndef GIZMOOVERLAY_H
#define GIZMOOVERLAY_H

// GizmoOverlay — draws a Gizmo's items through the engine's on-top overlay verbs.
//
// Each frame: ask the gizmo what it would draw (GizmoDrawItem), keep one engine
// node per item slot, convert each handle mesh once (SceneMirror::toMeshData), give
// every slot its own unlit depth-test-off material and push transform + colour.
// Studio code: iris + engine abstraction, never Ogre.
#include "irisgl/core/math/vec.h"
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
    void update(Gizmo *gizmo, const iris::Vec3 &rayPos, const iris::Vec3 &rayDir, const iris::Vec3 &viewDir);
    void clear();
    int visibleItems() const { return mVisible; }

private:
    struct Slot {
        jahshaka::engine::NodeId node = 0;
        jahshaka::engine::MaterialId material = 0;
        jahshaka::engine::MeshId mesh = 0;
        iris::Mesh *source = nullptr;
        bool shown = false;
        /// The colour last pushed to `material`, and whether one ever was.
        /// A gizmo part's colour only changes on hover, but the push (an unlit
        /// const-buffer write) happened per part per frame — fps audit F14.
        QColor colour;
        bool colourPushed = false;
    };
    jahshaka::engine::MeshId meshFor(iris::Mesh *mesh);

    jahshaka::engine::Scene *mTarget;
    QVector<Slot> mSlots;
    QHash<iris::Mesh *, jahshaka::engine::MeshId> mMeshes;
    int mVisible = 0;
};

#endif // GIZMOOVERLAY_H
