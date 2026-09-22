// REFLECTIONS UNDER A MOVING CAMERA (PAN-SMEAR-1) — `gi.reflect_motion` and
// `gi.reflect_motion_norays`.
//
// WHAT WAS WRONG, as the owner saw it: turning the view over a glossy floor drew
// a pale stepped band down the side the camera was turning TOWARD, gone the
// frame the camera stopped. Three mechanisms, all "the reflection paths treat a
// moved camera as something else":
//
//   1. THE RAY TIER'S WARM-UP MISTOOK A NEW PIXEL FOR A NEW VIEW. A floor pixel
//      the turn has just brought on screen has no previous position to reproject
//      from, its sample count restarted at one, and the filter faded the ray in
//      from the probe over eight frames — eight columns of fade on a steady
//      pan. The same restart fired for floor revealed from behind an occluder
//      by a sideways move.
//   2. THE MARCH'S RESOLVE READ THE PREVIOUS FRAME'S COLOUR AT THIS FRAME'S
//      COORDINATE, so every screen-space reflection trailed its object by one
//      frame of camera motion.
//   3. THE RAY TIER'S MEAN VALIDATED THE SURFACE AND NOT WHAT IT REFLECTS: under
//      a sideways move the floor beside a reflection kept thirty frames of the
//      thing it used to reflect.
//
// THE FIXTURE is what the rig missed for weeks: a glossy FLOOR filling the lower
// picture at a grazing angle (not glossy objects on a matte floor), with bright
// pillars standing on it so there is something to reflect and something to be
// revealed from behind.
//
// THE NUMBER: a camera move of kMoveFrames frames, a read-back of the LAST
// moving frame, then kSettleFrames more at the SAME pose and a second read-back.
// The assertion is the mean absolute difference between the two over the floor,
// in 8-bit codes — how wrong the moving picture is against the picture the
// renderer itself settles to. Frames, never time. Measured on the RTX 4080
// SUPER, identical over three runs, with the engine fix reversed and restored
// under this same binary:
//
//                                  before the fix      after
//      yaw,   rays + march              6.561           0.712
//      truck, rays + march              8.529           1.252
//      yaw,   the march alone           0.820           0.029
//      truck, the march alone           0.781           0.017
//
// THE NO-RAYS ENTRY runs the same binary under JAHSHAKA_NO_RAY_QUERY: the
// march's reprojection is then the only thing between the floor and a one-frame
// trail, which is what makes mechanism 2 visible on its own.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static const unsigned kWidth = 384;
static const unsigned kHeight = 216;
static const int kWarmFrames = 60;     // the view is warm and the mean converged
static const int kMoveFrames = 24;
static const int kSettleFrames = 45;

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

/// Mean absolute difference, in 8-bit codes, over the rows [y0, y1) and the
/// columns [x0, x1).
static float meanDiff(const Image &a, const Image &b, unsigned x0, unsigned x1, unsigned y0,
                      unsigned y1)
{
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size())
        return 1e9f;
    double sum = 0.0;
    size_t n = 0;
    for (unsigned y = y0; y < y1 && y < a.height; ++y) {
        for (unsigned x = x0; x < x1 && x < a.width; ++x) {
            const size_t i = (size_t(y) * a.width + x) * 4u;
            for (int c = 0; c < 3; ++c) {
                sum += std::fabs(double(a.rgba[i + c]) - double(b.rgba[i + c]));
                ++n;
            }
        }
    }
    return n ? float(sum / double(n)) : 0.0f;
}

struct Pose { Vec3 pos; Vec3 target; };

/// A steady turn to the LEFT about the camera's own position, 1.5 degrees a
/// frame: what the owner's held-button pan is.
static Pose yawPose(int frame)
{
    const float a = float(frame) * 1.5f * 3.14159265f / 180.0f;
    return { Vec3(0.0f, 5.0f, 14.0f), Vec3(-14.0f * std::sin(a), 1.0f, 14.0f - 14.0f * std::cos(a)) };
}

/// A steady slide to the LEFT, 0.25 m a frame. A TRANSLATION, which is what
/// tests the depth half of a reprojection (under a pure turn every depth along
/// a pixel's ray reprojects to the same place) and what moves a reflection
/// across the surface it is on.
static Pose truckPose(int frame)
{
    const float x = -0.25f * float(frame);
    return { Vec3(x, 5.0f, 14.0f), Vec3(x, 1.0f, 0.0f) };
}

