/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// input.alt_orbit — THE ALT+DRAG ORBIT DOES NOT JUMP (owner report 2026-09-15,
// ledger §353).
//
// THE OWNER'S WORDS: "Alt+click to rotate the viewport should not refocus but
// rotate around the point of the empty Alt+click; it seems to jump the viewport
// like an F focus on the point — hard to navigate."
//
// THE DEFECT, in both controllers. The orbit RE-PLACED the camera on a sphere
// around the pivot at the controller's own yaw/pitch —
//
//     pos = pivot + fromEulerAngles(pitch, yaw, 0) · (0, 0, 1) · distance
//
// (editorcameracontroller.cpp:144-149, and the arcball's applyPose) — so unless
// the camera ALREADY looked straight at the pivot, the first frame of the drag
// moved it to centre the pivot. That is the jump, and it happened before the
// user had dragged anything.
//
// WHAT IT IS NOW. A rotation, not a placement: the camera's CURRENT offset from
// the pivot is turned by the drag's delta, and its orientation by the same
// delta — offset' = R·(pos − pivot), rot' = R·rot. Then
//
//   A. a zero-length drag writes NOTHING: the pose is bit-identical, from a
//      camera that is not looking at the pivot at all;
//   B. the pivot stays at exactly the same PIXEL through a real drag (its
//      position in camera space is invariant under the construction), which is
//      the acceptance: the point you grabbed stays under the cursor;
//   C. the orbit is a rotation ABOUT the pivot: the distance to it is preserved
//      and the camera really turns;
//   D. ROLL survives, where the yaw/pitch rebuild levelled it;
//   E. the pole guard still holds (a huge pitch drag does not turn the camera
//      over);
//   F. the arcball ends the drag in a state CONSISTENT with the pose the orbit
//      left — its pivot is back on its own view ray at the orbited distance —
//      so the next pan or dolly cannot teleport the camera.
//
// Drives the controllers at the gesture level, exactly as EngineSceneViewport's
// mouse handlers do. No display, no engine view, no GPU.

#include <QGuiApplication>
#include <cmath>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "../support/documentgraph.h"
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "viewport/editorcameracontroller.h"
#include "viewport/orbitalcameracontroller.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } \
} while (0)

