// THE ROTATION GIZMO'S FRAME IS FROZEN FOR THE LENGTH OF A DRAG (owner report
// 2026-09-15, ledger §345).
//
// THE OWNER'S WORDS: "they seem to move on their own with the asset, I don't
// think that's the normal way."
//
// THE DEFECT. The default transform space is LOCAL, and the gizmo was DRAWN
// from the node's CURRENT rotation on every frame (drawItems called
// Gizmo::getTransform() directly, and both pick entry points re-read it on
// every mouse move). So a drag on the X ring turned the object about its local
// X and the ring turned with it: the ring ran out from under the cursor while
// the VALUE stayed right — the drag maths has always differenced against the
// frame captured at startDragging. Unreal and Blender freeze the picture for
// the length of the drag and re-orient at release.
//
// WHAT IS ASSERTED HERE, in both spaces:
//
//   A. the frame the gizmo draws and picks with is BIT-IDENTICAL, all sixteen
//      floats, from the press to the release, however far the node turns;
//   B. the ring stays under the cursor: the grab pixel is still within a
//      fraction of a pixel of the X ring's projected circle at every step of
//      the drag, and ringNameAtPixel still answers "x" there;
//   C. the drawn item agrees with the picked frame (there is no second path
//      that could read the live node);
//   D. the VALUE is still live — the node really turns, by the angle dragged;
//   E. the defect, as a number: the same measurement against the frame the
//      gizmo USED to draw (the node's live rotation) runs away from the grab
//      pixel by many times the pick tolerance in Local space;
//   F. release re-orients: after endDragging the frame is the node's again.
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
#include "viewport/rotationgizmo.h"

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok:   %s\n", msg);                               \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

namespace {

constexpr float kWidth = 1920.0f;      // the rig's display, and the shipped default
constexpr float kHeight = 1080.0f;
constexpr float kHandleScale = 0.08f;  // RotationHandle::handleScale

/// The pick ray a click at `px` casts — ScenePicker::screenSegment's own
/// unprojection, inlined so this stays a document-only suite.
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

/// The three rings as gizmomeshes::rotationRing builds them: the unit circle in
/// the plane spanned by (u, v), whose normal is the axis.
struct Ring { const char *name; iris::Vec3 u, v; };
const Ring kRings[3] = {
    { "x", iris::Vec3(0, 1, 0), iris::Vec3(0, 0, 1) },
    { "y", iris::Vec3(0, 0, 1), iris::Vec3(1, 0, 0) },
    { "z", iris::Vec3(1, 0, 0), iris::Vec3(0, 1, 0) },
};

/// A point of a ring, in world space, under an arbitrary gizmo frame — exactly
/// what screenDistance measures and what drawItems draws.
iris::Vec3 ringPoint(const iris::Mat4 &frame, float scale, float degrees, const Ring &ring)
{
    const float a = float(degrees * M_PI / 180.0);
    const auto dir = [&frame](const iris::Vec3 &d) {
        return (frame * iris::Vec4(d, 0)).toVector3D().normalized();
    };
    const iris::Vec3 centre = frame.column(3).toVector3D();
    return centre + (dir(ring.u) * std::cos(a) + dir(ring.v) * std::sin(a)) * scale;
}

/// How far `cursor` is, in pixels, from the X ring drawn under `frame` —
/// RotationHandle::screenDistance's measurement, against a frame of our choosing
/// so the FROZEN one and the LIVE one can be compared on the same cursor.
float ringDistancePx(const RotationGizmo &gizmo, const iris::Mat4 &frame, float scale,
                     const QPointF &cursor, const Ring &ring = kRings[0])
{
    float best = -1.0f;
    QPointF prev, first;
    bool havePrev = false, haveFirst = false;
    for (int i = 0; i < 96; ++i) {
        QPointF px;
        if (!gizmo.projectToPixel(ringPoint(frame, scale, float(360.0 * i / 96), ring), px)) {
            havePrev = false; continue;
        }
        if (!haveFirst) { first = px; haveFirst = true; }
        if (havePrev) {
            const double vx = px.x() - prev.x(), vy = px.y() - prev.y();
            const double wx = cursor.x() - prev.x(), wy = cursor.y() - prev.y();
            const double len2 = vx * vx + vy * vy;
            double t = len2 > 1e-12 ? (wx * vx + wy * vy) / len2 : 0.0;
            t = std::min(1.0, std::max(0.0, t));
            const double dx = wx - t * vx, dy = wy - t * vy;
            const float d = float(std::sqrt(dx * dx + dy * dy));
            if (best < 0.0f || d < best) best = d;
        } else {
            const double dx = cursor.x() - px.x(), dy = cursor.y() - px.y();
            const float d = float(std::sqrt(dx * dx + dy * dy));
            if (best < 0.0f || d < best) best = d;
        }
        prev = px; havePrev = true;
    }
    (void)first; (void)haveFirst;
    return best;
}

/// Gizmo::getTransform()'s answer for a node, written out here so the test can
/// build the LIVE frame the gizmo used to draw from.
iris::Mat4 liveFrame(const iris::SceneNodePtr &node, GizmoTransformSpace space)
{
    iris::Mat4 t;
    t.setToIdentity();
    t.translate(node->getGlobalPosition());
    if (space == GizmoTransformSpace::Local) t.rotate(node->getGlobalRotation().normalized());
    return t;
}

bool sameMatrix(const iris::Mat4 &a, const iris::Mat4 &b)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (a.column(c)[r] != b.column(c)[r]) return false;
    return true;
}