/// Runs one move and returns the floor's moving-vs-settled error; the sky's is
/// returned through `skyOut` (the control: nothing up there is a reflection).
static float runMove(Engine *e, View *view, Pose (*poseAt)(int), const char *what, float &skyOut)
{
    const Pose rest = poseAt(0);
    enginetest::testCameraLookAt(view, rest.pos, rest.target);
    render(e, kWarmFrames);
    for (int i = 1; i <= kMoveFrames; ++i) {
        const Pose p = poseAt(i);
        enginetest::testCameraLookAt(view, p.pos, p.target);
        render(e, 1);
    }
    Image moving, settled;
    if (!view->readPixels(moving)) { std::printf("FAIL: readPixels moving (%s)\n", what); ++failures; }
    render(e, kSettleFrames);
    if (!view->readPixels(settled)) { std::printf("FAIL: readPixels settled (%s)\n", what); ++failures; }
    // The floor is the lower two thirds of this framing; the top strip is sky.
    const float floorErr = meanDiff(moving, settled, 0u, kWidth, kHeight / 3u, kHeight);
    skyOut = meanDiff(moving, settled, 0u, kWidth, 0u, kHeight / 10u);
    std::printf("    %-28s floor %.3f codes   sky %.3f codes\n", what, floorErr, skyOut);
    return floorErr;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    const bool raysWanted = !getenv("JAHSHAKA_NO_RAY_QUERY");
    cfg.logFile = raysWanted ? "test-reflect-motion-ogre.log" : "test-reflect-motion-norays-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("reflectmotion", kWidth, kHeight, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("reflectmotion");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);

    const bool haveRays = e->rayQueryAvailable() && e->rayTracing();
    if (raysWanted && !haveRays) {
        std::printf("ok: this build/machine has no ray queries (available=%d wanted=%d) — "
                    "gi.reflect_motion is about the tier and skips cleanly; "
                    "gi.reflect_motion_norays covers the march\n",
                    int(e->rayQueryAvailable()), int(e->rayTracing()));
        return 0;
    }

    s->setAmbient(Colour(0.45f, 0.55f, 0.70f), Colour(0.30f, 0.30f, 0.32f));

    // THE GLOSSY FLOOR: metal at perceptual roughness 0.2 — inside the roughness
    // gate of both paths, and ABOVE the mirror ramp, so the ray tier's mean has
    // its whole memory (a true mirror takes every sample whole and cannot show
    // mechanisms 1 or 3 at all).
    {
        const NodeId floor = s->createNode();
        PbrParams p;
        p.albedo = Colour(0.85f, 0.85f, 0.85f);
        p.metalness = 1.0f;
        p.roughness = 0.2f;
        const MaterialId mat = s->createPbrMaterial(p);
        const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
        CHECK(floor && mat && mesh && s->attachMesh(floor, mesh, mat), "the glossy floor exists");
        enginetest::setNodeScale(s, floor, Vec3(120.0f, 0.2f, 120.0f));
        enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    }
    // THE PILLARS: bright, rough (outside the gate — they are reflected, they do
    // not reflect), spaced so floor shows between them.
    // THE SAME SEVEN HUES AT A PEAK RADIANCE OF 1.0, and the reason is this
    // suite's own units (VOXEL-CLIP-1, 2026-09-22, measured). They were authored
    // at 3.0, which the emissive voxel store clipped to 1.0 until ogre-patch 0087
    // made that store a float — so from 0087 on, a 3.0 pillar really does put
    // three times the radiance into the floor's reflection, and EVERY BAR IN THIS
    // SUITE IS AN ABSOLUTE CODE COUNT. All three moved by the same factor and
    // none of them is about brightness: the still control 0.460 -> 1.137 codes
    // (bar 1.0), the yaw 0.589 -> 1.294 (bar 1.5), the truck 1.070 -> 2.734
    // (bar 2.5). The subject here is the REPROJECTION — whether a moving frame
    // matches the frame the renderer settles to at the same pose — and at 3.0 the
    // floor's brighter pixels also saturate the RGBA8 readback (the on-vs-off
    // difference read 84.8 codes against 47.9), which makes a temporal-error
    // measurement on them worth less, not more. So the fixture is authored where
    // its instrument is linear and the bars keep the meaning they were calibrated
    // with. gi.voxel_emissive and gi.rt_reflect_lamp_clip are what guard the
    // store's range; this suite guards the reprojection.
    const Colour cols[7] = { Colour(1.0f, 0.066f, 0.066f), Colour(0.066f, 1.0f, 0.066f),
                             Colour(0.066f, 0.133f, 1.0f), Colour(1.0f, 0.8f, 0.066f),
                             Colour(1.0f, 0.066f, 1.0f),   Colour(0.066f, 0.933f, 0.933f),
                             Colour(1.0f, 1.0f, 1.0f) };
    for (int i = 0; i < 7; ++i) {
        const NodeId n = s->createNode();
        PbrParams p;
        p.albedo = Colour(0.05f, 0.05f, 0.05f);
        p.emissive = cols[i];
        p.roughness = 0.9f;
        const MaterialId mat = s->createPbrMaterial(p);
        const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
        if (!(n && mat && mesh && s->attachMesh(n, mesh, mat))) {
            std::printf("FAIL: pillar %d\n", i); ++failures;
        }
        enginetest::setNodeScale(s, n, Vec3(1.0f, 3.0f, 1.0f));
        enginetest::setNodePosition(s, n, Vec3(-12.0f + 4.0f * float(i), 1.5f, -2.0f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.testBoundsMin = Vec3(-30.0f, -2.0f, -30.0f);
    gi.testBoundsMax = Vec3(30.0f, 10.0f, 30.0f);
    CHECK(s->setGlobalIllumination(gi), "the voxel arm builds over the fixture");

    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    view->setPostFx(fx);

    // THE REFLECTION IS THERE AT ALL. Every bar below compares a moving frame with
    // a settled one, and two frames with NO reflection in them agree perfectly: a
    // resolve that declined everywhere (a matrix that stopped being pushed reads
    // as zero and declines every pixel) would pass this file at 0.000. So first:
    // the settled floor with the chain's reflections differs from the same floor
    // without them.
    {
        const Pose rest = yawPose(0);
        enginetest::testCameraLookAt(view, rest.pos, rest.target);
        render(e, kWarmFrames);
        Image with, without;
        view->readPixels(with);
        PostFxDesc off = fx;
        off.ssr = 0;
        view->setPostFx(off);
        render(e, kWarmFrames);
        view->readPixels(without);
        view->setPostFx(fx);
        const float present = meanDiff(with, without, 0u, kWidth, kHeight / 3u, kHeight);
        // Measured 50.7 with the ray tier (the floor mirrors the sky) and 3.4 with
        // the march alone (only the pillars are on screen to be reflected).
        CHECK_MSG(present > 1.5f,
                  "THE FLOOR REFLECTS: reflections on vs off differ by %.3f codes over the floor (> 1.5)",
                  present);
    }

    // THE CONTROL: with the camera STILL the same two read-backs must agree, or
    // the bars below measure the fixture's own restlessness and not the move.
    {
        const Pose rest = yawPose(0);
        enginetest::testCameraLookAt(view, rest.pos, rest.target);
        render(e, kWarmFrames + kMoveFrames);
        Image a, b;
        view->readPixels(a);
        render(e, kSettleFrames);
        view->readPixels(b);
        const float still = meanDiff(a, b, 0u, kWidth, kHeight / 3u, kHeight);
        // 0.509 with the ray tier on, 0.000 without: the tier's running mean has
        // a blend FLOOR (it keeps following a changing scene), so a glossy lobe
        // never stops moving by a fraction of a code. That is the noise floor the
        // moving numbers below sit on.
        const float stillBar = raysWanted ? 1.0f : 0.05f;   // the march alone is exactly still
        CHECK_MSG(still < stillBar, "THE CONTROL: a still camera's floor is settled (%.3f codes < %.2f)",
                  still, stillBar);
    }

    const RayQueryStatus rq = s->rayQueryStatus();
    std::printf("    rayQuery: available=%d enabled=%d reflect=%d\n", int(rq.available),
                int(rq.enabled), int(rq.reflect));
    if (raysWanted) CHECK(rq.reflect, "the tier is tracing this view's reflections");
    else            CHECK(!rq.enabled, "the no-rays switch really is off");

    float skyYaw = 0.0f, skyTruck = 0.0f;
    const float yawErr = runMove(e, view, yawPose, "yaw 1.5 deg/frame", skyYaw);
    const float truckErr = runMove(e, view, truckPose, "truck 0.25 m/frame", skyTruck);

    // THE BARS are about twice the fixed measurement (the run is deterministic), so
    // HALF of any one mechanism coming back reds them: mechanism 3 alone is worth
    // 3.4 codes on the slide, mechanism 2 alone 0.8. What is left with rays on is
    // the single-sample grain of a roughness-0.2 lobe in a frame that has just
    // moved — noise around the right answer, on a 0.5-code floor, gone when the
    // camera stops.
    const float yawBar = raysWanted ? 1.5f : 0.3f;
    const float truckBar = raysWanted ? 2.5f : 0.3f;
    CHECK_MSG(yawErr < yawBar,
              "A TURNING CAMERA: the moving floor is within %.1f codes of the settled one (%.3f)",
              yawBar, yawErr);
    CHECK_MSG(truckErr < truckBar,
              "A SLIDING CAMERA: the moving floor is within %.1f codes of the settled one (%.3f)",
              truckBar, truckErr);
    CHECK_MSG(skyYaw < 0.5f && skyTruck < 0.5f,
              "the sky strip is the same picture moving and settled (%.3f / %.3f) — the floor's "
              "number is about reflections",
              skyYaw, skyTruck);

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