namespace {

constexpr float kWidth = 1920.0f;
constexpr float kHeight = 1080.0f;

bool sameRotExact(const iris::Quat &a, const iris::Quat &b)
{
    return a.x() == b.x() && a.y() == b.y() && a.z() == b.z() && a.scalar() == b.scalar();
}
bool samePosExact(const iris::Vec3 &a, const iris::Vec3 &b)
{
    return a.x() == b.x() && a.y() == b.y() && a.z() == b.z();
}

/// Where a world point lands on screen, through the camera's own matrices —
/// the same projection Gizmo::projectToPixel uses.
bool pixelOf(const iris::CameraNodePtr &cam, const iris::Vec3 &world, QPointF &px)
{
    cam->setAspectRatio(kWidth / kHeight);
    cam->updateCameraMatrices();
    const iris::Vec4 clip = (cam->projMatrix * cam->viewMatrix) *
                            iris::Vec4(world.x(), world.y(), world.z(), 1.0f);
    if (clip.w() <= 1e-6f) return false;
    px = QPointF(double((clip.x() / clip.w() * 0.5f + 0.5f) * kWidth),
                 double((1.0f - (clip.y() / clip.w() * 0.5f + 0.5f)) * kHeight));
    return true;
}

/// A camera that is deliberately NOT looking at the pivot: the whole point of
/// the report is what happens when the two disagree.
iris::CameraNodePtr offAxisCamera()
{
    auto cam = iris::CameraNode::create();
    cam->nearClip = 0.05f;
    cam->farClip = 2000.0f;
    cam->angle = 45.0f;
    cam->setLocalPos(iris::Vec3(4, 6, 14));
    cam->setLocalRot(iris::Quat::fromEulerAngles(-12.0f, 10.0f, 0.0f));
    cam->update(0.0f);
    return cam;
}

void printPose(const char *what, const iris::CameraNodePtr &cam)
{
    const iris::Vec3 p = cam->getLocalPos();
    const iris::Quat r = cam->getLocalRot();
    std::printf("    %-26s pos (%7.3f %7.3f %7.3f)  rot (%6.3f %6.3f %6.3f %6.3f)\n",
                what, p.x(), p.y(), p.z(), r.x(), r.y(), r.z(), r.scalar());
}

/// The pivot an empty Alt+click lands on: a point well off the camera's own
/// view centre, so a "re-place on the sphere" orbit would be visible at once.
const iris::Vec3 kPivot(-3.0f, 1.0f, 2.0f);

}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("alt-orbit-ogre.log");
    if (!graph.ok()) { std::printf("FAIL: headless engine: %s\n", graph.error().c_str()); return 1; }

    // ---- THE FREE (FLY) CAMERA -------------------------------------------
    {
        std::printf("\n== free camera ==\n");
        EditorCameraController c(nullptr);
        auto cam = offAxisCamera();
        c.setCamera(cam);

        // A: the press, and a drag of NOTHING.
        const iris::Vec3 pos0 = cam->getLocalPos();
        const iris::Quat rot0 = cam->getLocalRot();
        c.setAltOrbit(true, kPivot);
        c.onMouseDown(Qt::LeftButton);
        c.onMouseMove(0, 0);
        printPose("free, zero-length drag", cam);
        CHECK(samePosExact(cam->getLocalPos(), pos0) && sameRotExact(cam->getLocalRot(), rot0),
              "A: an Alt+press and a zero-length drag leave the pose BIT-IDENTICAL (the jump "
              "the owner reported happened here, before any movement)");

        // B + C: a real drag.
        QPointF pivotPxBefore, pivotPxAfter;
        const bool projected = pixelOf(cam, kPivot, pivotPxBefore);
        const float distBefore = cam->getLocalPos().distanceToPoint(kPivot);
        for (int i = 0; i < 8; ++i) c.onMouseMove(-5, -3);      // 40 by 24 degrees
        printPose("free, after 40x24 deg", cam);
        const float distAfter = cam->getLocalPos().distanceToPoint(kPivot);
        const bool projected2 = pixelOf(cam, kPivot, pivotPxAfter);
        std::printf("    the pivot was at pixel (%.3f, %.3f) and is at (%.3f, %.3f); "
                    "radius %.4f -> %.4f\n", pivotPxBefore.x(), pivotPxBefore.y(),
                    pivotPxAfter.x(), pivotPxAfter.y(), double(distBefore), double(distAfter));
        CHECK(projected && projected2, "B: the pivot is on screen before and after");
        CHECK(std::hypot(pivotPxAfter.x() - pivotPxBefore.x(),
                         pivotPxAfter.y() - pivotPxBefore.y()) < 1.0f,
              "B: and it is at the SAME PIXEL — the point you grabbed stays under the cursor");
        CHECK(std::fabs(distAfter - distBefore) < 1e-3f,
              "C: the orbit is a rotation ABOUT the pivot — the radius is unchanged");
        CHECK(!sameRotExact(cam->getLocalRot(), rot0) && !samePosExact(cam->getLocalPos(), pos0),
              "C: and the camera really turned and moved (this is not a suite that passes on a "
              "controller which does nothing)");
        c.onMouseUp(Qt::LeftButton);
        c.setAltOrbit(false, iris::Vec3());
    }

    // ---- D: roll survives -------------------------------------------------
    {
        EditorCameraController c(nullptr);
        auto cam = offAxisCamera();
        // A rolled camera: a socketed or authored one, the case the "decompose
        // only, never write" contract exists for (cameracontrollerbase.h).
        cam->setLocalRot(iris::Quat::fromEulerAngles(-12.0f, 10.0f, 25.0f));
        cam->update(0.0f);
        c.setCamera(cam);
        const iris::Vec3 up0 = cam->getLocalRot().rotatedVector(iris::Vec3(0, 1, 0));
        c.setAltOrbit(true, kPivot);
        c.onMouseDown(Qt::LeftButton);
        for (int i = 0; i < 4; ++i) c.onMouseMove(-6, 0);
        const iris::Quat r = cam->getLocalRot();
        float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
        r.getEulerAngles(&pitch, &yaw, &roll);
        printPose("free, rolled camera", cam);
        std::printf("    roll %.3f degrees (was 25.000)\n", double(roll));
        CHECK(std::fabs(std::fabs(roll) - 25.0f) < 0.5f,
              "D: a rolled camera keeps its roll through the orbit (the yaw/pitch rebuild "
              "levelled it)");
        (void)up0;
        c.onMouseUp(Qt::LeftButton);
        c.setAltOrbit(false, iris::Vec3());
    }

    // ---- E: the pole guard -----------------------------------------------
    {
        EditorCameraController c(nullptr);
        auto cam = offAxisCamera();
        c.setCamera(cam);
        c.setAltOrbit(true, kPivot);
        c.onMouseDown(Qt::LeftButton);
        for (int i = 0; i < 40; ++i) c.onMouseMove(0, -20);     // 800 degrees of pitch
        const iris::Vec3 fwd = cam->getLocalRot().rotatedVector(iris::Vec3(0, 0, -1)).normalized();
        const iris::Vec3 up = cam->getLocalRot().rotatedVector(iris::Vec3(0, 1, 0)).normalized();
        printPose("free, 800 deg of pitch", cam);
        std::printf("    forward y %.4f, up y %.4f\n", double(fwd.y()), double(up.y()));
        CHECK(up.y() > 0.0f, "E: the pole guard holds — 800 degrees of pitch drag does not turn "
                             "the camera upside down");
        // ...and the guard clamps the ACCUMULATOR, not just the picture: the
        // first steps BACK must move the camera at once (a derived-value clamp
        // let the accumulator wind up past the pole, and the reverse drag did
        // nothing until the overshoot unwound — second reader, GIZMO-1).
        const iris::Vec3 atPole = cam->getLocalPos();
        for (int i = 0; i < 4; ++i) c.onMouseMove(0, 20);
        const float movedBack = cam->getLocalPos().distanceToPoint(atPole);
        std::printf("    four steps back from the pole moved %.4f\n", double(movedBack));
        CHECK(movedBack > 0.01f, "E: the reverse drag answers immediately at the pole (no wind-up)");
        c.onMouseUp(Qt::LeftButton);
        c.setAltOrbit(false, iris::Vec3());
    }

    // ---- THE ARCBALL ------------------------------------------------------
    {
        std::printf("\n== arcball ==\n");
        OrbitalCameraController c(nullptr);
        auto cam = offAxisCamera();
        c.setCamera(cam);

        const iris::Vec3 pos0 = cam->getLocalPos();
        const iris::Quat rot0 = cam->getLocalRot();
        c.setAltOrbit(true, kPivot);
        c.onMouseDown(Qt::LeftButton);
        c.onMouseMove(0, 0);
        c.update(1.0f / 60.0f);
        printPose("arcball, zero-length drag", cam);
        CHECK(samePosExact(cam->getLocalPos(), pos0) && sameRotExact(cam->getLocalRot(), rot0),
              "A: the arcball's Alt+drag leaves the pose BIT-IDENTICAL too, before any movement "
              "(it used to re-place the camera to centre the pivot)");

        QPointF before, after;
        const bool p1 = pixelOf(cam, kPivot, before);
        const float distBefore = cam->getLocalPos().distanceToPoint(kPivot);
        for (int i = 0; i < 8; ++i) { c.onMouseMove(-5, -3); c.update(1.0f / 60.0f); }
        const bool p2 = pixelOf(cam, kPivot, after);
        const float distAfter = cam->getLocalPos().distanceToPoint(kPivot);
        printPose("arcball, after 40x24 deg", cam);
        std::printf("    the pivot was at pixel (%.3f, %.3f) and is at (%.3f, %.3f); "
                    "radius %.4f -> %.4f\n", before.x(), before.y(), after.x(), after.y(),
                    double(distBefore), double(distAfter));
        CHECK(p1 && p2 && std::hypot(after.x() - before.x(), after.y() - before.y()) < 1.0f,
              "B: the picked point stays at the same pixel for the arcball as well");
        CHECK(std::fabs(distAfter - distBefore) < 1e-3f, "C: and the radius is preserved");

        // F: the drag ends, and the arcball's own state has to be consistent
        // with the pose the orbit left — otherwise the next pan or dolly
        // teleports the camera (the 2026-09-08 defect, in a new place).
        c.onMouseUp(Qt::LeftButton);
        c.setAltOrbit(false, iris::Vec3());
        const iris::Vec3 posAfterRelease = cam->getLocalPos();
        const iris::Quat rotAfterRelease = cam->getLocalRot();
        CHECK(samePosExact(posAfterRelease, cam->getLocalPos()) &&
              sameRotExact(rotAfterRelease, cam->getLocalRot()),
              "F: releasing the drag writes nothing (the state is re-derived by a pure read)");
        c.onMouseWheel(120);                       // a dolly, straight into applyPose
        const iris::Vec3 dollied = cam->getLocalPos();
        const iris::Vec3 fwd =
            rotAfterRelease.rotatedVector(iris::Vec3(0, 0, -1)).normalized();
        const iris::Vec3 moved = dollied - posAfterRelease;
        const float alongView = iris::Vec3::dotProduct(moved, fwd);
        const float sideways = (moved - fwd * alongView).length();
        printPose("arcball, after a dolly", cam);
        std::printf("    the dolly moved %.4f along the view and %.6f sideways\n",
                    double(alongView), double(sideways));
        CHECK(sameRotExact(cam->getLocalRot(), rotAfterRelease) || sideways < 1e-3f,
              "F: the dolly after the orbit runs along the view direction — the arcball's state "
              "matches the pose the orbit left, so nothing teleports");
        CHECK(sideways < 1e-3f, "F: …with no sideways component at all");
    }

    std::printf("\n%s\n", failures == 0 ? "PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
