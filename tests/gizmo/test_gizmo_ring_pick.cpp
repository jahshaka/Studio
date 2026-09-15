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
//   A. every sampled pixel of the DRAWN part of every ring hits SOME ring, and
//      the ring it hits really is under the cursor (within the pick tolerance);
//   B. each ring wins most of its own drawn pixels (the rest are the crossings,
//      where two rings are equally under the cursor and the more face-on one
//      wins by design);
//   B2. THE HIDDEN HALF IS NOT A HANDLE (GIZMO-3 item 1, Blender's rule). Since
//      each ring is drawn as the arc on the camera's side of the plane through
//      the gizmo's centre perpendicular to the view, the other half is not
//      there — and a pixel of it that is not also under the DRAWN arc (an
//      edge-on ring projects its two halves onto the same line, so most of
//      them are) must not pick that ring. The classification here is derived
//      from the RULE, not from the gizmo's own numbers: a sample is "drawn"
//      when dot(p - centre, toCamera) is comfortably positive and "hidden"
//      when it is comfortably negative, with a quarter-radius band around the
//      cut left out of both (that band is where the implementation's
//      Blender-style clip bias lives);
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
#include "viewport/gizmomeshes.h"
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

/// BLENDER'S RULE, WRITTEN OUT (GIZMO-3 item 1). A ring is drawn where
///
///     dot(p - centre, toCamera) >= -bias * radius
///
/// (dial3d_gizmo.c clips the dial against the plane through the gizmo's origin
/// whose normal is the camera's own axis, pushed back by DIAL_CLIP_BIAS times
/// the dial's scale). Substituting p - centre = radius * (u cos a + v sin a)
/// and writing (c, s) for toCamera's components in that basis, the condition is
///
///     cos(a - atan2(s, c)) >= -bias / hypot(c, s),
///
/// i.e. ONE ARC, centred on the eye's direction within the ring's own plane,
/// with the half-angle this returns (in degrees): 90 for a ring seen edge-on,
/// a little more as it turns towards the camera, 180 — the whole circle — once
/// the ring is face-on and its two halves are the same distance from the eye.
/// `centreDeg` comes back as the arc's centre in the same parameterisation the
/// samples below use.
float drawnArc(const iris::Vec3 &u, const iris::Vec3 &v, const iris::Vec3 &toCamera,
               float bias, float &centreDeg)
{
    const float c = iris::Vec3::dotProduct(toCamera, u);
    const float s = iris::Vec3::dotProduct(toCamera, v);
    const float m = std::sqrt(c * c + s * s);
    centreDeg = m > 1e-6f ? float(qRadiansToDegrees(std::atan2(s, c))) : 0.0f;
    const float cosCut = m > 1e-6f ? -bias / m : -1.0f;
    return float(qRadiansToDegrees(std::acos(std::max(-1.0f, std::min(1.0f, cosCut)))));
}

