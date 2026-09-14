// THE TRANSLATE GIZMO'S PLANE HANDLES (owner report 2026-09-15, ledger §346).
//
// THE OWNER'S WORDS: the translate gizmo "should have the three small PLANE
// handles in the corners between the arrows, each aligned to its plane like
// Unreal — it helps with the spatial connection for the user."
//
// There were none: translationgizmo.cpp's `planes` list was only the ray/plane
// hit-test the three ARROWS use to resolve a slide, and nothing was drawn or
// draggable for a plane. There are three now — XY, YZ, XZ — each a small square
// drawn IN its plane in its two axes' mixed colour, picked in PIXELS (the
// square has area, so "is the cursor on it" is a 2D question that stays well
// conditioned however the camera is turned) and dragged by the ray meeting that
// plane.
//
// WHAT IS ASSERTED HERE:
//
//   A. a pixel inside a plane square picks that plane, by name, and a press
//      there grabs it (the plane beats the arrow it is drawn beside);
//   B. the drag moves the node in EXACTLY the two axes the square spans — the
//      third coordinate does not move at all — and it tracks the cursor: the
//      node lands where the grabbed point of the plane lands;
//   C. a plane within 10 degrees of edge-on is neither drawn nor pickable
//      (it would be a line to aim at and a grazing intersection to drag);
//   D. the multi-select group delta applies, like any other handle;
//   E. the three arrows and the centre ball still answer beside them;
//   F. GIZMO-2 item 1 (owner §366/§368): the square's INNER CORNER is the
//      gizmo's origin and its sides run out along the two arrows — so the ball
//      keeps the origin (its pick sphere is 0.30 handle units, the square's
//      outer corner 0.71), the plane owns the rest of the square including the
//      arrows' inner stretch, and each arrow keeps everything beyond the span.
//
// Runs on the headless document graph: no display, no GPU, no pixels.

#include <QGuiApplication>
#include <QPointF>

#include <algorithm>
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
#include "viewport/gizmomeshes.h"
#include "viewport/translationgizmo.h"

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok:   %s\n", msg);                               \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

