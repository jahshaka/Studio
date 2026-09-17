// gizmo.scale_lock — THE SCALE GIZMO'S PRESERVE-RATIO DRAG (SCALE-LOCK-1).
//
// The owner asked for two things and said "both will be cool": a per-node LOCK
// that keeps an object's proportions when one scale channel is edited, and a
// STATELESS Shift-drag that does it for one gesture. This is the VIEWPORT half
// of the second one, and the axis-handle half of the first: the document flag,
// the arithmetic and the three degenerate rules are scripting.e2e.scale_lock,
// and the panel's fields are ui.panel_undo.
//
// The drag is driven through the very calls a mouse press and a mouse move
// make — getHitHandle / startDragging / drag / endDragging, with rays built
// from the DOCUMENT camera's own matrices — so what is asserted is the gesture,
// not a helper.
//
// WHAT IS ASSERTED:
//   A. the ray aimed at the X handle grabs the X handle (the fixture is real);
//   B. a plain drag on an axis handle moves THAT axis only — the behaviour
//      before this lane, unchanged when nothing is locked;
//   C. with the node's ratio LOCKED, the same drag scales all three by the same
//      ratio, measured against the scale the drag started at;
//   D. Shift held for the gesture does it on an UNLOCKED node, and does not
//      touch the flag;
//   E. Shift released mid-drag leaves the other two channels exactly where the
//      gesture found them (the ratio is measured from the press, so an
//      accidental tap leaves no residue);
//   F. the CENTRE handle is untouched by all of it — it has always been the
//      uniform one, in the additive sense its own maths give it;
//   G. the degenerate rules hold in the viewport too: a channel that was ZERO
//      has no ratio, and a negative scale keeps its sign.
//
// Runs on the headless document graph, like gizmo.drag_frame and
// gizmo.plane_handles: no display, no GPU, no pixels.

#include <QGuiApplication>
#include <QPointF>

#include <cmath>
#include <cstdio>

#include "../support/documentgraph.h"

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "viewport/freecamerapolicy.h"
#include "viewport/gizmo.h"
#include "viewport/scalegizmo.h"

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok:   %s\n", msg);                               \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

