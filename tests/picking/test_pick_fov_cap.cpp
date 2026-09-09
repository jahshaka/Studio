// PICKING AGREES WITH THE PICTURE ON A WIDE WINDOW (owner report 2026-09-07;
// the policy re-scoped 2026-09-08, see below).
//
// THE DEFECT this suite exists to stop coming back. The engine renders a FREE
// camera through the wide-aspect FRAMING HOLD — `verticalFovForFramingAspect`
// narrows the vertical angle on a target WIDER than 16:9 so the horizontal
// extent stays the 16:9 one (OgreView.cpp, CameraDesc::framingAspect) — while
// EVERY pick ray was unprojected through the DOCUMENT camera's UNHELD
// projection (ScenePicker::screenSegment -> CameraNode::updateCameraMatrices).
// Two different frusta, one window. The error is zero at the centre of the
// frame and grows towards the edges, so it reads as "picking is subtly wrong"
// rather than "picking is off". The rig measured a chrome sphere DRAWN at
// x = 2829 selecting a column eleven units behind it.
//
// THE 2026-09-08 RE-SCOPE, which this suite now also gates. The hold first
// shipped as a FIXED 95-degree horizontal cap, which bites as a function of the
// LENS: the pre-2026-09-09 Grand Showroom's 75-degree camera (re-staged to 45° since; the number stays as the synthetic case) crossed it at 1.42:1, so
// the scene rendered at 63 degrees vertical instead of 75 on every ordinary
// monitor — everything zoomed in by 1.25x. The policy is now an ASPECT (16:9):
// at or below it the picture is the authored one, bit for bit, for EVERY lens.
// So this suite runs its 75-degree camera at 16:9 as well, where the hold must
// be the identity and picking must still land.
//
// WHY THIS SUITE IS NOT CIRCULAR. It does not compare the fix against itself:
// it RENDERS the scene through the engine at the disputed aspect, finds where
// each object actually LANDED IN THE PIXELS, and then asks the document picker
// for the ray at that pixel. The assertion is a world-space one — the ray must
// pass through the object's centre — so it is independent of the projection
// maths on both sides, and it would fail identically if the engine's hold moved
// instead of the document's.
//
// WHAT IS PINNED
//   1. 16:9 (1280x720), where the hold is the IDENTITY: the ray at every
//      object's drawn centroid passes through that object, and the held and
//      unheld cameras cast the SAME ray.
//   2. Wide aspects (2.40:1 = 1288x536, the owner's ultrawide shape, and
//      5.94:1 = 3184x536, the rig's own): same assertion, with the hold now
//      doing something.
//   3. The defect's own signature: with the hold taken OFF the document camera
//      the same rays miss by units, not by a pixel. Without this the suite
//      could pass on a build where the hold never engaged at all.
//   4. THE IDENTITY NEGATIVE: at 16:9 the projection matrix is BIT-IDENTICAL
//      with the hold set and unset, at 45 AND at the Showroom's 75. The common
//      case must not move by one ulp — every 16:9 pixel assertion in the tree
//      depends on it, and the 2026-09-08 regression was exactly this promise
//      being false.
//   5. The hold is TRANSIENT — an authored camera never carries one, and a
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
    const float kHold = freecam::kFreeCameraFramingAspect;

    auto documentCamera = [&](float fovDeg, float aspect, float hold) {
        auto cam = iris::CameraNode::create();
        cam->setLocalPos(toIris(kCamPos));
        cam->lookAt(iris::Vec3(0, 0, 0));
        cam->angle = fovDeg;
        cam->nearClip = 0.1f;
        cam->farClip = 1000.0f;
        cam->setAspectRatio(aspect);
        cam->setFramingAspect(hold);
        cam->update(0.0f);
        return cam;
    };

    struct Case { const char *name; int w, h; };
    const Case kCases[] = {
        // 16:9 FIRST, and it is not decoration: under the new policy this is
        // the case where the hold must do NOTHING, and picking must land
        // anyway. It is the case the 2026-09-08 defect broke.
        { "1280x720 (16:9, the hold is the identity here)", 1280, 720 },
        { "1288x536 (2.40:1, the owner's ultrawide)", 1288, 536 },
        { "3184x536 (5.94:1, the rig's wide repro)", 3184, 536 },
    };

    for (const Case &c : kCases) {
        std::printf("\n-- %s --\n", c.name);
        View *view = engine->createOffscreenView(std::string("pickfov-") + std::to_string(c.w),
                                                 unsigned(c.w), unsigned(c.h), Colour(0, 0, 0));
        if (!view) { std::printf("FAIL: offscreen view %dx%d\n", c.w, c.h); ++failures; continue; }
        view->setScene(s);

        // EXACTLY what the app pushes for its explorer (scenemirror.cpp
        // applyCamera + enginesceneviewport freeCameraFramingAspect).
        CameraDesc cd;
        cd.position = kCamPos;
        cd.orientation = Quat();          // identity: looking down -Z
        cd.fovDegrees = kShowroomFov;
        cd.nearClip = 0.1f;
        cd.farClip = 1000.0f;
        cd.framingAspect = kHold;
        view->setCamera(cd);

        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        Image img;
        if (!view->readPixels(img)) { std::printf("FAIL: readPixels\n"); ++failures; continue; }
        const std::vector<Centroid> drawn = drawnCentroids(img);

        const float aspect = float(c.w) / float(c.h);
        auto held   = documentCamera(kShowroomFov, aspect, kHold);
        auto unheld = documentCamera(kShowroomFov, aspect, 0.0f);
        const bool engages = aspect > kHold;

        // State the two angles out loud: below 16:9 the hold must be the
        // identity, above it it must actually be doing something. The rest of
        // the case proves nothing without this line.
        const float vHeld = iris::lens::verticalFovDegForFramingAspect(kShowroomFov, aspect, kHold);
        std::printf("   aspect %.3f: authored vfov %.1f -> rendered vfov %.2f "
                    "(hfov %.1f -> %.1f)\n", double(aspect), double(kShowroomFov), double(vHeld),
                    double(iris::lens::horizontalFovDeg(kShowroomFov, aspect)),
                    double(iris::lens::horizontalFovDeg(vHeld, aspect)));
        if (engages)
            CHECK(vHeld < kShowroomFov - 1.0f, "the framing hold engages past 16:9");
        else
            CHECK(vHeld == kShowroomFov,
                  "at 16:9 the framing hold is the identity: the authored 75 degrees is rendered");
        // ...and that the DOCUMENT and the ENGINE compute the same angle. Two
        // implementations (double-precision lens helpers document side, float
        // trig engine side) that must never disagree by anything that matters.
        const float vEngine = verticalFovForFramingAspect(kShowroomFov, aspect, kHold);
        std::printf("   engine %.6f vs document %.6f (delta %.2e)\n",
                    double(vEngine), double(vHeld), double(std::fabs(vEngine - vHeld)));
        CHECK(std::fabs(vEngine - vHeld) < 1e-4f,
              "the document's framing hold and the engine's agree to 1e-4 degrees");

        float worstHeld = 0.0f, worstUnheld = 0.0f;
        for (size_t i = 0; i < kNumMarkers; ++i) {
            if (drawn[i].n < 50) {
                std::printf("FAIL: marker '%s' did not render (%ld px)\n", kMarkers[i].name, drawn[i].n);
                ++failures;
                continue;
            }
            const QPointF pt(drawn[i].x, drawn[i].y);
            const iris::Vec3 centre = toIris(kMarkers[i].pos);
            iris::Vec3 a, b;
            ScenePicker::screenSegment(held, c.w, c.h, pt, a, b);
            const float dHeld = distancePointToRay(centre, a, b);
            ScenePicker::screenSegment(unheld, c.w, c.h, pt, a, b);
            const float dRaw = distancePointToRay(centre, a, b);
            std::printf("   %-10s drawn at (%7.1f,%6.1f)  ray miss: held %.4f  unheld %.4f\n",
                        kMarkers[i].name, drawn[i].x, drawn[i].y, double(dHeld), double(dRaw));
            if (dHeld > worstHeld) worstHeld = dHeld;
            if (dRaw > worstUnheld) worstUnheld = dRaw;
        }
        // 0.25 world units at 12 units of range is about 40 pixels of the
        // narrow case and 40 of the wide one — far looser than the centroid
        // measurement and far tighter than the defect (which misses by units).
        CHECK(worstHeld < 0.25f, "every ray cast at a DRAWN centroid passes through that object");
        if (engages)
            CHECK(worstUnheld > 1.0f,
                  "the pre-fix (unheld) ray misses by world units — the defect is real");
        else
            CHECK(worstUnheld == worstHeld,
                  "at 16:9 the held and unheld cameras cast exactly the same rays");

        engine->destroyView(view);
    }

    // ---- 4. THE IDENTITY NEGATIVE ------------------------------------------
    //
    // At or below 16:9 the hold does nothing, so setting one must not perturb a
    // single bit of the projection — at ANY lens, which is the promise the
    // 95-degree cap broke. Compared as RAW BYTES, because "close enough" is
    // exactly the claim that would let a pixel suite drift.
    {
        const float ordinary[] = { 4.0f/3.0f, 3.0f/2.0f, 16.0f/10.0f, 16.0f/9.0f };
        const float lenses[]   = { 45.0f, 60.0f, 70.0f, kShowroomFov, 90.0f };
        bool identical = true;
        for (float lens : lenses) {
            for (float a : ordinary) {
                auto plain = documentCamera(lens, a, 0.0f);
                auto free  = documentCamera(lens, a, kHold);
                if (std::memcmp(&plain->projMatrix, &free->projMatrix, sizeof(iris::Mat4)) != 0 ||
                    free->effectiveFovDegrees() != lens || free->angle != lens) {
                    identical = false;
                    std::printf("   MOVED: lens %.0f at aspect %.3f (effective %.4f)\n",
                                double(lens), double(a), double(free->effectiveFovDegrees()));
                }
            }
        }
        CHECK(identical,
              "4:3 / 3:2 / 16:10 / 16:9 projections are BIT-IDENTICAL with the hold set, "
              "for lenses 45..90 — including the Showroom's 75 (the 2026-09-08 defect)");
    }

    // ---- 5. the control: 45 degrees at 16:9 picks exactly either way --------
    {
        const int W = 1920, H = 1080;
        const float aspect = float(W) / float(H);
        auto plain = documentCamera(45.0f, aspect, 0.0f);
        auto free  = documentCamera(45.0f, aspect, kHold);
        // The exact centre of the frame and a point 30% out: analytic, because
        // this case is about the two cameras agreeing with EACH OTHER.
        float worst = 0.0f;
        for (const QPointF pt : { QPointF(W * 0.5, H * 0.5), QPointF(W * 0.8, H * 0.35) }) {
            iris::Vec3 a0, b0, a1, b1;
            ScenePicker::screenSegment(plain, W, H, pt, a0, b0);
            ScenePicker::screenSegment(free, W, H, pt, a1, b1);
            const float d = ((b1 - a1).normalized() - (b0 - a0).normalized()).length();
            if (d > worst) worst = d;
        }
        std::printf("   16:9 / 45 deg: worst ray-direction difference %.3e\n", double(worst));
        CHECK(worst == 0.0f, "a 45-degree camera at 16:9 casts the SAME ray held or not");
    }

    // ---- 6. the hold is transient -------------------------------------------
    {
        auto authored = iris::CameraNode::create();
        CHECK(authored->framingAspect() == 0.0f,
              "a camera is born UNHELD — every authored camera keeps its lens");
        authored->setFramingAspect(kHold);
        auto copy = authored->createDuplicate().staticCast<iris::CameraNode>();
        CHECK(copy && copy->framingAspect() == 0.0f,
              "a duplicate does NOT inherit the hold (it is a host's statement, not the document's)");
    }

    engine->destroyScene(s);
    iris::graph::setStagingScene(nullptr);
    engine.reset();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