namespace {

constexpr float kWidth = 1920.0f;
constexpr float kHeight = 1080.0f;
constexpr float kHandleScale = 0.05f;   // TranslationHandle::handleScale

void rayFromPixel(const iris::CameraNodePtr &cam, const QPointF &px,
                  iris::Vec3 &rayPos, iris::Vec3 &rayDir)
{
    const iris::Mat4 invProj = cam->projMatrix.inverted();
    const iris::Mat4 invView = cam->viewMatrix.inverted();
    const float ndcX = (2.0f * float(px.x())) / kWidth - 1.0f;
    const float ndcY = -((2.0f * float(px.y())) / kHeight - 1.0f);
    const auto un = [&](float depth) {
        const iris::Vec4 eye = invProj * iris::Vec4(ndcX, ndcY, depth, 1.0f);
        const iris::Vec4 world = invView * eye;
        return world.toVector3D() / world.w();
    };
    rayPos = un(-1.0f);
    rayDir = (un(1.0f) - rayPos).normalized();
}

void place(const iris::CameraNodePtr &cam, const iris::Vec3 &eye)
{
    cam->setLocalPos(eye);
    cam->lookAt(iris::Vec3(0, 0, 0));
    cam->update(0.0f);
}

struct Plane { const char *name; iris::Vec3 u, v, normal; };
const Plane kPlanes[3] = {
    { "xy", iris::Vec3(1, 0, 0), iris::Vec3(0, 1, 0), iris::Vec3(0, 0, 1) },
    { "yz", iris::Vec3(0, 1, 0), iris::Vec3(0, 0, 1), iris::Vec3(1, 0, 0) },
    { "xz", iris::Vec3(1, 0, 0), iris::Vec3(0, 0, 1), iris::Vec3(0, 1, 0) },
};

/// A point WELL INSIDE a plane handle's square, in world units, at the gizmo's
/// scale. The square runs [0, kPlaneHandleSpan] on both axes since GIZMO-2
/// item 1 — its inner corner IS the gizmo's origin — so the inner part of it
/// belongs to the centre ball, whose pick sphere is CENTER_CIRCLE_RADIUS
/// (0.015) of gizmoScale = 0.30 of these handle units, three times the ball's
/// drawn 0.10 radius. 0.70 of the span on each axis is 0.495 handle units from
/// the origin, comfortably outside it (the crossover is asserted below).
constexpr float kGrabFraction = 0.70f;
iris::Vec3 squareGrab(const Plane &p, float scale)
{
    return (p.u + p.v) * (kGrabFraction * GizmoMeshes::kPlaneHandleSpan) * scale;
}

}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("gizmo-planes-ogre.log");
    if (!graph.ok()) { std::printf("FAIL: headless engine: %s\n", graph.error().c_str()); return 1; }

    auto doc = iris::Scene::create();
    auto node = iris::SceneNode::create();
    doc->getRootNode()->addChild(node);

    auto cam = iris::CameraNode::create();
    cam->nearClip = 0.05f;
    cam->farClip = 2000.0f;
    cam->angle = 45.0f;
    cam->setFramingAspect(freecam::kFreeCameraFramingAspect);
    doc->getRootNode()->addChild(cam);
    place(cam, iris::Vec3(6, 6, 6));               // iso: all three squares face the camera

    // ---- A + B: pick it, drag it, and only those two axes move ------------
    for (const Plane &plane : kPlanes) {
        node->setLocalPos(iris::Vec3(0, 0, 0));
        node->update(0.0f);

        TranslationGizmo gizmo;
        gizmo.setSelectedNode(node);
        gizmo.updateSize(cam);
        gizmo.setPickView(cam, kWidth, kHeight);
        const float scale = gizmo.getGizmoScale() * kHandleScale;

        QPointF grabPx;
        if (!gizmo.projectToPixel(squareGrab(plane, scale), grabPx)) {
            std::printf("FAIL: %s square does not project\n", plane.name); ++failures; continue;
        }
        float d = -1.0f;
        const QString named = gizmo.planeNameAtPixel(grabPx, d);
        if (named != QLatin1String(plane.name)) {
            std::printf("FAIL: the middle of the %s square picks '%s' at %.2f px\n",
                        plane.name, qPrintable(named), double(d)); ++failures; continue;
        }

        const iris::Vec3 forward =
            cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();
        iris::Vec3 rayPos, rayDir;
        rayFromPixel(cam, grabPx, rayPos, rayDir);
        // The press goes through the SAME call the viewport makes.
        iris::Vec3 hitOut;
        auto *grabbed = gizmo.getHitHandle(rayPos, rayDir, forward, hitOut);
        const bool grabbedPlane = grabbed && grabbed->axisName() == QLatin1String(plane.name);
        gizmo.startDragging(rayPos, rayDir, forward);

        // Drag to a chosen point IN the plane: the node must land exactly the
        // in-plane displacement away, and not move on the third axis at all.
        const iris::Vec3 target = plane.u * 2.5f + plane.v * (-1.75f);
        QPointF targetPx;
        if (!gizmo.projectToPixel(target, targetPx)) {
            std::printf("FAIL: %s drag target does not project\n", plane.name); ++failures; continue;
        }
        rayFromPixel(cam, targetPx, rayPos, rayDir);
        gizmo.drag(rayPos, rayDir, forward);
        node->update(0.0f);
        const iris::Vec3 moved = node->getGlobalPosition();
        const iris::Vec3 expect = target - squareGrab(plane, scale);
        const float offAxis = std::fabs(iris::Vec3::dotProduct(moved, plane.normal));

        std::printf("   %s: grabbed '%s' at %.2f px; the node moved to (%.3f, %.3f, %.3f), "
                    "the cursor's plane point predicts (%.3f, %.3f, %.3f); off-plane %.6f\n",
                    plane.name, grabbed ? qPrintable(grabbed->axisName()) : "(none)", double(d),
                    double(moved.x()), double(moved.y()), double(moved.z()),
                    double(expect.x()), double(expect.y()), double(expect.z()), double(offAxis));

        CHECK(grabbedPlane, "A: a press in the middle of the square grabs that plane handle "
                            "(it wins over the arrows it is drawn between)");
        CHECK(offAxis < 1e-4f, "B: the drag moves the node in EXACTLY the square's two axes — "
                               "the third does not move at all");
        CHECK(moved.distanceToPoint(expect) < 1e-3f,
              "B: and it tracks the cursor: the grabbed point of the plane lands under it");
        gizmo.endDragging();
    }

    // ---- C: edge-on is not a handle ---------------------------------------
    {
        // Looking down +Y puts the XZ plane FACE-ON and the other two EDGE-ON.
        place(cam, iris::Vec3(0.0f, 9.0f, 0.0f));
        node->setLocalPos(iris::Vec3(0, 0, 0));
        node->update(0.0f);

        TranslationGizmo gizmo;
        gizmo.setSelectedNode(node);
        gizmo.updateSize(cam);
        gizmo.setPickView(cam, kWidth, kHeight);
        const float scale = gizmo.getGizmoScale() * kHandleScale;
        const iris::Vec3 forward =
            cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();
        iris::Vec3 rayPos, rayDir;
        rayFromPixel(cam, QPointF(kWidth / 2, kHeight / 2), rayPos, rayDir);
        const QVector<GizmoDrawItem> items = gizmo.drawItems(rayPos, rayDir, forward);

        int hidden = 0, shown = 0;
        for (const Plane &plane : kPlanes) {
            QPointF px;
            if (!gizmo.projectToPixel(squareGrab(plane, scale), px)) continue;
            float d = -1.0f;
            const bool picks = gizmo.planeNameAtPixel(px, d) == QLatin1String(plane.name);
            const bool edgeOn = std::fabs(iris::Vec3::dotProduct(plane.normal, forward)) <
                                std::sin(float(qDegreesToRadians(kPlaneEdgeOnDegrees)));
            std::printf("   top view: %s plane %s, and it %s\n", plane.name,
                        edgeOn ? "is EDGE-ON" : "faces the camera",
                        picks ? "picks" : "does not pick");
            if (edgeOn) { ++hidden; if (picks) { ++failures; std::printf("FAIL: an edge-on plane picked\n"); } }
            else { ++shown; if (!picks) { ++failures; std::printf("FAIL: a face-on plane did not pick\n"); } }
        }
        CHECK(hidden == 2 && shown == 1, "C: from straight above, two planes are edge-on and one "
                                         "faces the camera");
        // 4 arrows/ball + the one plane that is drawn — the two edge-on ones
        // are left out of the picture as well as out of the pick.
        std::printf("   top view: %d draw items (7 handles, 2 planes withheld)\n", items.size());
        CHECK(items.size() == 5, "C: and the two edge-on squares are not drawn either");
    }

    // ---- D + E: the group follows, and the old handles still answer -------
    {
        place(cam, iris::Vec3(6, 6, 6));
        node->setLocalPos(iris::Vec3(0, 0, 0));
        node->update(0.0f);
        auto second = iris::SceneNode::create();
        doc->getRootNode()->addChild(second);
        second->setLocalPos(iris::Vec3(0, 0, 4));
        second->update(0.0f);

        TranslationGizmo gizmo;
        gizmo.setSelectedNode(node);
        gizmo.setGroup({ node, second });
        gizmo.updateSize(cam);
        gizmo.setPickView(cam, kWidth, kHeight);
        const float scale = gizmo.getGizmoScale() * kHandleScale;
        const iris::Vec3 forward =
            cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();

        QPointF grabPx, targetPx;
        const iris::Vec3 target = kPlanes[2].u * 2.0f + kPlanes[2].v * 1.0f;   // xz
        const bool have = gizmo.projectToPixel(squareGrab(kPlanes[2], scale), grabPx) &&
                          gizmo.projectToPixel(target, targetPx);
        CHECK(have, "D: the xz square and its drag target project");
        if (have) {
            const iris::Vec3 secondStart = second->getGlobalPosition();
            iris::Vec3 rayPos, rayDir;
            rayFromPixel(cam, grabPx, rayPos, rayDir);
            gizmo.startDragging(rayPos, rayDir, forward);
            rayFromPixel(cam, targetPx, rayPos, rayDir);
            gizmo.drag(rayPos, rayDir, forward);
            node->update(0.0f); second->update(0.0f);
            const iris::Vec3 delta = node->getGlobalPosition();
            const iris::Vec3 secondMoved = second->getGlobalPosition();
            std::printf("   group: the primary moved (%.3f, %.3f, %.3f); the second went from "
                        "(%.1f, %.1f, %.1f) to (%.3f, %.3f, %.3f)\n",
                        double(delta.x()), double(delta.y()), double(delta.z()),
                        double(secondStart.x()), double(secondStart.y()), double(secondStart.z()),
                        double(secondMoved.x()), double(secondMoved.y()), double(secondMoved.z()));
            CHECK(delta.length() > 1.0f, "D: the primary moved across the plane");
            CHECK(secondMoved.distanceToPoint(secondStart + delta) < 1e-3f,
                  "D: and the rest of the selection took exactly the same step");
            gizmo.endDragging();
        }

        // E: the arrows and the ball are still there, and still picked in 3D.
        node->setLocalPos(iris::Vec3(0, 0, 0));
        node->update(0.0f);
        gizmo.setGroup({});
        gizmo.updateSize(cam);
        gizmo.setPickView(cam, kWidth, kHeight);
        QPointF centrePx;
        gizmo.projectToPixel(iris::Vec3(0, 0, 0), centrePx);
        iris::Vec3 rayPos, rayDir, hit;
        rayFromPixel(cam, centrePx, rayPos, rayDir);
        auto *ball = gizmo.getHitHandle(rayPos, rayDir, forward, hit);
        CHECK(ball && ball->axisName() == QLatin1String("center"),
              "E: the centre ball still takes precedence over everything");
        int arrowsFound = 0;
        for (const char *axis : { "x", "y", "z" }) {
            const iris::Vec3 dir(axis[0] == 'x' ? 1.0f : 0.0f, axis[0] == 'y' ? 1.0f : 0.0f,
                                 axis[0] == 'z' ? 1.0f : 0.0f);
            QPointF px;
            // Out at the arrow's tip, well beyond the plane squares' 1.05.
            if (!gizmo.projectToPixel(dir * 1.7f * gizmo.getGizmoScale() * kHandleScale, px)) continue;
            rayFromPixel(cam, px, rayPos, rayDir);
            auto *h = gizmo.getHitHandle(rayPos, rayDir, forward, hit);
            if (h && h->axisName() == QLatin1String(axis)) ++arrowsFound;
        }
        CHECK(arrowsFound == 3, "E: and all three arrows are still grabbable at their tips");
    }

    // ---- F: the corner-at-the-centre square (GIZMO-2 item 1) --------------
    //
    // The numbers, in HANDLE-LOCAL units (multiply by handleScale * gizmoScale
    // for world units): the square spans [0, 0.50] on both axes, so its outer
    // corner is 0.707 out; the centre ball is DRAWN at radius 0.10 and PICKED
    // at CENTER_CIRCLE_RADIUS / handleScale = 0.015 / 0.05 = 0.30; the arrow
    // runs to 1.90. This walks the xz square's diagonal from the origin out
    // past the arrow's tip and prints who answers where.
    {
        place(cam, iris::Vec3(6, 6, 6));
        node->setLocalPos(iris::Vec3(0, 0, 0));
        node->update(0.0f);

        TranslationGizmo gizmo;
        gizmo.setSelectedNode(node);
        gizmo.updateSize(cam);
        gizmo.setPickView(cam, kWidth, kHeight);
        const float scale = gizmo.getGizmoScale() * kHandleScale;
        const iris::Vec3 forward =
            cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();
        const float ballPick = 0.015f / kHandleScale;            // CENTER_CIRCLE_RADIUS
        const float span = GizmoMeshes::kPlaneHandleSpan;

        auto handleAt = [&](const iris::Vec3 &local) {
            QPointF px;
            if (!gizmo.projectToPixel(local * scale, px)) return QString();
            iris::Vec3 rayPos, rayDir, hit;
            rayFromPixel(cam, px, rayPos, rayDir);
            auto *h = gizmo.getHitHandle(rayPos, rayDir, forward, hit);
            return h ? h->axisName() : QString();
        };

        // Along the xz square's diagonal (u = v = t), t in handle units.
        const iris::Vec3 diag = (kPlanes[2].u + kPlanes[2].v).normalized();
        std::printf("   the square's diagonal (ball picks to %.2f, square's outer corner at "
                    "%.3f):\n", double(ballPick), double(span * std::sqrt(2.0f)));
        for (float t : { 0.0f, 0.15f, 0.25f, 0.40f, 0.50f, 0.65f, 0.70f }) {
            const QString who = handleAt(diag * t);
            std::printf("      %.2f out: '%s'\n", double(t), qPrintable(who));
        }
        CHECK(handleAt(iris::Vec3(0, 0, 0)) == QLatin1String("center"),
              "F: the frame's inner corner is the origin, and the origin is the BALL "
              "(a click on the white ball is the ball)");
        CHECK(handleAt(diag * (0.5f * (ballPick + span * std::sqrt(2.0f)))) ==
                  QLatin1String("xz"),
              "F: past the ball's sphere, inside the square, the plane answers");
        // The arrows: inside the span the square owns the pixel, beyond it the
        // arrow does. (0.25 is inside the square AND on the X shaft; 1.20 is
        // past the square's 0.50 and well short of the tip.)
        std::printf("   on the +X shaft: %.2f out -> '%s'; %.2f out -> '%s'\n", 0.25,
                    qPrintable(handleAt(iris::Vec3(0.25f, 0, 0))), 1.20,
                    qPrintable(handleAt(iris::Vec3(1.20f, 0, 0))));
        CHECK(handleAt(iris::Vec3(1.20f, 0, 0)) == QLatin1String("x"),
              "F: and the arrow owns everything beyond the square's span (74 % of it)");

        // HOW MUCH OF EACH SQUARE IS ACTUALLY THE PLANE. The ball's sphere eats
        // the inner part of it, and how much depends on how the square is
        // turned to the camera (the ground square from a 3/4 view is the worst:
        // its diagonal runs towards the eye). Sampled over the square's own
        // (u, v) in a 17x17 grid, as a number rather than an opinion.
        for (const Plane &plane : kPlanes) {
            int inPlane = 0, inBall = 0, other = 0;
            for (int iu = 0; iu <= 16; ++iu)
                for (int iv = 0; iv <= 16; ++iv) {
                    const float u = float(iu) / 16.0f * span, v = float(iv) / 16.0f * span;
                    const QString who = handleAt(plane.u * u + plane.v * v);
                    if (who == QLatin1String(plane.name)) ++inPlane;
                    else if (who == QLatin1String("center")) ++inBall;
                    else ++other;
                }
            std::printf("   %s square: %d%% of it picks the plane, %d%% the centre ball, "
                        "%d%% an arrow\n", plane.name, inPlane * 100 / 289, inBall * 100 / 289,
                        other * 100 / 289);
            CHECK(inPlane * 2 > 289, "F: over half of every square's area still grabs the plane "
                                     "(the centre ball keeps the rest)");
        }
    }

    std::printf("\n%s\n", failures == 0 ? "PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
