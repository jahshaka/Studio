/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// input.axis_view_lock — THE AXIS-VIEW ROTATION LOCK (owner report 2026-09-08:
// "when in Top/Left/Right/Bottom views we should not be able to rotate the
// camera — only pan and zoom; the camera should be locked top-down, bottom-up
// etc").
//
// The lock lives on CameraControllerBase and is armed by the viewport
// (EngineSceneViewport::cameraRotationLocked — an axis view, and not piloting);
// what it MEANS is entirely in the two editor controllers, so this suite drives
// them at the gesture level, exactly as EngineSceneViewport's mouse handlers do:
// onMouseDown(button) then onMouseMove(dx, dy), onMouseWheel(delta), and
// onKeyPressed + update(dt) for the fly keys.
//
// WHAT IS PINNED, for BOTH controllers (free fly and arcball):
//   * a rotation gesture — RMB look drag, Alt+LMB orbit, the arcball's drag —
//     leaves the rotation quaternion BIT-FOR-BIT unchanged while locked, and
//     changes it while not locked (a test that only proves the first half would
//     pass on a controller that never rotates at all);
//   * an MMB pan still moves the camera, and moves it IN THE VIEW PLANE: the
//     displacement's component along the view axis is zero;
//   * the wheel still zooms — orthoSize moves, the pose does not;
//   * the fly keys still fly, on the camera's own basis: Up/Down pan up and
//     down the screen, Left/Right strafe, PageUp/PageDown dolly along the view
//     axis, and none of them touches the rotation (the editor's fly moved from
//     W/A/S/D/Q/E to the arrow cluster on 2026-09-09; the letters are free);
//   * unlocking restores rotation.
//
// Plus the pole defect this work found: strafing with the fly keys used
// forward x worldUp, which DEGENERATES to a zero vector when the camera looks
// straight down (a top view is exactly that pose) — Vec3::normalized() returns
// zero there, so A and D silently did nothing. The fallback to the camera's own
// right vector is asserted in BOTH the locked and the unlocked pose.
//
// No display, no engine view, no GPU: a camera node plus the two controllers.

#include <QGuiApplication>
#include <cmath>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "../support/documentgraph.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "viewport/editorcameracontroller.h"
#include "viewport/orbitalcameracontroller.h"
#include "viewport/flyspeedsettings.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } \
} while (0)

