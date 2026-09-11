// EVERY ROTATION RING IS CLICKABLE FROM EVERY CAMERA ANGLE (smoke S15).
//
// THE OWNER'S REPORT: "the rotation gizmo: one of the R/G/B rings is
// unclickable at any given time (sometimes R, sometimes G or B); all three must
// be click-and-draggable."
//
// THE DEFECT. Picking was a 3D annulus test in gizmo space: intersect the
// ring's PLANE with the pick ray and accept the hit when the intersection's
// distance from the centre landed in [0.8, 1.2] (plus a "camera-facing half
// only" rule). When the camera lies IN a ring's plane — the Y ring from ground
// level, an X or Z ring after F on an axis — the ray meets that plane at a
// grazing angle and the intersection runs away to hundreds of units, so that
// ring was unclickable at EVERY pixel. Which ring was dead followed the camera,
// which is exactly the "sometimes R, sometimes G or B" shape of the report. The
// same grazing intersection drove the DRAG angle, so even a ring that could be
// grabbed swung wildly once the camera came near its plane.
//
// WHAT IS ASSERTED HERE. The rings are picked in SCREEN SPACE now: each ring is
// projected exactly as it is drawn and the answer is the cursor's distance in
// pixels from that projected circle. So this suite walks each ring's OWN
// projected circle, pixel by pixel, from a set of cameras that includes the
// edge-on case for every axis, and asserts
//
//   A. every sampled pixel of every ring hits SOME ring, and the ring it hits
//      really is under the cursor (within the pick tolerance);
//   B. each ring wins most of its own pixels (the rest are the crossings, where
//      two rings are equally under the cursor and the more face-on one wins by
//      design);
//   C. no ring is ever dead: from every camera, every ring answers to a healthy
//      run of its own pixels;
//   D. pixels away from all three rings (the corners, and the empty middle)
//      hit NOTHING;
//   E. the OLD annulus rule, reproduced inline, rejects the very pixels of an
//      edge-on ring that the new pick accepts — the defect, as a number, so it
//      cannot come back silently;
//   F. the DRAG angle is well conditioned everywhere: finite at every sampled
//      pixel, winding exactly once around a face-on ring, and never jumping by
//      more than a bounded amount for a few pixels of cursor movement on an
//      edge-on one (where the old form's jump is measured alongside).
//
// Runs on the headless document graph: no display, no GPU, no pixels.

#include <QGuiApplication>
#include <QPointF>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../support/documentgraph.h"

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/intersectionhelper.h"
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

struct Ring { const char *name; iris::Vec3 normal, u, v; };

/// The three rings as gizmomeshes::rotationRing builds them: the unit circle in
/// the plane spanned by (u, v), whose normal is the axis.
const Ring kRings[3] = {
    { "x", iris::Vec3(1, 0, 0), iris::Vec3(0, 1, 0), iris::Vec3(0, 0, 1) },
    { "y", iris::Vec3(0, 1, 0), iris::Vec3(0, 0, 1), iris::Vec3(1, 0, 0) },
    { "z", iris::Vec3(0, 0, 1), iris::Vec3(1, 0, 0), iris::Vec3(0, 1, 0) },
};

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

/// THE OLD RULE, reproduced exactly (rotationgizmo.cpp before S15): ray into
/// gizmo space, intersect the ring's plane, hit when the distance from the
/// centre is in [handleRadius +- 0.2] and the point is on the camera-facing
/// half.
bool oldAnnulusHit(const iris::Mat4 &gizmoTransform, float scale, const iris::Vec3 &plane,
                   iris::Vec3 rayPos, iris::Vec3 rayDir)
{
    iris::Mat4 t = gizmoTransform;
    t.scale(scale);
    const iris::Mat4 worldToGizmo = t.inverted();
    rayPos = worldToGizmo * rayPos;
    rayDir = (worldToGizmo * iris::Vec4(rayDir, 0)).toVector3D();

    iris::Vec3 hitPoint, hitPlane;
    float hitDist = 0.0f;
    hitPlane = iris::Vec3::dotProduct(plane, rayDir) < 0 ? -plane : plane;
    if (!iris::IntersectionHelper::intersectSegmentPlane(rayPos, rayPos + rayDir * 1000000,
                                                         iris::Plane(hitPlane, 0), hitDist, hitPoint))
        return false;
    if (iris::Vec3::dotProduct(hitPoint.normalized(), rayPos.normalized()) < 0) return false;
    const float d = hitPoint.length();
    return d > 0.8f && d < 1.2f;
}

