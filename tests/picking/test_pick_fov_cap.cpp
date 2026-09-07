// PICKING AGREES WITH THE PICTURE ON A WIDE WINDOW (owner report 2026-09-07).
//
// THE DEFECT this suite exists to stop coming back. The engine renders a FREE
// camera through the wide-aspect FOV clamp — `verticalFovForHorizontalCap`
// narrows the vertical angle once the target's aspect would take the horizontal
// one past 95 degrees (OgreView.cpp, CameraDesc::maxHorizontalFovDegrees) —
// while EVERY pick ray was unprojected through the DOCUMENT camera's UNCLAMPED
// projection (ScenePicker::screenSegment -> CameraNode::updateCameraMatrices).
// Two different frusta, one window. The error is zero at the centre of the
// frame and grows towards the edges, so it reads as "picking is subtly wrong"
// rather than "picking is off", and it bites at ANY aspect past the camera's
// own threshold: the shipped Grand Showroom's 75-degree camera crosses it at
// 1.42:1, i.e. on every ordinary monitor. The rig measured a chrome sphere
// DRAWN at x = 2829 selecting a column eleven units behind it.
//
// WHY THIS SUITE IS NOT CIRCULAR. It does not compare the fix against itself:
// it RENDERS the scene through the engine at the disputed aspect, finds where
// each object actually LANDED IN THE PIXELS, and then asks the document picker
// for the ray at that pixel. The assertion is a world-space one — the ray must
// pass through the object's centre — so it is independent of the projection
// maths on both sides, and it would fail identically if the engine's clamp
// moved instead of the document's.
//
// WHAT IS PINNED
//   1. Wide aspects (2.36:1 = 1264x536 and 5.94:1 = 3184x536, the rig's own
//      two): the ray at every object's DRAWN centroid passes through that
//      object's centre.
//   2. The defect's own signature: with the cap taken OFF the document camera
//      (i.e. the shipped behaviour before this fix) the same rays miss by
//      units, not by a pixel. Without this the suite could pass on a build
//      where the clamp never engaged at all.
//   3. THE IDENTITY NEGATIVE: at 16:9 with the default 45-degree lens the
//      projection matrix is BIT-IDENTICAL with the cap set and unset. The
//      common case must not move by one ulp — every 16:9 pixel assertion in the
//      tree depends on it.
//   4. The control: a 45-degree camera at 16:9 picks exactly, capped or not.
//   5. The cap is TRANSIENT — an authored camera never carries one, and a
//      duplicate never inherits one.
#include <QGuiApplication>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/cameralens.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/scenegraph/scene.h"
#include "viewport/freecamerapolicy.h"
#include "viewport/scenepicker.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok:   %s\n", msg);                               \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