namespace {

bool nearf(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

bool sameRot(const iris::Quat &a, const iris::Quat &b)
{
    return a.x() == b.x() && a.y() == b.y() && a.z() == b.z() && a.scalar() == b.scalar();
}

bool samePos(const iris::Vec3 &a, const iris::Vec3 &b)
{
    return nearf(a.x(), b.x(), 1e-5f) && nearf(a.y(), b.y(), 1e-5f) && nearf(a.z(), b.z(), 1e-5f);
}

void printPose(const char *what, const iris::CameraNodePtr &cam)
{
    const iris::Vec3 p = cam->getLocalPos();
    const iris::Quat r = cam->getLocalRot();
    std::printf("    %-28s pos (%7.3f %7.3f %7.3f)  rot (%6.3f %6.3f %6.3f %6.3f)  ortho %.3f\n",
                what, p.x(), p.y(), p.z(), r.x(), r.y(), r.z(), r.scalar(), cam->orthoSize);
}

/// The camera every case starts from: the explorer's shipped pose.
iris::CameraNodePtr freshCamera(bool orthographic)
{
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(iris::Vec3(0, 5, 14));
    cam->setLocalRot(iris::Quat());
    cam->projMode = orthographic ? iris::CameraProjection::Orthogonal
                                 : iris::CameraProjection::Perspective;
    cam->setOrthagonalZoom(10.0f);
    cam->update(0.0f);
    return cam;
}

/// The camera's view axis (what an axis view is looking down).
iris::Vec3 viewAxis(const iris::CameraNodePtr &cam)
{
    return cam->getLocalRot().rotatedVector(iris::Vec3(0, 0, -1));
}

/// How far a displacement left the view plane. Zero = a pure in-plane pan.
float outOfPlane(const iris::Vec3 &delta, const iris::Vec3 &axis)
{
    return delta.x() * axis.x() + delta.y() * axis.y() + delta.z() * axis.z();
}

// ---- the free (fly) camera in an axis view ------------------------------
// setAxisView is what the viewport calls for top (yaw 0, pitch -90); the lock
// is armed AFTER it, exactly as EngineSceneViewport::setCameraView does.
struct FreeRig {
    EditorCameraController c{nullptr};
    iris::CameraNodePtr cam;
    FreeRig(float yawDeg, float pitchDeg, bool ortho, bool locked)
        : cam(freshCamera(ortho))
    {
        c.setCamera(cam);
        c.setAxisView(yawDeg, pitchDeg);
        c.setRotationLocked(locked);
    }
};

struct OrbitRig {
    OrbitalCameraController c{nullptr};
    iris::CameraNodePtr cam;
    OrbitRig(float yawDeg, float pitchDeg, bool ortho, bool locked)
        : cam(freshCamera(ortho))
    {
        c.setCamera(cam);
        c.setAxisView(yawDeg, pitchDeg);
        // The arcball's snap is a LERP inside update(); run it out before the
        // gesture so the pose under test is the settled axis view.
        for (int i = 0; i < 64; ++i) c.update(1.0f / 60.0f);
        c.setRotationLocked(locked);
    }
};

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    // A camera node IS an Ogre scene node since the scene-graph swap, so even a
    // suite that only moves one needs the headless engine underneath it.
    enginetest::DocumentGraph graph("axis-view-lock-ogre.log");
    FlySpeedSettings::reset();

    // ---- 1. the free camera, TOP view, locked ----------------------------
    {
        FreeRig r(0.f, -90.f, true, true);
        printPose("free/top after the snap", r.cam);
        const iris::Vec3 axis = viewAxis(r.cam);
        CHECK(nearf(axis.x(), 0.f) && nearf(axis.y(), -1.f) && nearf(axis.z(), 0.f),
              "free: the top view looks straight DOWN (-Y) after setAxisView");

        const iris::Quat rot0 = r.cam->getLocalRot();
        const iris::Vec3 pos0 = r.cam->getLocalPos();

        // THE RMB LOOK DRAG.
        r.c.onMouseDown(Qt::RightButton);
        r.c.onMouseMove(40, -25);
        r.c.onMouseMove(40, -25);
        printPose("free/top after RMB drag", r.cam);
        CHECK(sameRot(r.cam->getLocalRot(), rot0),
              "free/LOCKED: an RMB look drag does not rotate the camera AT ALL");
        CHECK(samePos(r.cam->getLocalPos(), pos0),
              "free/LOCKED: …and does not move it either");
        r.c.onMouseUp(Qt::RightButton);

        // ALT+LMB ORBIT.
        r.c.setAltOrbit(true, iris::Vec3(0, 0, 0));
        r.c.onMouseDown(Qt::LeftButton);
        r.c.onMouseMove(35, 20);
        printPose("free/top after Alt-orbit", r.cam);
        CHECK(sameRot(r.cam->getLocalRot(), rot0) && samePos(r.cam->getLocalPos(), pos0),
              "free/LOCKED: an Alt+LMB orbit is IGNORED — it neither turns nor pans");
        r.c.onMouseUp(Qt::LeftButton);
        r.c.setAltOrbit(false, iris::Vec3());

        // THE MMB PAN.
        r.c.onMouseDown(Qt::MiddleButton);
        r.c.onMouseMove(50, 30);
        printPose("free/top after MMB pan", r.cam);
        const iris::Vec3 panned = r.cam->getLocalPos();
        const iris::Vec3 delta(panned.x() - pos0.x(), panned.y() - pos0.y(), panned.z() - pos0.z());
        CHECK(sameRot(r.cam->getLocalRot(), rot0), "free/LOCKED: an MMB pan does not rotate");
        CHECK(!samePos(panned, pos0), "free/LOCKED: an MMB pan DOES move the camera");
        CHECK(nearf(outOfPlane(delta, axis), 0.f, 1e-5f),
              "free/LOCKED: …and the move is entirely IN THE VIEW PLANE (no dolly)");
        r.c.onMouseUp(Qt::MiddleButton);

        // THE WHEEL: ortho zoom, nothing else.
        const iris::Vec3 beforeWheel = r.cam->getLocalPos();
        const float ortho0 = r.cam->orthoSize;
        r.c.onMouseWheel(120);
        printPose("free/top after wheel", r.cam);
        CHECK(r.cam->orthoSize < ortho0, "free/LOCKED: the wheel zooms (orthoSize shrinks)");
        CHECK(sameRot(r.cam->getLocalRot(), rot0) && samePos(r.cam->getLocalPos(), beforeWheel),
              "free/LOCKED: …and moves nothing else");
    }

    // ---- 2. the free camera, top view, NOT locked: the drag DOES rotate ---
    {
        FreeRig r(0.f, -90.f, true, false);
        const iris::Quat rot0 = r.cam->getLocalRot();
        r.c.onMouseDown(Qt::RightButton);
        r.c.onMouseMove(40, -25);
        printPose("free/top UNLOCKED after drag", r.cam);
        CHECK(!sameRot(r.cam->getLocalRot(), rot0),
              "free/UNLOCKED: the same drag rotates — the lock is what stops it, "
              "not a controller that cannot turn");
    }

    // ---- 3. the free camera's fly keys in a locked axis view -------------
    {
        // One second of flight with `keys` held, from the settled top view.
        auto fly = [](std::initializer_list<Qt::Key> keys, bool locked) {
            FreeRig r(0.f, -90.f, true, locked);
            const iris::Vec3 pos0 = r.cam->getLocalPos();
            const iris::Quat rot0 = r.cam->getLocalRot();
            r.c.onMouseDown(Qt::RightButton);          // the fly only runs while RMB is held
            for (Qt::Key k : keys) r.c.onKeyPressed(k);
            r.c.update(1.0f);
            const iris::Vec3 p = r.cam->getLocalPos();
            const bool turned = !sameRot(r.cam->getLocalRot(), rot0);
            return std::make_pair(iris::Vec3(p.x() - pos0.x(), p.y() - pos0.y(), p.z() - pos0.z()),
                                  turned);
        };

        const auto up   = fly({ Qt::Key_Up }, true);
        const auto down = fly({ Qt::Key_Down }, true);
        const auto rght = fly({ Qt::Key_Right }, true);
        const auto left = fly({ Qt::Key_Left }, true);
        const auto pgup = fly({ Qt::Key_PageUp }, true);
        const auto pgdn = fly({ Qt::Key_PageDown }, true);
        std::printf("    locked fly Up (%.3f %.3f %.3f)  Right (%.3f %.3f %.3f)  "
                    "PgUp (%.3f %.3f %.3f)\n",
                    up.first.x(), up.first.y(), up.first.z(),
                    rght.first.x(), rght.first.y(), rght.first.z(),
                    pgup.first.x(), pgup.first.y(), pgup.first.z());
        CHECK(!up.second && !down.second && !rght.second && !left.second &&
              !pgup.second && !pgdn.second,
              "free/LOCKED: no fly key rotates the camera");
        // Top view: the view axis is -Y, the screen's up is world -Z, right is +X.
        CHECK(nearf(up.first.y(), 0.f) && up.first.z() < -1.0f,
              "free/LOCKED: Up PANS up the screen (world -Z in a top view), it does not dolly");
        CHECK(nearf(down.first.y(), 0.f) && down.first.z() > 1.0f,
              "free/LOCKED: Down pans back down");
        CHECK(nearf(rght.first.y(), 0.f) && rght.first.x() > 1.0f,
              "free/LOCKED: Right strafes right IN THE PLANE (the pole where forward x up dies)");
        CHECK(nearf(left.first.y(), 0.f) && left.first.x() < -1.0f,
              "free/LOCKED: Left strafes left");
        CHECK(pgup.first.y() > 1.0f && nearf(pgup.first.x(), 0.f) && nearf(pgup.first.z(), 0.f),
              "free/LOCKED: PageUp dollies OUT along the view axis (up, from a top view)");
        CHECK(pgdn.first.y() < -1.0f, "free/LOCKED: PageDown dollies in");

        // ...and the letters do nothing in an axis view either.
        const auto wLocked = fly({ Qt::Key_W }, true);
        const auto eLocked = fly({ Qt::Key_E }, true);
        CHECK(wLocked.first.isNull() && eLocked.first.isNull(),
              "free/LOCKED: W and E are not fly keys any more, here either");

        // THE POLE DEFECT, unlocked: a perspective camera looking straight down
        // strafed NOWHERE before the camera-right fallback.
        const auto dUnlocked = fly({ Qt::Key_Right }, false);
        std::printf("    unlocked straight-down Right -> (%.3f %.3f %.3f)\n",
                    dUnlocked.first.x(), dUnlocked.first.y(), dUnlocked.first.z());
        CHECK(dUnlocked.first.x() > 1.0f,
              "free/UNLOCKED at the pole: Right still strafes (forward x worldUp degenerates "
              "there)");
    }

    // ---- 4. the arcball in a locked axis view ----------------------------
    {
        OrbitRig r(0.f, -90.f, true, true);
        printPose("orbit/top after the snap", r.cam);
        const iris::Vec3 axis = viewAxis(r.cam);
        const iris::Quat rot0 = r.cam->getLocalRot();
        const iris::Vec3 pos0 = r.cam->getLocalPos();
        CHECK(nearf(axis.y(), -1.f, 1e-3f), "orbit: the top view looks straight DOWN after the lerp");

        r.c.onMouseDown(Qt::RightButton);
        r.c.onMouseMove(45, 30);
        r.c.update(1.0f / 60.0f);
        printPose("orbit/top after RMB drag", r.cam);
        CHECK(sameRot(r.cam->getLocalRot(), rot0) && samePos(r.cam->getLocalPos(), pos0),
              "orbit/LOCKED: the arcball's own drag is ignored — pose untouched");
        r.c.onMouseUp(Qt::RightButton);

        r.c.setAltOrbit(true, iris::Vec3(0, 0, 0));
        r.c.onMouseDown(Qt::LeftButton);
        r.c.onMouseMove(30, 30);
        r.c.update(1.0f / 60.0f);
        CHECK(sameRot(r.cam->getLocalRot(), rot0),
              "orbit/LOCKED: Alt+LMB does not orbit either");
        r.c.onMouseUp(Qt::LeftButton);
        r.c.setAltOrbit(false, iris::Vec3());

        const iris::Vec3 before = r.cam->getLocalPos();
        r.c.onMouseDown(Qt::MiddleButton);
        r.c.onMouseMove(50, 30);
        printPose("orbit/top after MMB pan", r.cam);
        const iris::Vec3 after = r.cam->getLocalPos();
        const iris::Vec3 delta(after.x() - before.x(), after.y() - before.y(), after.z() - before.z());
        CHECK(sameRot(r.cam->getLocalRot(), rot0), "orbit/LOCKED: the pan does not rotate");
        CHECK(!samePos(after, before), "orbit/LOCKED: the pan moves the camera (with its pivot)");
        CHECK(nearf(outOfPlane(delta, axis), 0.f, 1e-4f),
              "orbit/LOCKED: …in the view plane");
        r.c.onMouseUp(Qt::MiddleButton);

        const float ortho0 = r.cam->orthoSize;
        const iris::Vec3 beforeWheel = r.cam->getLocalPos();
        r.c.onMouseWheel(120);
        printPose("orbit/top after wheel", r.cam);
        CHECK(r.cam->orthoSize < ortho0, "orbit/LOCKED: the wheel zooms the ortho view");
        CHECK(samePos(r.cam->getLocalPos(), beforeWheel) && sameRot(r.cam->getLocalRot(), rot0),
              "orbit/LOCKED: …and nothing else moves");
    }

    // ---- 4b. the two defects the lock's own suite found -------------------
    // Both are ARCBALL-ONLY and both were felt as "the camera jumped".
    {
        // (a) Alt+LMB in a locked view re-pointed the pivot at the selection
        // and re-derived the orbit radius from it. The drag itself did nothing
        // — and then the NEXT pan rebuilt the camera from the new pivot and
        // teleported it along the view axis.
        OrbitRig r(0.f, -90.f, true, true);
        const iris::Vec3 axis = viewAxis(r.cam);
        const iris::Vec3 pos0 = r.cam->getLocalPos();
        r.c.setAltOrbit(true, iris::Vec3(0, 0, 0));   // a selection somewhere else
        r.c.onMouseDown(Qt::LeftButton);
        r.c.onMouseMove(20, 20);
        r.c.onMouseUp(Qt::LeftButton);
        r.c.setAltOrbit(false, iris::Vec3());
        r.c.onMouseDown(Qt::MiddleButton);
        r.c.onMouseMove(50, 30);
        const iris::Vec3 after = r.cam->getLocalPos();
        const iris::Vec3 delta(after.x() - pos0.x(), after.y() - pos0.y(), after.z() - pos0.z());
        printPose("orbit/top alt-then-pan", r.cam);
        CHECK(nearf(outOfPlane(delta, axis), 0.f, 1e-4f),
              "orbit/LOCKED: an ignored Alt+LMB cannot move the camera through the NEXT pan");
        r.c.onMouseUp(Qt::MiddleButton);
    }
    {
        // (b) The ortho wheel ASSIGNED the orbit radius to orthoSize, so the
        // first notch snapped the zoom to the orbit distance — scrolling in
        // zoomed OUT once. Both controllers now step orthoSize by one unit per
        // notch, identically.
        FreeRig f(0.f, -90.f, true, true);
        OrbitRig o(0.f, -90.f, true, true);
        const float f0 = f.cam->orthoSize, o0 = o.cam->orthoSize;
        f.c.onMouseWheel(120);
        o.c.onMouseWheel(120);
        std::printf("    one notch in: free %.3f -> %.3f, orbit %.3f -> %.3f\n",
                    f0, f.cam->orthoSize, o0, o.cam->orthoSize);
        CHECK(nearf(f.cam->orthoSize, f0 - 1.0f) && nearf(o.cam->orthoSize, o0 - 1.0f),
              "one wheel notch zooms IN by one unit — in BOTH controllers, identically");
        f.c.onMouseWheel(-120);
        o.c.onMouseWheel(-120);
        CHECK(nearf(f.cam->orthoSize, f0) && nearf(o.cam->orthoSize, o0),
              "…and one notch back out returns to exactly where it started");
        for (int i = 0; i < 40; ++i) o.c.onMouseWheel(120);
        CHECK(o.cam->orthoSize >= 0.1f, "zooming past the floor stops at it rather than inverting");
    }

    // ---- 5. the arcball, NOT locked, and unlocking ------------------------
    {
        OrbitRig r(0.f, -90.f, true, false);
        const iris::Quat rot0 = r.cam->getLocalRot();
        r.c.onMouseDown(Qt::RightButton);
        r.c.onMouseMove(45, 30);
        r.c.update(1.0f / 60.0f);
        CHECK(!sameRot(r.cam->getLocalRot(), rot0), "orbit/UNLOCKED: the drag rotates");
        r.c.onMouseUp(Qt::RightButton);
    }
    {
        // The round trip the Views dropdown makes: lock in an axis view, then
        // back to perspective, and the camera turns again.
        FreeRig r(90.f, 0.f, true, true);          // left view
        const iris::Quat locked = r.cam->getLocalRot();
        r.c.onMouseDown(Qt::RightButton);
        r.c.onMouseMove(30, 10);
        CHECK(sameRot(r.cam->getLocalRot(), locked), "free/LEFT view: locked as well as top");
        r.c.setRotationLocked(false);
        r.c.onMouseMove(30, 10);
        CHECK(!sameRot(r.cam->getLocalRot(), locked),
              "unlocking (setView perspective) hands rotation straight back");
        r.c.onMouseUp(Qt::RightButton);
    }

    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
