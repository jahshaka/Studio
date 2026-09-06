#include "irisgl/core/math/vec.h"
#include "viewport/gizmooverlay.h"
#include "irisgl/mirror/scenemirror.h"
#include "irisgl/document/assets/mesh.h"

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

void GizmoOverlay::update(Gizmo *gizmo, const iris::Vec3 &rayPos, const iris::Vec3 &rayDir, const iris::Vec3 &viewDir)
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
        SceneMirror::pushTransform(mTarget, slot.node, item.transform);
        // COLOUR ON CHANGE ONLY (fps audit F14). setUnlitMaterial schedules a
        // const-buffer write, and this ran for every part of the gizmo on every
        // frame the gizmo was up — while the only thing that ever changes a
        // part's colour is the mouse moving onto or off it. The transform above
        // genuinely does change every frame (the gizmo is screen-scaled and
        // follows the camera), so it stays unconditional.
        if (!slot.colourPushed || slot.colour != item.colour) {
            mTarget->setUnlitMaterial(slot.material, Colour(item.colour.redF(), item.colour.greenF(),
                                                            item.colour.blueF(), 1.0f));
            slot.colour = item.colour;
            slot.colourPushed = true;
        }
        if (!slot.shown) { mTarget->setNodeVisible(slot.node, true); slot.shown = true; }
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
