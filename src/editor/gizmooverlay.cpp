#include "gizmooverlay.h"
#include <QMatrix3x3>
#include <QQuaternion>
#include "../engine/scenemirror.h"
#include "../irisgl/src/graphics/mesh.h"

using namespace jahshaka::engine;

GizmoOverlay::GizmoOverlay(Scene *target) : mTarget(target) {}

GizmoOverlay::~GizmoOverlay()
{
    // The engine scene may already be gone; callers destroy the overlay first.
}

MeshId GizmoOverlay::meshFor(iris::Mesh *mesh)
{
    auto it = mMeshes.constFind(mesh);
    if (it != mMeshes.constEnd()) return it.value();
    MeshData data;
    if (!SceneMirror::toMeshData(mesh, data)) return 0;
    MeshId id = mTarget->createMesh(data);
    if (id) mMeshes.insert(mesh, id);
    return id;
}

void GizmoOverlay::update(Gizmo *gizmo, const QVector3D &rayPos, const QVector3D &rayDir, const QVector3D &viewDir)
{
    QVector<GizmoDrawItem> items;
    if (gizmo) items = gizmo->drawItems(rayPos, rayDir, viewDir);
    mVisible = 0;
    for (int i = 0; i < items.size(); ++i) {
        const GizmoDrawItem &item = items[i];
        if (i >= mSlots.size()) mSlots.append(Slot());
        Slot &slot = mSlots[i];
        if (!slot.node) slot.node = mTarget->createNode();
        if (!slot.material)
            slot.material = mTarget->createUnlitMaterial(Colour(1, 1, 1), false);   // on top
        if (!slot.node || !slot.material) continue;
        iris::Mesh *src = item.mesh.data();
        if (slot.source != src) {
            MeshId m = meshFor(src);
            if (m && mTarget->attachMesh(slot.node, m, slot.material)) { slot.mesh = m; slot.source = src; }
        }
        // Decompose the gizmo's world matrix into TRS for the engine.
        const QMatrix4x4 &t = item.transform;
        const QVector3D cx = t.column(0).toVector3D(), cy = t.column(1).toVector3D(), cz = t.column(2).toVector3D();
        const QVector3D scale(cx.length(), cy.length(), cz.length());
        const QVector3D pos = t.column(3).toVector3D();
        float m[9] = { cx.x() / scale.x(), cy.x() / scale.y(), cz.x() / scale.z(),
                       cx.y() / scale.x(), cy.y() / scale.y(), cz.y() / scale.z(),
                       cx.z() / scale.x(), cy.z() / scale.y(), cz.z() / scale.z() };
        const QQuaternion rot = QQuaternion::fromRotationMatrix(QMatrix3x3(m));
        mTarget->setNodeTransform(slot.node, Vec3(pos.x(), pos.y(), pos.z()),
                                  Quat(rot.x(), rot.y(), rot.z(), rot.scalar()),
                                  Vec3(scale.x(), scale.y(), scale.z()));
        mTarget->setUnlitMaterial(slot.material, Colour(item.colour.redF(), item.colour.greenF(), item.colour.blueF(), 1.0f));
        mTarget->setNodeVisible(slot.node, true);
        slot.shown = true;
        ++mVisible;
    }
    for (int i = items.size(); i < mSlots.size(); ++i) {
        if (mSlots[i].shown && mSlots[i].node) mTarget->setNodeVisible(mSlots[i].node, false);
        mSlots[i].shown = false;
    }
}

void GizmoOverlay::clear()
{
    for (Slot &s : mSlots) if (s.node) mTarget->removeNode(s.node);
    for (Slot &s : mSlots) if (s.material) mTarget->destroyMaterial(s.material);
    for (MeshId m : mMeshes) mTarget->destroyMesh(m);
    mSlots.clear(); mMeshes.clear(); mVisible = 0;
}