namespace {

constexpr float kWidth = 1920.0f;
constexpr float kHeight = 1080.0f;

/// A point ON the X handle's segment, in world units: the handle runs from the
/// gizmo's origin out to handleExtent * handleLength * gizmoScale * handleScale
/// (ScaleHandle::isHit), so half way along it is comfortably on it and clear of
/// the centre cube's pick sphere.
iris::Vec3 onXHandle(ScaleGizmo &gizmo, float along = 0.5f)
{
    const float reach = 1.5f * gizmo.getGizmoScale() * 0.05f;   // handleLength * handleScale
    return iris::Vec3(along * reach, 0.0f, 0.0f);
}

struct Rig
{
    iris::ScenePtr doc;
    iris::SceneNodePtr node;
    iris::CameraNodePtr cam;
    iris::Vec3 eye, viewDir;
};

/// The drag, exactly as the viewport drives it: a press aimed at the X handle,
/// two moves aimed at points along X, then the release. `mods` is what the
/// viewport hands the gizmo per event (EngineSceneViewport::mouseMoveEvent);
/// `modsSecondHalf`, when not -1, replaces them for the second move — a key
/// released mid-gesture.
///
/// RETURNS THE SCALE THE DRAG PRODUCED, read after the last move and before the
/// release, because THE RELEASE IS THE UNDO STEP, not the gesture:
/// Gizmo::createUndoAction rewinds the node to the transform it captured at
/// press and hands the new one to a TransformSceneNodeCommand, whose immediate
/// redo() re-applies it — so with no undo service (this suite has none, like
/// gizmo.drag_frame and gizmo.plane_handles) the rewind is where the node ends
/// up. The undo step itself is not this lane's change and is gated by
/// gizmo.group_transform.
iris::Vec3 dragAlongX(ScaleGizmo &gizmo, const Rig &rig, float travel,
                      Qt::KeyboardModifiers mods = Qt::NoModifier,
                      Qt::KeyboardModifiers modsSecondHalf = Qt::KeyboardModifiers(-1))
{
    const iris::Vec3 grab = onXHandle(gizmo);
    const auto rayTo = [&rig](const iris::Vec3 &target, iris::Vec3 &dir) {
        dir = (target - rig.eye).normalized();
    };
    iris::Vec3 dir;
    rayTo(grab, dir);
    gizmo.setDragModifiers(mods);
    gizmo.startDragging(rig.eye, dir, rig.viewDir);

    for (int step = 1; step <= 2; ++step) {
        const iris::Vec3 target = grab + iris::Vec3(travel * float(step) / 2.0f, 0, 0);
        rayTo(target, dir);
        const Qt::KeyboardModifiers now =
            (step == 2 && modsSecondHalf != Qt::KeyboardModifiers(-1)) ? modsSecondHalf : mods;
        gizmo.setDragModifiers(now);
        gizmo.drag(rig.eye, dir, rig.viewDir);
    }
    const iris::Vec3 dragged = rig.node->getLocalScale();
    gizmo.endDragging();
    return dragged;
}

ScaleGizmo *makeGizmo(const Rig &rig)
{
    auto *gizmo = new ScaleGizmo();
    gizmo->setSelectedNode(rig.node);
    gizmo->updateSize(rig.cam);
    gizmo->setPickView(rig.cam, kWidth, kHeight);
    return gizmo;
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("gizmo-scale-lock-ogre.log");
    if (!graph.ok()) { std::printf("FAIL: headless engine: %s\n", graph.error().c_str()); return 1; }

    Rig rig;
    rig.doc = iris::Scene::create();
    rig.node = iris::SceneNode::create();
    rig.doc->getRootNode()->addChild(rig.node);
    rig.cam = iris::CameraNode::create();
    rig.cam->nearClip = 0.05f;
    rig.cam->farClip = 2000.0f;
    rig.cam->angle = 45.0f;
    rig.cam->setFramingAspect(freecam::kFreeCameraFramingAspect);
    rig.doc->getRootNode()->addChild(rig.cam);
    // Off the X axis on all three, so the X handle is neither edge-on nor
    // looked straight down.
    rig.cam->setLocalPos(iris::Vec3(3.0f, 4.0f, 6.0f));
    rig.cam->lookAt(iris::Vec3(0, 0, 0));
    rig.cam->update(0.0f);
    rig.eye = rig.cam->getGlobalPosition();
    rig.viewDir = rig.cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();

    // The starting scale is deliberately NON-UNIFORM: "preserve the ratio" is
    // about the ratio the object has, not about making it a cube.
    const iris::Vec3 start(1.0f, 2.0f, 0.5f);
    const float travel = 0.6f;

    // ---- A: the fixture grabs the handle under the ray, BY DISTANCE --------
    {
        rig.node->setLocalScale(start);
        rig.node->update(0.0f);
        ScaleGizmo *gizmo = makeGizmo(rig);
        const auto axisName = [](ScaleHandle *h) {
            return h == nullptr ? "nothing"
                 : h->axis == GizmoAxis::X ? "the X handle"
                 : h->axis == GizmoAxis::Y ? "the Y handle"
                 : h->axis == GizmoAxis::Z ? "the Z handle" : "the centre";
        };
        const iris::Vec3 grab = onXHandle(*gizmo);
        const iris::Vec3 dir = (grab - rig.eye).normalized();
        iris::Vec3 hit;
        ScaleHandle *handle = gizmo->getHitHandle(rig.eye, dir, rig.viewDir, hit);
        std::printf("    A: the ray at (%.4f, 0, 0) grabs %s\n", double(grab.x()),
                    axisName(handle));
        CHECK(handle != nullptr && handle->axis == GizmoAxis::X,
              "A: a ray aimed half way along the X handle grabs the X handle");

        // …AND THE ANSWER IS THE HANDLE THE RAY IS AIMED AT, with that handle's
        // own hit point. getHitHandle used to measure its "closest" with the
        // caller's OUT parameter instead of the candidate it had just computed
        // (fixed in this round), so after the first hit every later
        // `dist < closestDistance` was false and the first handle in
        // construction order would have won a CONTESTED ray.
        //
        // MEASURED, and worth writing down: at this pin's geometry that
        // contest cannot be staged. An axis handle's pick radius is
        // handleScale^2 * gizmoScale (0.0025 of it), the three segments meet
        // only at the origin, and any ray passing within that radius of two of
        // them also passes inside the CENTRE handle's pick sphere
        // (0.015 * gizmoScale, six times larger) — and the centre is tested
        // first and short-circuits. So the defect was real but unreachable: the
        // arm below is a regression guard on "the handle answered is the one
        // aimed at, and the point it reports is on THAT handle", and flipping
        // the fixed line back does not change its verdict.
        const float reach = 1.5f * gizmo->getGizmoScale() * 0.05f;
        const iris::Vec3 onZ(0.0f, 0.0f, 0.5f * reach);
        const iris::Vec3 zDir = (onZ - rig.eye).normalized();
        ScaleHandle *zHandle = gizmo->getHitHandle(rig.eye, zDir, rig.viewDir, hit);
        std::printf("    A: the ray at (0, 0, %.4f) grabs %s, and the point it reports is "
                    "(%.4f, %.4f, %.4f)\n", double(onZ.z()), axisName(zHandle),
                    double(hit.x()), double(hit.y()), double(hit.z()));
        CHECK(zHandle != nullptr && zHandle->axis == GizmoAxis::Z,
              "A: a ray aimed at the Z handle grabs Z, not the first handle in the list");
        CHECK(std::fabs(hit.z() - onZ.z()) < 0.2f * reach,
              "A: …and the hit point it hands back is the one on THAT handle");
        delete gizmo;
    }

    // ---- B: a plain drag is one axis (today's behaviour) -------------------
    float plainX = 0.0f;
    {
        rig.node->setLocalScale(start);
        rig.node->update(0.0f);
        ScaleGizmo *gizmo = makeGizmo(rig);
        const iris::Vec3 now = dragAlongX(*gizmo, rig, travel);
        plainX = now.x();
        std::printf("    B: unlocked drag -> scale %.4f/%.4f/%.4f\n", double(now.x()),
                    double(now.y()), double(now.z()));
        CHECK(std::fabs(now.x() - start.x()) > 0.05f,
              "B: an unlocked axis drag moves that axis (the drag is real)");
        CHECK(std::fabs(now.y() - start.y()) < 1e-4f && std::fabs(now.z() - start.z()) < 1e-4f,
              "B: …and nothing else moves");
        delete gizmo;
    }

    // ---- C: the node's LOCK makes the same drag uniform -------------------
    {
        rig.node->setLocalScale(start);
        rig.node->setScaleLock(true);
        rig.node->update(0.0f);
        ScaleGizmo *gizmo = makeGizmo(rig);
        const iris::Vec3 now = dragAlongX(*gizmo, rig, travel);
        const float ratio = now.x() / start.x();
        std::printf("    C: locked drag -> scale %.4f/%.4f/%.4f (ratio %.4f; y and z predict "
                    "%.4f/%.4f)\n", double(now.x()), double(now.y()), double(now.z()),
                    double(ratio), double(start.y() * ratio), double(start.z() * ratio));
        CHECK(std::fabs(now.x() - plainX) < 1e-4f,
              "C: the locked drag reaches the same X as the plain one (the handle maths are "
              "untouched)");
        CHECK(std::fabs(now.y() - start.y() * ratio) < 1e-4f &&
                  std::fabs(now.z() - start.z() * ratio) < 1e-4f,
              "C: …and the other two are scaled by that same ratio");
        delete gizmo;
        rig.node->setScaleLock(false);
    }

    // ---- D: Shift does it for the gesture only ----------------------------
    {
        rig.node->setLocalScale(start);
        rig.node->update(0.0f);
        ScaleGizmo *gizmo = makeGizmo(rig);
        const iris::Vec3 now = dragAlongX(*gizmo, rig, travel, Qt::ShiftModifier);
        const float ratio = now.x() / start.x();
        std::printf("    D: Shift drag -> scale %.4f/%.4f/%.4f\n", double(now.x()),
                    double(now.y()), double(now.z()));
        CHECK(std::fabs(now.y() - start.y() * ratio) < 1e-4f &&
                  std::fabs(now.z() - start.z() * ratio) < 1e-4f,
              "D: Shift held for the drag scales all three on an UNLOCKED node");
        CHECK(!rig.node->getScaleLock(),
              "D: …and the gesture is stateless — the flag is still off");
        delete gizmo;
    }

    // ---- E: released mid-drag, no residue ---------------------------------
    {
        rig.node->setLocalScale(start);
        rig.node->update(0.0f);
        ScaleGizmo *gizmo = makeGizmo(rig);
        const iris::Vec3 now = dragAlongX(*gizmo, rig, travel, Qt::ShiftModifier, Qt::NoModifier);
        std::printf("    E: Shift released mid-drag -> scale %.4f/%.4f/%.4f\n", double(now.x()),
                    double(now.y()), double(now.z()));
        CHECK(std::fabs(now.x() - plainX) < 1e-4f,
              "E: the released drag still reaches the X the gesture asked for");
        CHECK(std::fabs(now.y() - start.y()) < 1e-4f && std::fabs(now.z() - start.z()) < 1e-4f,
              "E: …and the other two are back exactly where the gesture found them (the ratio "
              "is measured from the press, so a tap leaves no residue)");
        delete gizmo;
    }

    // ---- F: the centre handle is not this lane's gesture -------------------
    {
        rig.node->setLocalScale(start);
        rig.node->setScaleLock(true);
        rig.node->update(0.0f);
        ScaleGizmo *gizmo = makeGizmo(rig);
        // The centre cube: a ray straight at the gizmo's origin.
        const iris::Vec3 dir = (iris::Vec3(0, 0, 0) - rig.eye).normalized();
        iris::Vec3 hit;
        ScaleHandle *handle = gizmo->getHitHandle(rig.eye, dir, rig.viewDir, hit);
        const bool centre = handle && handle->axis == GizmoAxis::Center;
        gizmo->setDragModifiers(Qt::ShiftModifier);
        gizmo->startDragging(rig.eye, dir, rig.viewDir);
        const iris::Vec3 target(0.3f, 0.3f, 0.3f);
        gizmo->drag(rig.eye, (target - rig.eye).normalized(), rig.viewDir);
        const iris::Vec3 now = rig.node->getLocalScale();
        gizmo->endDragging();
        std::printf("    F: centre drag (locked, Shift held) -> scale %.4f/%.4f/%.4f; the "
                    "additive rule predicts %+.4f on each\n", double(now.x()), double(now.y()),
                    double(now.z()), double(now.x() - start.x()));
        CHECK(centre, "F: a ray at the origin grabs the CENTRE handle");
        // Its rule is the same length onto all three — additive, not a ratio —
        // and neither the lock nor Shift changes it.
        const float dx = now.x() - start.x();
        CHECK(std::fabs((now.y() - start.y()) - dx) < 1e-4f &&
                  std::fabs((now.z() - start.z()) - dx) < 1e-4f,
              "F: …and it is still the ADDITIVE uniform gesture it always was, lock or no lock");
        delete gizmo;
        rig.node->setScaleLock(false);
    }

    // ---- G: the degenerate rules, in the viewport --------------------------
    {
        // A ZERO CHANNEL HAS NO RATIO: x takes the drag, y and z keep theirs.
        rig.node->setLocalScale(iris::Vec3(0.0f, 2.0f, 0.5f));
        rig.node->setScaleLock(true);
        rig.node->update(0.0f);
        ScaleGizmo *gizmo = makeGizmo(rig);
        iris::Vec3 now = dragAlongX(*gizmo, rig, travel);
        std::printf("    G: locked drag from x = 0 -> scale %.4f/%.4f/%.4f\n", double(now.x()),
                    double(now.y()), double(now.z()));
        CHECK(std::fabs(now.y() - 2.0f) < 1e-4f && std::fabs(now.z() - 0.5f) < 1e-4f,
              "G: a channel that was ZERO has no ratio — the other two keep their values");
        delete gizmo;

        // A NEGATIVE START KEEPS ITS SIGN: the ratio is positive while the drag
        // makes x more negative, so y and z grow with it and stay positive.
        rig.node->setLocalScale(iris::Vec3(-1.0f, 2.0f, 0.5f));
        rig.node->update(0.0f);
        gizmo = makeGizmo(rig);
        now = dragAlongX(*gizmo, rig, -travel);
        const float ratio = now.x() / -1.0f;
        std::printf("    G: locked drag from x = -1 -> scale %.4f/%.4f/%.4f (ratio %.4f)\n",
                    double(now.x()), double(now.y()), double(now.z()), double(ratio));
        CHECK(now.x() < -1.0f && ratio > 1.0f,
              "G: dragging a negative channel further negative is a POSITIVE ratio");
        CHECK(std::fabs(now.y() - 2.0f * ratio) < 1e-4f &&
                  std::fabs(now.z() - 0.5f * ratio) < 1e-4f,
              "G: …and the other two grow with it, signs intact");
        delete gizmo;
        rig.node->setScaleLock(false);
    }

    std::printf(failures == 0 ? "gizmo.scale_lock: ALL OK\n" : "gizmo.scale_lock: %d FAILURES\n",
                failures);
    return failures == 0 ? 0 : 1;
}