void place(const iris::CameraNodePtr &cam, float pitch, float yaw, float distance)
{
    const iris::Quat rot = iris::Quat::fromEulerAngles(pitch, yaw, 0.0f);
    cam->setLocalRot(rot);
    cam->setLocalPos(rot.rotatedVector(iris::Vec3(0, 0, 1)) * distance);
    cam->update(0.0f);
}

}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("gizmo-drag-frame-ogre.log");
    if (!graph.ok()) { std::printf("FAIL: headless engine: %s\n", graph.error().c_str()); return 1; }

    auto doc = iris::Scene::create();
    auto node = iris::SceneNode::create();
    doc->getRootNode()->addChild(node);            // at the origin

    auto cam = iris::CameraNode::create();
    cam->nearClip = 0.05f;
    cam->farClip = 2000.0f;
    cam->angle = 45.0f;
    cam->setFramingAspect(freecam::kFreeCameraFramingAspect);
    doc->getRootNode()->addChild(cam);
    place(cam, -35.0f, 45.0f, 9.0f);               // iso: all three rings readable

    struct Space { const char *name; GizmoTransformSpace value; };
    const Space kSpaces[] = {
        { "Local",  GizmoTransformSpace::Local  },
        { "Global", GizmoTransformSpace::Global },
    };

    for (const Space &space : kSpaces) {
        std::printf("\n== %s space ==\n", space.name);
        node->setLocalRot(iris::Quat());
        node->update(0.0f);

        RotationGizmo gizmo;
        gizmo.setSelectedNode(node);
        gizmo.setTransformSpace(space.value);
        gizmo.updateSize(cam);
        gizmo.setPickView(cam, kWidth, kHeight);

        const float scale = gizmo.getGizmoScale() * kHandleScale;

        // A pixel that really is the X ring's (a crossing would hand the drag
        // to a neighbour — correct behaviour, but not what is measured here).
        float startDeg = -1.0f;
        QPointF grabPx;
        for (int i = 0; i < 96 && startDeg < 0.0f; ++i) {
            const float deg = float(360.0 * i / 96);
            QPointF px;
            if (!gizmo.projectToPixel(ringPoint(gizmo.getTransform(), scale, deg, kRings[0]), px)) continue;
            float d = -1.0f;
            if (gizmo.ringNameAtPixel(px, d) == QLatin1String("x")) { startDeg = deg; grabPx = px; }
        }
        if (startDeg < 0.0f) { std::printf("FAIL: no X-ring pixel to grab\n"); ++failures; continue; }

        const iris::Vec3 forward =
            cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();
        iris::Vec3 rayPos, rayDir;
        rayFromPixel(cam, grabPx, rayPos, rayDir);
        gizmo.startDragging(rayPos, rayDir, forward);
        if (!gizmo.isDragging()) { std::printf("FAIL: the press did not start a drag\n"); ++failures; continue; }

        const iris::Mat4 frozen = gizmo.getTransform();
        float worstFrozenPx = 0.0f, worstLivePx = 0.0f, turned = 0.0f;
        bool frameHeld = true, ringHeld = true, nameHeld = true, drawnAgrees = true;

        for (int step = 1; step <= 12; ++step) {          // 5 .. 60 degrees
            QPointF px;
            if (!gizmo.projectToPixel(ringPoint(frozen, scale, startDeg + 5.0f * step, kRings[0]), px)) continue;
            rayFromPixel(cam, px, rayPos, rayDir);
            gizmo.drag(rayPos, rayDir, forward);
            node->update(0.0f);
            // EXACTLY WHAT THE VIEWPORT DOES EVERY FRAME of a live drag: it
            // re-sizes the gizmo and re-sets the pick view through mouseRay
            // before handing the drag on.
            gizmo.updateSize(cam);
            gizmo.setPickView(cam, kWidth, kHeight);

            // A: the frame has not moved, bit for bit.
            if (!sameMatrix(gizmo.getTransform(), frozen)) frameHeld = false;

            // B: the ring is still under the grab pixel, and still answers.
            const float dFrozen = ringDistancePx(gizmo, frozen, scale, grabPx);
            worstFrozenPx = std::max(worstFrozenPx, dFrozen);
            float reported = -1.0f;
            if (gizmo.ringNameAtPixel(grabPx, reported) != QLatin1String("x")) nameHeld = false;
            if (reported > kRingPickTolerancePx) ringHeld = false;

            // C: the DRAWN item is the frozen frame too (scaled).
            const QVector<GizmoDrawItem> items = gizmo.drawItems(rayPos, rayDir, forward);
            if (items.isEmpty()) drawnAgrees = false;
            for (const GizmoDrawItem &item : items) {
                iris::Mat4 expect = frozen;
                expect.scale(gizmo.getGizmoScale() * kHandleScale);
                if (!sameMatrix(item.transform, expect)) drawnAgrees = false;
            }

            // E: HOW FAR THE PICTURE WOULD HAVE MOVED if the frame still
            // followed the node. Dragging the X ring turns the node about X,
            // which leaves the X RING ITSELF invariant — it is the other two
            // that spin out from under the cursor, and the owner's report is
            // about the gizmo as a whole ("they seem to move on their own with
            // the asset"). So every ring is measured: for each, how far a point
            // of its FROZEN circle now is from its LIVE one.
            const iris::Mat4 live = liveFrame(node, space.value);
            for (const Ring &r : kRings) {
                for (int i = 0; i < 12; ++i) {
                    QPointF rp;
                    if (!gizmo.projectToPixel(ringPoint(frozen, scale, float(360.0 * i / 12), r), rp))
                        continue;
                    const float d = ringDistancePx(gizmo, live, scale, rp, r);
                    if (d > 0.0f) worstLivePx = std::max(worstLivePx, d);
                }
            }

            const iris::Quat q = node->getLocalRot().normalized();
            turned = 2.0f * float(qRadiansToDegrees(
                         std::acos(std::min(1.0f, std::max(-1.0f, std::fabs(q.scalar()))))));
        }

        std::printf("   grab pixel %.0f,%.0f   turned %.1f deg   frozen ring stayed within "
                    "%.2f px of the grab   the LIVE rings ran to %.1f px\n",
                    grabPx.x(), grabPx.y(), double(turned), double(worstFrozenPx),
                    double(worstLivePx));

        CHECK(frameHeld, "A: the gizmo's frame is bit-identical through the whole drag");
        CHECK(worstFrozenPx < 1.0f, "B: the grabbed ring never moves as much as a pixel away "
                                    "from the grab point");
        CHECK(nameHeld && ringHeld, "B: the grab pixel still picks the X ring at every step");
        CHECK(drawnAgrees, "C: every drawn item uses that same frozen frame");
        CHECK(turned > 45.0f, "D: and the node really turned (the VALUE is live)");
        if (space.value == GizmoTransformSpace::Local) {
            CHECK(worstLivePx > 4.0f * kRingPickTolerancePx,
                  "E: the frame the gizmo USED to draw runs the rings away from under the "
                  "cursor by many times the pick tolerance (the reported defect, measured)");
        } else {
            CHECK(worstLivePx < 1.0f,
                  "E: in Global space the rings never rotated anyway — the freeze costs "
                  "nothing there");
        }

        // F: release re-orients.
        gizmo.endDragging();
        node->update(0.0f);
        CHECK(sameMatrix(gizmo.getTransform(), liveFrame(node, space.value)),
              "F: the release drops the frozen frame — the rings snap to the node again");
    }

    std::printf("\n%s\n", failures == 0 ? "PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
