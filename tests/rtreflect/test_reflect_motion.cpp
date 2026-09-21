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
    const Colour cols[7] = { Colour(3.0f, 0.2f, 0.2f), Colour(0.2f, 3.0f, 0.2f), Colour(0.2f, 0.4f, 3.0f),
                             Colour(3.0f, 2.4f, 0.2f), Colour(3.0f, 0.2f, 3.0f), Colour(0.2f, 2.8f, 2.8f),
                             Colour(3.0f, 3.0f, 3.0f) };
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
        CHECK_MSG(still < 1.0f, "THE CONTROL: a still camera's floor is settled (%.3f codes < 1.0)", still);
    }

    const RayQueryStatus rq = s->rayQueryStatus();
    std::printf("    rayQuery: available=%d enabled=%d reflect=%d\n", int(rq.available),
                int(rq.enabled), int(rq.reflect));
    if (raysWanted) CHECK(rq.reflect, "the tier is tracing this view's reflections");
    else            CHECK(!rq.enabled, "the no-rays switch really is off");

    float skyYaw = 0.0f, skyTruck = 0.0f;
    const float yawErr = runMove(e, view, yawPose, "yaw 1.5 deg/frame", skyYaw);
    const float truckErr = runMove(e, view, truckPose, "truck 0.25 m/frame", skyTruck);

    // THE BARS sit between the two measurements in the header, nearer the fixed
    // one. What is left with rays on is the single-sample grain of a
    // roughness-0.2 lobe in a frame that has just moved — noise around the right
    // answer, on a 0.5-code floor, gone when the camera stops.
    const float yawBar = raysWanted ? 3.0f : 0.3f;
    const float truckBar = raysWanted ? 4.0f : 0.3f;
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