/// Smallest signed difference between two angles in degrees.
float angleDelta(float a, float b)
{
    float d = a - b;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

/// How far `cursor` is, in pixels, from the part of a ring that is DRAWN — its
/// camera-facing arc, walked here independently of the gizmo's own state.
float drawnArcDistancePx(RotationGizmo &gizmo, const iris::Vec3 &centre, const iris::Vec3 &u,
                         const iris::Vec3 &v, float radius, float arcCentreDeg, float arcHalfDeg,
                         const QPointF &cursor)
{
    float best = -1.0f;
    QPointF prev;
    bool havePrev = false;
    for (int i = 0; i <= 180; ++i) {
        const float deg = float(360.0 * i / 180.0);
        if (std::fabs(angleDelta(deg, arcCentreDeg)) > arcHalfDeg) { havePrev = false; continue; }
        const float a = float(qDegreesToRadians(deg));
        const iris::Vec3 off = (u * std::cos(a) + v * std::sin(a)) * radius;
        QPointF px;
        if (!gizmo.projectToPixel(centre + off, px)) { havePrev = false; continue; }
        const double dx = cursor.x() - px.x(), dy = cursor.y() - px.y();
        float d = float(std::sqrt(dx * dx + dy * dy));
        if (havePrev) {
            const double vx = px.x() - prev.x(), vy = px.y() - prev.y();
            const double wx = cursor.x() - prev.x(), wy = cursor.y() - prev.y();
            const double len2 = vx * vx + vy * vy;
            double t = len2 > 1e-12 ? (wx * vx + wy * vy) / len2 : 0.0;
            t = std::min(1.0, std::max(0.0, t));
            const double ex = wx - t * vx, ey = wy - t * vy;
            d = std::min(d, float(std::sqrt(ex * ex + ey * ey)));
        }
        if (best < 0.0f || d < best) best = d;
        prev = px; havePrev = true;
    }
    return best;
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

    // One probe handle per ring, on the same gizmo: RotationHandle is the
    // public description of a ring, so the suite can ask a NAMED ring for its
    // drag angle instead of asking whichever ring the pick chose.
    RotationHandle probeX(&gizmo, GizmoAxis::X), probeY(&gizmo, GizmoAxis::Y),
        probeZ(&gizmo, GizmoAxis::Z);
    RotationHandle *probes[3] = { &probeX, &probeY, &probeZ };

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
    int totalPixels = 0, drawnPixels = 0, drawnHit = 0;
    // E: how the OLD annulus rule did on the very same pixels, and how often a
    // whole ring was dead from a whole camera — the owner's report, counted.
    int totalOldHits = 0, deadRings = 0;
    // B2 (GIZMO-3): hidden-half samples that land clear of the drawn arc, and
    // how many of them wrongly picked their own ring.
    int hiddenTested = 0, hiddenPicked = 0;
    /// How far outside the pick tolerance a hidden sample has to be from the
    /// DRAWN arc before its answer is meaningful (an edge-on ring projects both
    /// halves onto one line, so most hidden samples sit ON the drawn arc and
    /// are correctly pickable).
    constexpr float kHiddenClearPx = kRingPickTolerancePx + 3.0f;
    /// Blender's DIAL_CLIP_BIAS, as the RULE states it: the clip plane sits
    /// this fraction of the ring's radius behind the centre (see drawnArc).
    constexpr float kClipBias = 0.02f;
    /// ...and how far, IN THE RING'S OWN ANGLE, a sample has to be from the end
    /// of the drawn arc before it counts as drawn or as hidden. Everything
    /// within this band of either end is left out of both claims.
    constexpr float kArcEndMarginDeg = 5.0f;

    for (const Camera &c : kCameras) {
        place(cam, c);
        gizmo.updateSize(cam);
        gizmo.setPickView(cam, kWidth, kHeight);

        const float scale = gizmo.getGizmoScale() * GizmoMeshes::kRotationHandleScale;
        const iris::Mat4 gizmoTransform = gizmo.getTransform();
        const iris::Vec3 centre = gizmoTransform.column(3).toVector3D();
        const iris::Vec3 forward =
            cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();

        std::printf("\n== camera %s ==\n", c.name);
        const iris::Vec3 toCamera = -forward;      // Blender cuts with the view axis
        for (const Ring &ring : kRings) {
            const float facing = std::fabs(iris::Vec3::dotProduct(ring.normal, forward));
            // The arc this ring DRAWS, derived from Blender's rule, not from
            // the gizmo's own numbers.
            float arcCentreDeg = 0.0f;
            const float arcHalfDeg = drawnArc(ring.u, ring.v, toCamera, kClipBias, arcCentreDeg);
            int own = 0, any = 0, samples = 0, samplesAll = 0, oldHits = 0;
            int longestOwnRun = 0, run = 0;
            float maxAngleStep = 0.0f, oldMaxAngleStep = 0.0f;
            float winding = 0.0f, prevAngle = 0.0f;
            bool haveAngle = true, firstAngle = true;

            for (int i = 0; i < kSamples; ++i) {
                const float a = float(2.0 * M_PI * i / kSamples);
                const iris::Vec3 off = (ring.u * std::cos(a) + ring.v * std::sin(a)) * scale;
                const iris::Vec3 world = centre + off;
                QPointF px;
                if (!gizmo.projectToPixel(world, px)) continue;      // behind the eye
                if (px.x() < 0 || px.y() < 0 || px.x() > kWidth || px.y() > kHeight) continue;
                ++samplesAll; ++totalPixels;
                const float fromArcEnd =
                    std::fabs(angleDelta(float(qRadiansToDegrees(a)), arcCentreDeg));
                const bool drawnHere  = fromArcEnd <= arcHalfDeg - kArcEndMarginDeg;
                const bool hiddenHere = fromArcEnd >= arcHalfDeg + kArcEndMarginDeg;

                // The old rule, on the very same pixel, over the WHOLE circle —
                // it is the pre-S15 pick being measured, not ours.
                iris::Vec3 rayPos, rayDir;
                rayFromPixel(cam, px, rayPos, rayDir);
                if (oldAnnulusHit(gizmoTransform, scale, ring.normal, rayPos, rayDir)) ++oldHits;

                // F: THE DRAG ANGLE is defined all the way round — a drag that
                // started on the drawn arc may carry the cursor anywhere — so
                // it is walked over the whole circle, as before, and it is
                // THIS ring's angle that is walked (a probe handle on the same
                // gizmo: since GIZMO-3 the pick refuses the hidden half, so
                // asking the PICKED ring would stop measuring here).
                {
                    float angle = 0.0f;
                    const bool ok = probes[&ring - kRings]->getHitAngle(rayPos, rayDir, angle);
                    if (!ok || !std::isfinite(angle)) haveAngle = false;
                    else {
                        if (!firstAngle) {
                            const float d = angleDelta(angle, prevAngle);
                            winding += d;
                            maxAngleStep = std::max(maxAngleStep, std::fabs(d));
                        }
                        prevAngle = angle; firstAngle = false;
                    }
                }

                float distancePx = -1.0f;
                const QString hit = gizmo.ringNameAtPixel(px, distancePx);
                // A: whatever answered really is under the cursor.
                if (!hit.isEmpty() && distancePx > kRingPickTolerancePx) {
                    std::printf("FAIL: %s ring, pixel %.0f,%.0f: reported '%s' at %.2f px, "
                                "beyond the %.1f px tolerance\n", ring.name, px.x(), px.y(),
                                qPrintable(hit), double(distancePx), double(kRingPickTolerancePx));
                    ++failures;
                }

                // B2: THE HIDDEN HALF IS NOT A HANDLE — but only where it is
                // not also under the drawn arc.
                if (hiddenHere) {
                    const float toDrawn = drawnArcDistancePx(gizmo, centre, ring.u, ring.v, scale,
                                                             arcCentreDeg, arcHalfDeg, px);
                    if (toDrawn > kHiddenClearPx) {
                        ++hiddenTested;
                        if (hit == QLatin1String(ring.name)) {
                            ++hiddenPicked;
                            std::printf("FAIL: %s ring, pixel %.0f,%.0f: the HIDDEN half picked "
                                        "its own ring (%.1f px from anything drawn)\n", ring.name,
                                        px.x(), px.y(), double(toDrawn));
                            ++failures;
                        }
                    }
                    continue;                  // not a drawn pixel: A/B skip it
                }
                if (!drawnHere) continue;      // the cut band: neither claim applies
                ++samples; ++drawnPixels;
                if (!hit.isEmpty()) { ++any; ++drawnHit; }
                if (hit == QLatin1String(ring.name)) { ++own; ++run; longestOwnRun = std::max(longestOwnRun, run); }
                else run = 0;
            }

            totalOldHits += oldHits;
            if (samplesAll > 0 && oldHits == 0) ++deadRings;

            // The angle step is printed, not asserted: it walks the ring's 3D
            // parameter, and half of an edge-on ring projects onto the other
            // half, so a 180-degree flip at the silhouette is the projection
            // talking, not the drag. What the drag has to do is asserted below
            // (section G) and by the winding claim on a ring the camera can
            // actually see as a ring.
            std::printf("   ring %s  facing %.3f  arc %5.1f deg  pixels %2d (drawn %2d)  hit %2d  own %2d "
                        "(longest run %2d)  OLD rule hit %2d  angle step<=%6.2f deg  "
                        "winding %7.1f\n", ring.name, double(facing), double(2.0f * arcHalfDeg),
                        samplesAll, samples, any, own, longestOwnRun, oldHits,
                        double(maxAngleStep), double(winding));

            // A/C: every pixel of the DRAWN arc is a live click target, and
            // this ring answers to a healthy run of its own pixels.
            if (samples < 8) { std::printf("FAIL: ring %s projected too few pixels to test\n", ring.name); ++failures; }
            if (any != samples) {
                std::printf("FAIL: ring %s: %d of %d of its own DRAWN pixels hit NOTHING\n",
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

    std::printf("\n%d of %d DRAWN ring pixels hit a ring (of %d sampled all the way round); "
                "the OLD annulus rule took %d of them, and left %d whole ring(s) dead from a "
                "whole camera. %d hidden-half pixels clear of everything drawn were tested, "
                "%d of them wrongly picked their own ring\n",
                drawnHit, drawnPixels, totalPixels, totalOldHits, deadRings, hiddenTested,
                hiddenPicked);
    CHECK(totalPixels > 500 && drawnPixels > 250, "the sweep covered a real number of pixels");
    CHECK(drawnHit == drawnPixels, "EVERY DRAWN pixel on EVERY ring, from EVERY camera, is "
                                   "clickable (the owner's acceptance for S15, on the arc "
                                   "GIZMO-3 draws)");
    // B2: the other half of that sentence — what is NOT drawn is NOT a handle.
    CHECK(hiddenTested > 100, "the sweep found a real number of hidden-half pixels clear of the "
                              "drawn arc to test");
    CHECK(hiddenPicked == 0, "and NONE of them picks its ring: the half a ring does not draw is "
                             "not a handle (GIZMO-3 item 1, Blender's rule)");
    // E: THE DEFECT, as numbers. "Sometimes I can't click R, sometimes G or B"
    // is a ring that answers at NO pixel from some camera — and there were
    // such rings, while the screen-space pick answers at every pixel of all of
    // them.
    CHECK(deadRings >= 2, "the old 3D annulus rule left whole rings unclickable from whole "
                          "cameras (the reported defect, reproduced inline)");
    CHECK(totalOldHits * 4 < totalPixels * 3, "and it rejected a quarter or more of the ring "
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
            const float scale = gizmo.getGizmoScale() * GizmoMeshes::kRotationHandleScale;
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
            const float scale = gizmo.getGizmoScale() * GizmoMeshes::kRotationHandleScale;
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
        const float scale = gizmo.getGizmoScale() * GizmoMeshes::kRotationHandleScale;
        const iris::Mat4 t = gizmo.getTransform();
        const iris::Vec3 centre = t.column(3).toVector3D();
        const auto dir = [&t](const iris::Vec3 &d) {
            return (t * iris::Vec4(d, 0)).toVector3D().normalized();
        };
        const iris::Vec3 toCamera =
            -cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();
        int missed = 0, sampled = 0;
        for (const Ring &ring : kRings) {
            // The rings are in the NODE's frame here, so the arc is derived in
            // that frame too — the ring basis the gizmo draws in.
            const iris::Vec3 uw = dir(ring.u), vw = dir(ring.v);
            float arcCentreDeg = 0.0f;
            const float arcHalfDeg = drawnArc(uw, vw, toCamera, 0.02f, arcCentreDeg);
            for (int i = 0; i < 36; ++i) {
                const float deg = float(360.0 * i / 36);
                if (std::fabs(angleDelta(deg, arcCentreDeg)) > arcHalfDeg - 5.0f) continue;
                const float a = float(qDegreesToRadians(deg));
                QPointF px;
                if (!gizmo.projectToPixel(centre + uw * scale * std::cos(a) + vw * scale * std::sin(a), px))
                    continue;
                ++sampled;
                float d = -1.0f;
                if (gizmo.ringNameAtPixel(px, d).isEmpty()) ++missed;
            }
        }
        std::printf("\n   local space, node rotated (37,-52,18): %d drawn pixels sampled, "
                    "%d missed\n", sampled, missed);
        CHECK(sampled > 45 && missed == 0,
              "a LOCAL-space gizmo on a rotated node is pickable all the way along every ring's "
              "drawn arc");
        gizmo.setTransformSpace(GizmoTransformSpace::Global);
        node->setLocalRot(iris::Quat());
    }

    // ---- H: A 55-DEGREE SWEEP TURNS THE NODE 55.00 DEGREES -----------------
    //
    // The half-ring change moves what is DRAWN and what is PICKED; it must not
    // move the drag maths by a hundredth of a degree. Driven on a face-on ring,
    // where the cursor's angle around the circle IS the ring's angle, so the
    // expected answer is exact rather than a tolerance band.
    {
        std::printf("\n== a 55-degree sweep ==\n");
        node->setLocalRot(iris::Quat());
        node->update(0.0f);
        gizmo.setTransformSpace(GizmoTransformSpace::Global);
        place(cam, { "front", 0.0f, 0.0f, 9.0f });
        gizmo.updateSize(cam);
        gizmo.setPickView(cam, kWidth, kHeight);
        const float scale = gizmo.getGizmoScale() * GizmoMeshes::kRotationHandleScale;
        const iris::Vec3 centre = gizmo.getTransform().column(3).toVector3D();
        const iris::Vec3 forward =
            cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();
        const Ring &ring = kRings[2];                    // Z: face-on from the front
        const auto pixelAt = [&](float degrees, QPointF &px) {
            const float a = float(qDegreesToRadians(degrees));
            return gizmo.projectToPixel(
                centre + (ring.u * std::cos(a) + ring.v * std::sin(a)) * scale, px);
        };
        QPointF startPx, endPx;
        const float startDeg = 20.0f, sweepDeg = 55.0f;
        if (!pixelAt(startDeg, startPx) || !pixelAt(startDeg + sweepDeg, endPx)) {
            std::printf("FAIL: the 55-degree sweep could not be projected\n"); ++failures;
        } else {
            float d = -1.0f;
            const QString grabbed = gizmo.ringNameAtPixel(startPx, d);
            iris::Vec3 rayPos, rayDir;
            rayFromPixel(cam, startPx, rayPos, rayDir);
            gizmo.startDragging(rayPos, rayDir, forward);
            rayFromPixel(cam, endPx, rayPos, rayDir);
            gizmo.drag(rayPos, rayDir, forward);
            node->update(0.0f);
            // READ BEFORE THE RELEASE: with no undo service wired up (this is a
            // document-only suite) createUndoAction puts the node back where
            // the drag started and has no command stack to re-apply it from.
            const iris::Quat q = node->getLocalRot().normalized();
            const float turned = 2.0f * float(qRadiansToDegrees(
                std::acos(qBound(-1.0f, std::fabs(q.scalar()), 1.0f))));
            gizmo.endDragging();
            std::printf("   grabbed '%s' at %.2f px; a %.0f-degree sweep turned the node "
                        "%.2f degrees\n", qPrintable(grabbed), double(d), double(sweepDeg),
                        double(turned));
            CHECK(grabbed == QLatin1String("z"), "the sweep starts on the face-on Z ring");
            CHECK(std::fabs(turned - sweepDeg) < 0.05f,
                  "and a 55-degree sweep turns the node 55.00 degrees (the drag maths is "
                  "untouched by the half-ring drawing)");
            node->setLocalRot(iris::Quat());
            node->update(0.0f);
        }
    }

    // ---- I: THE RINGS FOLLOW THE GLOBAL/LOCAL TOGGLE -----------------------
    //
    // editor.setGizmoSpace("local"|"global") reaches the gizmo through
    // Gizmo::setTransformSpace, and Gizmo::getTransform answers the node's
    // position ALONE in Global space and its position AND rotation in Local —
    // so the ring PLANES are the world's or the object's. Verified rather than
    // assumed (GIZMO-3 item 4): a node turned 40 degrees about Y, and both the
    // drawn ring's own axis and the PICK are checked in each space.
    {
        std::printf("\n== the Global/Local toggle ==\n");
        const float turnDeg = 40.0f;
        node->setLocalRot(iris::Quat::fromEulerAngles(0.0f, turnDeg, 0.0f));
        node->update(0.0f);
        place(cam, { "iso", -35.0f, 45.0f, 9.0f });
        gizmo.updateSize(cam);
        gizmo.setPickView(cam, kWidth, kHeight);
        const iris::Vec3 forward =
            cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();
        const iris::Vec3 toCamera = -forward;
        // The X ring's axis is world X in Global space and the NODE's X — a
        // 40-degree turn about Y — in Local.
        const iris::Vec3 worldX(1, 0, 0);
        const iris::Vec3 nodeX = node->getGlobalRotation().normalized().rotatedVector(worldX);

        struct Case { const char *name; GizmoTransformSpace space; };
        const Case kCases[] = { { "Global", GizmoTransformSpace::Global },
                                { "Local",  GizmoTransformSpace::Local } };
        for (const Case &c : kCases) {
            gizmo.setTransformSpace(c.space);
            gizmo.updateSize(cam);
            gizmo.setPickView(cam, kWidth, kHeight);
            const iris::Mat4 t = gizmo.getTransform();
            const iris::Vec3 centre = t.column(3).toVector3D();
            const float scale = gizmo.getGizmoScale() * GizmoMeshes::kRotationHandleScale;

            // (a) THE DRAWN RING'S OWN AXIS. Every drawn item carries the frame
            // it is drawn in; the X ring's mesh is built about +X, so the
            // item's first column IS that ring's axis in world space. The X
            // ring is the one in the axis colour 237,66,66.
            iris::Vec3 drawnAxis;
            bool haveAxis = false;
            for (const GizmoDrawItem &item : gizmo.drawItems(iris::Vec3(), iris::Vec3(), forward)) {
                if (item.colour != QColor(237, 66, 66)) continue;
                drawnAxis = item.transform.column(0).toVector3D().normalized();
                haveAxis = true;
                break;
            }
            const iris::Vec3 expected = c.space == GizmoTransformSpace::Global ? worldX : nodeX;
            const float align = haveAxis ? std::fabs(iris::Vec3::dotProduct(drawnAxis, expected)) : 0.0f;
            const float offWorld = haveAxis
                ? float(qRadiansToDegrees(std::acos(qBound(-1.0f,
                      std::fabs(iris::Vec3::dotProduct(drawnAxis, worldX)), 1.0f)))) : -1.0f;
            std::printf("   %-6s space: the red ring's axis is %.4f aligned with the %s X axis, "
                        "and %.1f degrees off the WORLD X\n", c.name, double(align),
                        c.space == GizmoTransformSpace::Global ? "world" : "object",
                        double(offWorld));
            CHECK(haveAxis && align > 0.9999f,
                  c.space == GizmoTransformSpace::Global
                      ? "Global: the ring planes are the WORLD's"
                      : "Local: the ring planes are the OBJECT's");
            const bool tilted = std::fabs(offWorld - turnDeg) < 0.5f;
            if (c.space == GizmoTransformSpace::Local)
                CHECK(tilted, "...and it really is turned with the object (40 degrees off world X)");
            else
                CHECK(offWorld < 0.5f, "...and it is NOT turned with the object");

            // (b) THE PICK SAYS THE SAME THING: the drawn arc of the circle in
            // THIS space is picked as the X ring, and the circle of the OTHER
            // space is mostly not (the two cross at the turn axis, so a few of
            // its pixels legitimately land on the drawn ring).
            const auto sweepCircle = [&](const iris::Vec3 &axis, int &own, int &tested) {
                own = tested = 0;
                const iris::Vec3 u = (std::fabs(axis.y()) < 0.9f
                                          ? iris::Vec3::crossProduct(axis, iris::Vec3(0, 1, 0))
                                          : iris::Vec3::crossProduct(axis, iris::Vec3(1, 0, 0))).normalized();
                const iris::Vec3 v = iris::Vec3::crossProduct(axis, u).normalized();
                float arcCentreDeg = 0.0f;
                const float arcHalfDeg = drawnArc(u, v, toCamera, 0.02f, arcCentreDeg);
                for (int i = 0; i < 36; ++i) {
                    const float deg = float(360.0 * i / 36);
                    if (std::fabs(angleDelta(deg, arcCentreDeg)) > arcHalfDeg - 5.0f) continue;
                    const float a = float(qDegreesToRadians(deg));
                    QPointF px;
                    if (!gizmo.projectToPixel(centre + (u * std::cos(a) + v * std::sin(a)) * scale, px))
                        continue;
                    ++tested;
                    float d = -1.0f;
                    if (gizmo.ringNameAtPixel(px, d) == QLatin1String("x")) ++own;
                }
            };
            int ownHere = 0, testedHere = 0, ownOther = 0, testedOther = 0;
            sweepCircle(expected, ownHere, testedHere);
            sweepCircle(c.space == GizmoTransformSpace::Global ? nodeX : worldX, ownOther, testedOther);
            std::printf("   %-6s space: %d of %d pixels of the %s circle pick 'x'; %d of %d of "
                        "the other space's circle do\n", c.name, ownHere, testedHere,
                        c.space == GizmoTransformSpace::Global ? "world" : "object",
                        ownOther, testedOther);
            CHECK(testedHere > 8 && ownHere * 10 >= testedHere * 6,
                  "and the PICK follows the same planes as the picture");
            CHECK(testedOther > 8 && ownOther * 4 <= testedOther,
                  "...while the circle of the OTHER space is not the X ring here");
        }
        gizmo.setTransformSpace(GizmoTransformSpace::Global);
        node->setLocalRot(iris::Quat());
        node->update(0.0f);
    }

    // ---- THE DEFAULT SPACE, AS A DECISION ----------------------------------
    //
    // A fresh gizmo is in GLOBAL space (Gizmo::Gizmo): the rings and arrows
    // aligned to the world axes whatever the object's turn, Blender's model
    // and the owner's call (2026-09-15). Nothing in the app writes the space at
    // startup (no persisted setting; MainWindow only checks whichever toolbar
    // button matches what the gizmos already are), so the constructor IS the
    // default. It was Local by accident until GIZMO-3 reported it.
    {
        RotationGizmo fresh;
        const bool global = fresh.getTransformSpace() == GizmoTransformSpace::Global;
        std::printf("\n   a fresh gizmo's transform space is %s\n", global ? "GLOBAL" : "LOCAL");
        CHECK(global, "the default transform space at a fresh launch is GLOBAL (owner, 2026-09-15: "
                      "Blender's model; it was Local by accident until GIZMO-3 reported it)");
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