namespace {

/// The five markers, laid across the frame so the OUTER ones are where a
/// frustum disagreement shows and the centre one is where it cannot.
struct Marker {
    const char *name;
    Vec3 pos;
    Colour emissive;
};

const Marker kMarkers[] = {
    { "far-left",  Vec3(-9.0f, 0.0f, 0.0f), Colour(4.0f, 0.0f, 0.0f) },
    { "left",      Vec3(-4.5f, 0.0f, 0.0f), Colour(0.0f, 4.0f, 0.0f) },
    { "centre",    Vec3( 0.0f, 0.0f, 0.0f), Colour(0.0f, 0.0f, 4.0f) },
    { "right",     Vec3( 4.5f, 0.0f, 0.0f), Colour(4.0f, 4.0f, 0.0f) },
    { "far-right", Vec3( 9.0f, 0.0f, 0.0f), Colour(0.0f, 4.0f, 4.0f) },
};
constexpr size_t kNumMarkers = sizeof(kMarkers) / sizeof(kMarkers[0]);

/// Which marker (if any) a pixel belongs to. The markers are the only lit thing
/// in the scene (black background, no lights, dark albedo) and each one owns a
/// distinct pattern of saturated channels, so the classification is exact
/// rather than nearest-match.
int classify(const Colour &c)
{
    const bool r = c.r > 0.5f, g = c.g > 0.5f, b = c.b > 0.5f;
    if (!r && !g && !b) return -1;
    for (size_t i = 0; i < kNumMarkers; ++i) {
        const Colour &e = kMarkers[i].emissive;
        if (r == (e.r > 0.5f) && g == (e.g > 0.5f) && b == (e.b > 0.5f)) return int(i);
    }
    return -1;
}

struct Centroid { double x = 0, y = 0; long n = 0; };

/// Where each marker actually landed in the rendered frame.
std::vector<Centroid> drawnCentroids(const Image &img)
{
    std::vector<Centroid> out(kNumMarkers);
    for (unsigned y = 0; y < img.height; ++y) {
        for (unsigned x = 0; x < img.width; ++x) {
            const int m = classify(img.at(x, y));
            if (m < 0) continue;
            out[size_t(m)].x += x; out[size_t(m)].y += y; ++out[size_t(m)].n;
        }
    }
    for (Centroid &c : out) if (c.n) { c.x /= double(c.n); c.y /= double(c.n); }
    return out;
}

/// Distance from a world point to the infinite line the pick segment lies on —
/// the whole assertion, in one number, and one that no projection convention
/// can quietly satisfy by accident.
float distancePointToRay(const iris::Vec3 &p, const iris::Vec3 &a, const iris::Vec3 &b)
{
    const iris::Vec3 d = b - a;
    const float len2 = d.lengthSquared();
    if (len2 <= 0.0f) return (p - a).length();
    const float t = iris::Vec3::dotProduct(p - a, d) / len2;
    return ((a + d * t) - p).length();
}

iris::Vec3 toIris(const Vec3 &v) { return iris::Vec3(v.x, v.y, v.z); }

}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-pick-fov-cap-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    // A document node IS an engine node (SCENEGRAPH_SPEC D2). This suite needs
    // BOTH halves — pixels from the engine, rays from the document — and
    // Ogre::Root is a singleton, so the document graph is staged onto the SAME
    // engine rather than a second headless one.
    iris::graph::setStagingScene(
        reinterpret_cast<iris::graph::SceneHandle>(engine->documentGraphScene()));

    // ---- the scene: five emissive markers on a row, nothing else ------------
    Scene *s = engine->createScene("pickfov");
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    for (size_t i = 0; i < kNumMarkers; ++i) {
        PbrParams p;
        p.albedo = Colour(0, 0, 0);
        p.emissive = kMarkers[i].emissive;
        p.roughness = 1.0f;
        const NodeId n = s->createNode();
        s->attachMesh(n, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(p));
        enginetest::setNodeScale(s, n, Vec3(1.2f, 1.2f, 1.2f));
        enginetest::setNodePosition(s, n, kMarkers[i].pos);
    }

    // The camera: the Grand Showroom's 75-degree lens, 12 units back, looking
    // straight down -Z so the markers sit on a horizontal line through the
    // frame's centre. Position and orientation are shared bit-for-bit between
    // the engine camera and the document one below.
    const Vec3 kCamPos(0.0f, 0.0f, 12.0f);
    const float kShowroomFov = 75.0f;
    const float kCap = freecam::kFreeCameraMaxHorizontalFovDegrees;

    auto documentCamera = [&](float fovDeg, float aspect, float cap) {
        auto cam = iris::CameraNode::create();
        cam->setLocalPos(toIris(kCamPos));
        cam->lookAt(iris::Vec3(0, 0, 0));
        cam->angle = fovDeg;
        cam->nearClip = 0.1f;
        cam->farClip = 1000.0f;
        cam->setAspectRatio(aspect);
        cam->setHorizontalFovCap(cap);
        cam->update(0.0f);
        return cam;
    };

    struct Case { const char *name; int w, h; };
    const Case kCases[] = {
        { "1264x536 (2.36:1, the owner's window)", 1264, 536 },
        { "3184x536 (5.94:1, the rig's wide repro)", 3184, 536 },
    };

    for (const Case &c : kCases) {
        std::printf("\n-- %s --\n", c.name);
        View *view = engine->createOffscreenView(std::string("pickfov-") + std::to_string(c.w),
                                                 unsigned(c.w), unsigned(c.h), Colour(0, 0, 0));
        if (!view) { std::printf("FAIL: offscreen view %dx%d\n", c.w, c.h); ++failures; continue; }
        view->setScene(s);

        // EXACTLY what the app pushes for its explorer (scenemirror.cpp
        // applyCamera + enginesceneviewport freeCameraFovCap).
        CameraDesc cd;
        cd.position = kCamPos;
        cd.orientation = Quat();          // identity: looking down -Z
        cd.fovDegrees = kShowroomFov;
        cd.nearClip = 0.1f;
        cd.farClip = 1000.0f;
        cd.maxHorizontalFovDegrees = kCap;
        view->setCamera(cd);

        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        Image img;
        if (!view->readPixels(img)) { std::printf("FAIL: readPixels\n"); ++failures; continue; }
        const std::vector<Centroid> drawn = drawnCentroids(img);

        const float aspect = float(c.w) / float(c.h);
        auto capped   = documentCamera(kShowroomFov, aspect, kCap);
        auto uncapped = documentCamera(kShowroomFov, aspect, 0.0f);

        // The cap must actually be doing something here, or the rest proves
        // nothing: state the two angles out loud.
        const float vCap = iris::lens::verticalFovDegForHorizontalCap(kShowroomFov, aspect, kCap);
        std::printf("   aspect %.3f: authored vfov %.1f -> capped vfov %.2f "
                    "(hfov %.1f -> %.1f)\n", double(aspect), double(kShowroomFov), double(vCap),
                    double(iris::lens::horizontalFovDeg(kShowroomFov, aspect)),
                    double(iris::lens::horizontalFovDeg(vCap, aspect)));
        CHECK(vCap < kShowroomFov - 1.0f, "the wide-aspect clamp engages at this aspect");
        // ...and that the DOCUMENT and the ENGINE compute the same clamp. Two
        // implementations (double-precision lens helpers document side, float
        // trig engine side) that must never disagree by anything that matters.
        const float vEngine = verticalFovForHorizontalCap(kShowroomFov, aspect, kCap);
        std::printf("   engine clamp %.6f vs document clamp %.6f (delta %.2e)\n",
                    double(vEngine), double(vCap), double(std::fabs(vEngine - vCap)));
        CHECK(std::fabs(vEngine - vCap) < 1e-3f,
              "the document's clamp and the engine's agree to a thousandth of a degree");

        float worstCapped = 0.0f, worstUncapped = 0.0f;
        for (size_t i = 0; i < kNumMarkers; ++i) {
            if (drawn[i].n < 50) {
                std::printf("FAIL: marker '%s' did not render (%ld px)\n", kMarkers[i].name, drawn[i].n);
                ++failures;
                continue;
            }
            const QPointF pt(drawn[i].x, drawn[i].y);
            const iris::Vec3 centre = toIris(kMarkers[i].pos);
            iris::Vec3 a, b;
            ScenePicker::screenSegment(capped, c.w, c.h, pt, a, b);
            const float dCap = distancePointToRay(centre, a, b);
            ScenePicker::screenSegment(uncapped, c.w, c.h, pt, a, b);
            const float dRaw = distancePointToRay(centre, a, b);
            std::printf("   %-10s drawn at (%7.1f,%6.1f)  ray miss: capped %.4f  uncapped %.4f\n",
                        kMarkers[i].name, drawn[i].x, drawn[i].y, double(dCap), double(dRaw));
            if (dCap > worstCapped) worstCapped = dCap;
            if (dRaw > worstUncapped) worstUncapped = dRaw;
        }
        // 0.25 world units at 12 units of range is about 40 pixels of the
        // narrow case and 40 of the wide one — far looser than the centroid
        // measurement and far tighter than the defect (which misses by units).
        CHECK(worstCapped < 0.25f, "every ray cast at a DRAWN centroid passes through that object");
        CHECK(worstUncapped > 1.0f, "the pre-fix (uncapped) ray misses by world units — the defect is real");

        engine->destroyView(view);
    }

    // ---- 3. THE IDENTITY NEGATIVE ------------------------------------------
    //
    // 16:9 with the default lens is inside the cap, so setting one must not
    // perturb a single bit of the projection. Compared as RAW BYTES, because
    // "close enough" is exactly the claim that would let a pixel suite drift.
    {
        const float aspect = 16.0f / 9.0f;
        auto plain = documentCamera(45.0f, aspect, 0.0f);
        auto withCap = documentCamera(45.0f, aspect, kCap);
        CHECK(std::memcmp(&plain->projMatrix, &withCap->projMatrix, sizeof(iris::Mat4)) == 0,
              "at 16:9 the 45-degree projection matrix is BIT-IDENTICAL with the cap set");
        CHECK(withCap->effectiveFovDegrees() == 45.0f,
              "an angle inside the cap is returned bit-for-bit, no arithmetic");
        CHECK(withCap->angle == 45.0f, "the AUTHORED angle is never rewritten by the cap");

        // ...and the same at the Showroom's own lens on a NARROW window, which
        // is the other half of "the common case does not move".
        auto plain43 = documentCamera(kShowroomFov, 4.0f / 3.0f, 0.0f);
        auto cap43 = documentCamera(kShowroomFov, 4.0f / 3.0f, kCap);
        CHECK(std::memcmp(&plain43->projMatrix, &cap43->projMatrix, sizeof(iris::Mat4)) == 0,
              "at 4:3 the 75-degree projection matrix is BIT-IDENTICAL with the cap set");
    }

    // ---- 4. the control: 45 degrees at 16:9 picks exactly either way --------
    {
        const int W = 1920, H = 1080;
        const float aspect = float(W) / float(H);
        auto plain = documentCamera(45.0f, aspect, 0.0f);
        auto withCap = documentCamera(45.0f, aspect, kCap);
        // The exact centre of the frame and a point 30% out: analytic, because
        // this case is about the two cameras agreeing with EACH OTHER.
        float worst = 0.0f;
        for (const QPointF pt : { QPointF(W * 0.5, H * 0.5), QPointF(W * 0.8, H * 0.35) }) {
            iris::Vec3 a0, b0, a1, b1;
            ScenePicker::screenSegment(plain, W, H, pt, a0, b0);
            ScenePicker::screenSegment(withCap, W, H, pt, a1, b1);
            const float d = ((b1 - a1).normalized() - (b0 - a0).normalized()).length();
            if (d > worst) worst = d;
        }
        std::printf("   16:9 / 45 deg: worst ray-direction difference %.3e\n", double(worst));
        CHECK(worst == 0.0f, "a 45-degree camera at 16:9 casts the SAME ray capped or not");
    }

    // ---- 5. the cap is transient -------------------------------------------
    {
        auto authored = iris::CameraNode::create();
        CHECK(authored->horizontalFovCap() == 0.0f,
              "a camera is born UNCAPPED — every authored camera keeps its lens");
        authored->setHorizontalFovCap(kCap);
        auto copy = authored->createDuplicate().staticCast<iris::CameraNode>();
        CHECK(copy && copy->horizontalFovCap() == 0.0f,
              "a duplicate does NOT inherit the cap (it is a host's statement, not the document's)");
    }

    engine->destroyScene(s);
    iris::graph::setStagingScene(nullptr);
    engine.reset();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