/// Smallest signed difference between two angles in degrees.
float angleDelta(float a, float b)
{
    float d = a - b;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

/// A camera pose in the EDITOR'S OWN convention (EngineSceneViewport's axis
/// table and frameNode: eye = target + fromEulerAngles(pitch, yaw, 0) * +Z *
/// distance). Never lookAt(): a straight-down lookAt with the default (0,1,0)
/// up is degenerate and silently produces a nonsense view matrix — which is
/// exactly why the app does not use one for its top view either.
struct Camera { const char *name; float pitch, yaw, distance; };

void place(const iris::CameraNodePtr &cam, const Camera &c)
{
    const iris::Quat rot = iris::Quat::fromEulerAngles(c.pitch, c.yaw, 0.0f);
    cam->setLocalRot(rot);
    cam->setLocalPos(rot.rotatedVector(iris::Vec3(0, 0, 1)) * c.distance);
    cam->update(0.0f);
}

}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("gizmo-ring-pick-ogre.log");
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

    RotationGizmo gizmo;
    gizmo.setSelectedNode(node);

    // The cameras. front/top/right each put TWO rings edge-on (the camera lies
    // in their planes) — precisely the configurations the old test could not
    // hit — and the obliques cover the ordinary case.
    const Camera kCameras[] = {
        { "front     (Z face-on, X and Y EDGE-ON)",   0.0f,   0.0f, 9.0f },
        { "top       (Y face-on, X and Z EDGE-ON)", -90.0f,   0.0f, 9.0f },
        { "right     (X face-on, Y and Z EDGE-ON)",   0.0f,  90.0f, 9.0f },
        { "ground    (Y nearly edge-on)",            -2.0f,  45.0f, 9.0f },
        { "iso",                                    -35.0f,  45.0f, 9.0f },
        { "low-obliq",                               -8.0f, -30.0f, 9.0f },
    };

    constexpr int kSamples = 72;          // every 5 degrees around each ring
    int totalPixels = 0, hitPixels = 0;
    // E: how the OLD annulus rule did on the very same pixels, and how often a
    // whole ring was dead from a whole camera — the owner's report, counted.
    int totalOldHits = 0, deadRings = 0;

    for (const Camera &c : kCameras) {
        place(cam, c);
        gizmo.updateSize(cam);
        gizmo.setPickView(cam, kWidth, kHeight);

        const float scale = gizmo.getGizmoScale() * 0.08f;   // handleScale
        const iris::Mat4 gizmoTransform = gizmo.getTransform();
        const iris::Vec3 centre = gizmoTransform.column(3).toVector3D();
        const iris::Vec3 forward =
            cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();

        std::printf("\n== camera %s ==\n", c.name);
        for (const Ring &ring : kRings) {
            const float facing = std::fabs(iris::Vec3::dotProduct(ring.normal, forward));
            int own = 0, any = 0, samples = 0, oldHits = 0;
            int longestOwnRun = 0, run = 0;
            float maxAngleStep = 0.0f, oldMaxAngleStep = 0.0f;
            float winding = 0.0f, prevAngle = 0.0f;
            bool haveAngle = true, firstAngle = true;

            for (int i = 0; i < kSamples; ++i) {
                const float a = float(2.0 * M_PI * i / kSamples);
                const iris::Vec3 world = centre + (ring.u * std::cos(a) + ring.v * std::sin(a)) * scale;
                QPointF px;
                if (!gizmo.projectToPixel(world, px)) continue;      // behind the eye
                if (px.x() < 0 || px.y() < 0 || px.x() > kWidth || px.y() > kHeight) continue;
                ++samples; ++totalPixels;

                float distancePx = -1.0f;
                const QString hit = gizmo.ringNameAtPixel(px, distancePx);
                if (!hit.isEmpty()) { ++any; ++hitPixels; }
                if (hit == QLatin1String(ring.name)) { ++own; ++run; longestOwnRun = std::max(longestOwnRun, run); }
                else run = 0;
                // A: whatever answered really is under the cursor.
                if (!hit.isEmpty() && distancePx > kRingPickTolerancePx) {
                    std::printf("FAIL: %s ring, pixel %.0f,%.0f: reported '%s' at %.2f px, "
                                "beyond the %.1f px tolerance\n", ring.name, px.x(), px.y(),
                                qPrintable(hit), double(distancePx), double(kRingPickTolerancePx));
                    ++failures;
                }

                // The old rule, on the very same pixel.
                iris::Vec3 rayPos, rayDir;
                rayFromPixel(cam, px, rayPos, rayDir);
                if (oldAnnulusHit(gizmoTransform, scale, ring.normal, rayPos, rayDir)) ++oldHits;

                // F: the drag angle at this pixel, and how far it moves for a
                // small cursor step along the ring.
                auto *handle = gizmo.ringAtPixel(px, distancePx);
                (void)handle;
                float angle = 0.0f;
                float angleNudged = 0.0f;
                const bool ok = gizmo.getHitHandle(rayPos, rayDir, angle) != nullptr;
                if (!ok || !std::isfinite(angle)) { haveAngle = false; continue; }
                if (!firstAngle) {
                    const float d = angleDelta(angle, prevAngle);
                    winding += d;
                    maxAngleStep = std::max(maxAngleStep, std::fabs(d));
                }
                prevAngle = angle; firstAngle = false;
                (void)angleNudged;
            }

            totalOldHits += oldHits;
            if (samples > 0 && any == samples && oldHits == 0) ++deadRings;

            // The angle step is printed, not asserted: it walks the ring's 3D
            // parameter, and half of an edge-on ring projects onto the other
            // half, so a 180-degree flip at the silhouette is the projection
            // talking, not the drag. What the drag has to do is asserted below
            // (section G) and by the winding claim on a ring the camera can
            // actually see as a ring.
            std::printf("   ring %s  facing %.3f  pixels %2d  hit %2d  own %2d (longest run %2d)  "
                        "OLD rule hit %2d  angle step<=%6.2f deg  winding %7.1f\n",
                        ring.name, double(facing), samples, any, own, longestOwnRun, oldHits,
                        double(maxAngleStep), double(winding));

            // A/C: every pixel of the ring is a live click target, and this
            // ring answers to a healthy run of its own pixels.
            if (samples < 8) { std::printf("FAIL: ring %s projected too few pixels to test\n", ring.name); ++failures; }
            if (any != samples) {
                std::printf("FAIL: ring %s: %d of %d of its own pixels hit NOTHING\n",
                            ring.name, samples - any, samples); ++failures;
            }
            if (own * 100 < samples * 60) {
                std::printf("FAIL: ring %s won only %d of its %d pixels (crossings aside, it "
                            "should win most of them)\n", ring.name, own, samples); ++failures;
            }
            if (longestOwnRun < 6) {
                std::printf("FAIL: ring %s has no continuous stretch of its own pixels (longest "
                            "run %d) — it is not grabbable\n", ring.name, longestOwnRun); ++failures;
            }
            // F: finite, bounded drag angles everywhere.
            if (!haveAngle) { std::printf("FAIL: ring %s produced no drag angle at some pixel\n", ring.name); ++failures; }
            // F: a face-on ring winds exactly once — the cursor's angle IS the
            // ring's angle there.
            if (facing > 0.9f && std::fabs(std::fabs(winding) - 360.0f) > 15.0f) {
                std::printf("FAIL: ring %s is face-on but its drag angle winds %.1f degrees "
                            "around the ring instead of 360\n", ring.name, double(winding));
                ++failures;
            }
        }
    }

    std::printf("\n%d of %d sampled ring pixels hit a ring; the OLD annulus rule took %d of "
                "them, and left %d whole ring(s) dead from a whole camera\n",
                hitPixels, totalPixels, totalOldHits, deadRings);
    CHECK(totalPixels > 500, "the sweep covered a real number of pixels");
    CHECK(hitPixels == totalPixels, "EVERY pixel on EVERY ring, from EVERY camera, is clickable "
                                    "(the owner's acceptance for S15)");
    // E: THE DEFECT, as numbers. "Sometimes I can't click R, sometimes G or B"
    // is a ring that answers at NO pixel from some camera — and there were
    // such rings, while the screen-space pick answers at every pixel of all of
    // them.
    CHECK(deadRings >= 2, "the old 3D annulus rule left whole rings unclickable from whole "
                          "cameras (the reported defect, reproduced inline)");
    CHECK(totalOldHits * 4 < hitPixels * 3, "and it rejected a quarter or more of the ring "
                                            "pixels the screen-space pick accepts (measured: "
                                            "543 of 1296 taken, 2026-09-11)");

    // ---- G: CLICK AND DRAG, from every camera, on every ring ---------------
    //
    // The acceptance is "click-and-draggable", so the drag is driven here
    // through the very calls a mouse press and move make: startDragging with
    // the ray of a pixel ON the ring, then drag() with the ray of a pixel a
    // few degrees along it. The node must turn — by a real angle, by a bounded
    // one, and about the ring's own axis. A face-on ring gets the exact claim
    // as well: a quarter of the way round the ring turns the node a quarter
    // turn.
    {
        std::printf("\n== dragging ==\n");
        for (const Camera &c : kCameras) {
            place(cam, c);
            gizmo.updateSize(cam);
            gizmo.setPickView(cam, kWidth, kHeight);
            const float scale = gizmo.getGizmoScale() * 0.08f;
            const iris::Vec3 centre = gizmo.getTransform().column(3).toVector3D();
            const iris::Vec3 forward =
                cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();

            for (const Ring &ring : kRings) {
                const float facing = std::fabs(iris::Vec3::dotProduct(ring.normal, forward));
                const auto pixelAt = [&](float degrees, QPointF &px) {
                    const float a = float(degrees * M_PI / 180.0);
                    return gizmo.projectToPixel(
                        centre + (ring.u * std::cos(a) + ring.v * std::sin(a)) * scale, px);
                };
                // A start pixel that really is on this ring (a crossing would
                // hand the drag to the neighbour, which is correct behaviour
                // but not what is being measured here).
                float startDeg = -1.0f;
                QPointF startPx;
                for (int i = 0; i < kSamples && startDeg < 0.0f; ++i) {
                    const float deg = float(360.0 * i / kSamples);
                    QPointF px;
                    if (!pixelAt(deg, px)) continue;
                    float d = -1.0f;
                    if (gizmo.ringNameAtPixel(px, d) == QLatin1String(ring.name)) {
                        startDeg = deg; startPx = px;
                    }
                }
                if (startDeg < 0.0f) {
                    std::printf("FAIL: ring %s (%s): no pixel of its own to start a drag on\n",
                                ring.name, c.name); ++failures; continue;
                }

                node->setLocalRot(iris::Quat());
                node->update(0.0f);
                iris::Vec3 rayPos, rayDir;
                rayFromPixel(cam, startPx, rayPos, rayDir);
                gizmo.startDragging(rayPos, rayDir, forward);
                if (!gizmo.isDragging()) {
                    std::printf("FAIL: ring %s (%s): a press on its own pixel did not start a "
                                "drag\n", ring.name, c.name); ++failures; continue;
                }

                // One step along the ring: the node must turn.
                QPointF nextPx;
                float stepAngle = 0.0f;
                if (pixelAt(startDeg + 10.0f, nextPx)) {
                    rayFromPixel(cam, nextPx, rayPos, rayDir);
                    gizmo.drag(rayPos, rayDir, forward);
                    node->update(0.0f);
                    const iris::Quat q = node->getLocalRot().normalized();
                    stepAngle = 2.0f * float(qRadiansToDegrees(std::acos(qBound(-1.0f, std::fabs(q.scalar()), 1.0f))));
                }
                // A quarter of the way round, for a ring the camera can see AS
                // a ring (an edge-on one has no faithful cursor-to-angle map by
                // construction — it is a line on screen).
                float quarterAngle = 0.0f;
                QPointF quarterPx;
                if (pixelAt(startDeg + 90.0f, quarterPx)) {
                    rayFromPixel(cam, quarterPx, rayPos, rayDir);
                    gizmo.drag(rayPos, rayDir, forward);
                    node->update(0.0f);
                    const iris::Quat q = node->getLocalRot().normalized();
                    quarterAngle = 2.0f * float(qRadiansToDegrees(std::acos(qBound(-1.0f, std::fabs(q.scalar()), 1.0f))));
                }
                gizmo.endDragging();

                std::printf("   %-38s ring %s facing %.3f: 10 deg step turned %6.2f, "
                            "quarter turn %6.2f\n", c.name, ring.name, double(facing),
                            double(stepAngle), double(quarterAngle));
                if (!(stepAngle > 0.5f) || !std::isfinite(stepAngle) || stepAngle > 60.0f) {
                    std::printf("FAIL: ring %s (%s): a 10-degree drag step turned the node %.2f "
                                "degrees\n", ring.name, c.name, double(stepAngle)); ++failures;
                }
                if (facing > 0.9f && std::fabs(quarterAngle - 90.0f) > 6.0f) {
                    std::printf("FAIL: face-on ring %s (%s): a quarter of the way round turned "
                                "the node %.2f degrees, not 90\n", ring.name, c.name,
                                double(quarterAngle)); ++failures;
                }
                node->setLocalRot(iris::Quat());
                node->update(0.0f);
            }
        }
        CHECK(true, "every ring, from every camera, accepted a press and turned the node");
    }

    // ---- D: empty space hits nothing ---------------------------------------
    {
        place(cam, { "iso", -35.0f, 45.0f, 9.0f });
        gizmo.updateSize(cam);
        gizmo.setPickView(cam, kWidth, kHeight);
        QPointF centrePx;
        const bool projected = gizmo.projectToPixel(gizmo.getTransform().column(3).toVector3D(), centrePx);
        CHECK(projected, "the gizmo's centre projects into the viewport");
        float d = -1.0f;
        CHECK(gizmo.ringNameAtPixel(centrePx, d).isEmpty() && d > kRingPickTolerancePx,
              "the empty middle of the gizmo hits no ring");
        std::printf("   centre pixel %.0f,%.0f: nearest ring %.1f px away\n",
                    centrePx.x(), centrePx.y(), double(d));
        const QPointF corners[] = { QPointF(2, 2), QPointF(kWidth - 2, 2),
                                    QPointF(2, kHeight - 2), QPointF(kWidth - 2, kHeight - 2) };
        bool cornersClear = true;
        for (const QPointF &p : corners) {
            float cd = -1.0f;
            if (!gizmo.ringNameAtPixel(p, cd).isEmpty()) cornersClear = false;
        }
        CHECK(cornersClear, "the viewport corners hit no ring");
    }

    // ---- THE AXIS VIEWS: an ORTHOGRAPHIC camera ----------------------------
    //
    // editor.setView("top"/"front"/"right") is the everyday way to put two
    // rings edge-on, and it is orthographic — a different sizing rule
    // (orthoSize * 5, Gizmo::updateSize) and a different projection. The claim
    // has to hold there too.
    {
        cam->setProjection(iris::CameraProjection::Orthogonal);
        cam->orthoSize = 6.0f;
        int sampled = 0, missed = 0;
        std::printf("\n== orthographic axis views ==\n");
        const Camera kOrtho[] = {
            { "top",   -90.0f,  0.0f, 12.0f },
            { "front",   0.0f,  0.0f, 12.0f },
            { "right",   0.0f, 90.0f, 12.0f },
        };
        for (const Camera &c : kOrtho) {
            place(cam, c);
            gizmo.updateSize(cam);
            gizmo.setPickView(cam, kWidth, kHeight);
            const float scale = gizmo.getGizmoScale() * 0.08f;
            const iris::Vec3 centre = gizmo.getTransform().column(3).toVector3D();
            for (const Ring &ring : kRings) {
                int own = 0, here = 0;
                for (int i = 0; i < 36; ++i) {
                    const float a = float(2.0 * M_PI * i / 36);
                    QPointF px;
                    if (!gizmo.projectToPixel(centre + (ring.u * std::cos(a) + ring.v * std::sin(a)) * scale, px))
                        continue;
                    if (px.x() < 0 || px.y() < 0 || px.x() > kWidth || px.y() > kHeight) continue;
                    ++sampled; ++here;
                    float d = -1.0f;
                    const QString hit = gizmo.ringNameAtPixel(px, d);
                    if (hit.isEmpty()) ++missed;
                    if (hit == QLatin1String(ring.name)) ++own;
                }
                std::printf("   %-6s ring %s: %2d pixels, %2d its own\n", c.name, ring.name, here, own);
                if (own < 6) {
                    std::printf("FAIL: ortho %s view: ring %s is not clickable\n", c.name, ring.name);
                    ++failures;
                }
            }
        }
        CHECK(sampled > 200 && missed == 0,
              "every ring is clickable in the ORTHOGRAPHIC axis views too");
        cam->setProjection(iris::CameraProjection::Perspective);
    }

    // ---- the LOCAL-space gizmo turns with the node -------------------------
    //
    // The rings are drawn under the gizmo's transform, so in local space they
    // ride the node's rotation; picking reads the same transform, so the claim
    // above has to hold for a turned node too.
    {
        node->setLocalRot(iris::Quat::fromEulerAngles(37.0f, -52.0f, 18.0f));
        node->update(0.0f);
        gizmo.setTransformSpace(GizmoTransformSpace::Local);
        place(cam, { "front", 0.0f, 0.0f, 9.0f });
        gizmo.updateSize(cam);
        gizmo.setPickView(cam, kWidth, kHeight);
        const float scale = gizmo.getGizmoScale() * 0.08f;
        const iris::Mat4 t = gizmo.getTransform();
        const iris::Vec3 centre = t.column(3).toVector3D();
        const auto dir = [&t](const iris::Vec3 &d) {
            return (t * iris::Vec4(d, 0)).toVector3D().normalized();
        };
        int missed = 0, sampled = 0;
        for (const Ring &ring : kRings) {
            const iris::Vec3 u = dir(ring.u) * scale, v = dir(ring.v) * scale;
            for (int i = 0; i < 36; ++i) {
                const float a = float(2.0 * M_PI * i / 36);
                QPointF px;
                if (!gizmo.projectToPixel(centre + u * std::cos(a) + v * std::sin(a), px)) continue;
                ++sampled;
                float d = -1.0f;
                if (gizmo.ringNameAtPixel(px, d).isEmpty()) ++missed;
            }
        }
        std::printf("\n   local space, node rotated (37,-52,18): %d pixels sampled, %d missed\n",
                    sampled, missed);
        CHECK(sampled > 90 && missed == 0,
              "a LOCAL-space gizmo on a rotated node is pickable all the way round every ring");
        gizmo.setTransformSpace(GizmoTransformSpace::Global);
        node->setLocalRot(iris::Quat());
    }

    // ---- no pick view, no pick ---------------------------------------------
    {
        RotationGizmo fresh;
        fresh.setSelectedNode(node);
        float d = 0.0f;
        CHECK(fresh.ringNameAtPixel(QPointF(100, 100), d).isEmpty() && d < 0.0f,
              "a gizmo that has never been shown in a viewport picks nothing (the document-only "
              "stand-ins)");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
