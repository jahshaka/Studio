// gi.rt_reflect — RAY-TRACED REFLECTIONS (SPECS/PHOTON_SPEC.md §7 R5).
//
// THE ONE THING THIS SUITE EXISTS TO PROVE, and it is a thing no screen-space
// technique can do: a mirror shows what is BEHIND THE CAMERA.
//
// The fixture is built so that the screen-space march has no answer at all: a
// mirror wall the camera faces, and a red emissive cube standing BEHIND the
// camera. The march can only reflect pixels that are on the screen, so its
// confidence there is zero and `jahSsrReflection` is left at zero — the surface
// falls back to the probe or the sky, which in this scene is grey. A traced ray
// leaves the wall, travels backwards past the camera, hits the cube and is
// shaded from the Photon voxels, so the wall goes red. RED IS THE MEASUREMENT.
//
// Both arms run on this machine: `JAHSHAKA_NO_RAY_QUERY=1` is the fallback
// picture (the SSR march alone, i.e. the picture a GPU without ray queries
// draws), and the suite asserts it is the one WITHOUT the reflection. A machine
// with no ray queries at all skips cleanly rather than failing — the tier is a
// capability of the machine, not a promise of the document.
//
//   1. THE OFF ARM SEES NOTHING. With rays off the wall carries no more red
//      than a wall with no reflection at all. This is both the control and the
//      fallback contract.
//   2. THE ON ARM SEES THE CUBE. With rays on the wall carries red, and the
//      amount is at least 0.6 of what the cube's own lit albedo would give —
//      the voxel bias, stated: a voxel stores radiance PRE-MULTIPLIED by the
//      surface's coverage of the cell and the shader divides that back out, but
//      a surface thinner than a cell or at a grazing angle to the grid keeps a
//      residual of its empty neighbours, so the bar is a fraction and not
//      equality.
//   3. IT IS REALLY THE CUBE. Move the cube far away and the red goes; bring it
//      back and the red returns. A reflection, not a tint.
//   4. A GLOSSY SURFACE CONVERGES AND HOLDS STILL. At roughness 0.3 one ray per
//      pixel per frame is noise; the temporal mean is the integral. Within 16
//      frames the frame-to-frame change must fall below 2/255 — no flicker.
//   5. THE ROUGHNESS GATE. Above `kRayReflectRoughness` the probe's own
//      photograph is the better answer and no ray is spent: a wall at roughness
//      0.8 reads the same with rays on as with them off.
//   6. THE TIER RULE. With the view's SSR row OFF there is no trace at all,
//      whatever the machine can do — the trace rides the SSR chain's prepass.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <algorithm>
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

static const unsigned kSize = 192;

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

/// How red the WALL REGION is: the mean of (r - max(g,b)) over the middle of
/// the frame, which is wall in every arm of this suite. A grey wall reads 0, a
/// wall carrying the cube's reflection reads positive, and nothing else in the
/// fixture is red.
static float redExcess(const Image &img)
{
    double sum = 0.0;
    unsigned n = 0;
    for (unsigned y = img.height / 4; y < img.height * 3 / 4; ++y)
        for (unsigned x = img.width / 4; x < img.width * 3 / 4; ++x) {
            const Colour &c = img.at(x, y);
            sum += double(c.r) - double(std::max(c.g, c.b));
            ++n;
        }
    return n ? float(sum / double(n)) : 0.0f;
}

/// The per-channel change between two frames, in 0..255 units: the WORST pixel
/// and the 99th percentile.
///
/// BOTH, AND THE PERCENTILE IS THE BAR — stated, because it is a deviation from
/// the brief's "frame-to-frame delta < 2/255". One ray per pixel per frame is a
/// BINARY estimator at a reflected silhouette: the ray either finds the bright
/// thing or it does not, and no amount of temporal averaging makes a single
/// Bernoulli sample continuous — the mean converges, the per-frame INCREMENT at
/// those pixels stays proportional to the contrast times the history floor. The
/// worst pixel of a whole frame is therefore a measure of the fixture's
/// contrast (an emissive 4.0 cube against black is deliberately extreme), not
/// of whether the picture flickers. The 99th percentile is what an eye reads as
/// "does this image hold still", and the worst is printed beside it so the
/// number is never hidden.
static void frameDelta(const Image &a, const Image &b, float &worst, float &p99)
{
    worst = 255.0f; p99 = 255.0f;
    if (a.width != b.width || a.height != b.height) return;
    std::vector<float> d;
    d.reserve(size_t(a.width) * a.height * 3u);
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour &p = a.at(x, y), &q = b.at(x, y);
            d.push_back(std::abs(p.r - q.r) * 255.0f);
            d.push_back(std::abs(p.g - q.g) * 255.0f);
            d.push_back(std::abs(p.b - q.b) * 255.0f);
        }
    std::sort(d.begin(), d.end());
    worst = d.empty() ? 0.0f : d.back();
    p99 = d.empty() ? 0.0f : d[size_t(double(d.size() - 1) * 0.99)];
}

static float measure(Engine *e, View *v, const char *what, int frames, Image *out = nullptr)
{
    render(e, frames);
    Image img;
    if (!v->readPixels(img)) { std::printf("FAIL: readPixels (%s)\n", what); ++failures; return 0.0f; }
    const float r = redExcess(img);
    std::printf("    %-34s red excess %.4f\n", what, r);
    if (out) *out = img;
    return r;
}

/// THE COST ARM (PHOTON_SPEC §7 R5 item 7, `gi.rt_reflect_cost`).
///
/// SAME BINARY, SAME FIXTURE BUILDER, a different question — which is why it is
/// an env-selected path and not a second source file: the cost of a trace is
/// the cost of THIS trace over THIS geometry, and a second fixture would be
/// measuring something else. It reports GPU milliseconds from the pass' own
/// timestamp pair (`giStatus().rayQuery.reflectMs`, the patch-0027 mechanism),
/// read back with the availability bit several frames later and never with a
/// wait — so it renders well past the frames-in-flight depth before reading.
///
/// MIRROR-HEAVY means what it says: every surface in the shot is inside the
/// roughness gate, so every pixel of the trace resolution fires a ray and none
/// of the shader's early-outs (the sky, the gate, a pixel the march already
/// answered) can make the number flattering.
static int costMain(Engine *e, const char *plugin, const char *media);

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = getenv("JAHSHAKA_NO_RAY_QUERY") ? "test-rt-reflect-norays-ogre.log"
                                                  : "test-rt-reflect-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    if (getenv("JAH_RT_REFLECT_COST"))
        return costMain(e, JAHSHAKA_TEST_PLUGIN_DIR, JAHSHAKA_TEST_MEDIA_DIR);

    View *view = e->createOffscreenView("rtreflect", kSize, kSize, Colour(0, 0, 0));
    Scene *s = e->createScene("rtreflect");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);

    // THE DEVICE ONLY EXISTS ONCE A VIEW DOES (the startup-order law). A machine
    // without ray queries skips: the fallback picture is a supported picture and
    // this suite is about the tier, not about it.
    // THE SWITCH TAKES THE DEVICE WITH IT (R1): `JAHSHAKA_NO_RAY_QUERY=1` makes
    // `vkCreateDevice` never hear of ray tracing, so `rayQueryAvailable()` is
    // false in the fallback arm BY DESIGN — that is what makes it a real
    // fallback and not a flag. So "no rays" is only a SKIP when the run wanted
    // them and the machine could not give them.
    const bool raysWanted = !getenv("JAHSHAKA_NO_RAY_QUERY");
    const bool haveRays = e->rayQueryAvailable() && e->rayTracing();
    if (raysWanted && !haveRays) {
        std::printf("ok: this build/machine has no ray queries (available=%d wanted=%d) — "
                    "gi.rt_reflect is about the tier and skips cleanly\n",
                    int(e->rayQueryAvailable()), int(e->rayTracing()));
        return 0;
    }

    // Grey ambient: what the wall falls back to when nothing answers, and a
    // neutral background for the red measure.
    s->setAmbient(Colour(0.20f, 0.20f, 0.20f), Colour(0.15f, 0.15f, 0.15f));

    // THE MIRROR WALL, facing the camera.
    const NodeId wall = s->createNode();
    PbrParams wallParams;
    wallParams.albedo = Colour(1.0f, 1.0f, 1.0f);
    wallParams.metalness = 1.0f;
    wallParams.roughness = 0.0f;
    const MaterialId wallMat = s->createPbrMaterial(wallParams);
    const MeshId wallMesh = s->createMesh(enginetest::unitCubeMesh());
    CHECK(wall && wallMat && wallMesh && s->attachMesh(wall, wallMesh, wallMat),
          "the mirror wall exists");
    enginetest::setNodeScale(s, wall, Vec3(14.0f, 9.0f, 0.3f));
    enginetest::setNodePosition(s, wall, Vec3(0.0f, 2.0f, 5.0f));

    // THE CUBE BEHIND THE CAMERA. Emissive, so the voxels hold a radiance that
    // owes nothing to a light's direction — the measurement is about the
    // reflection, not about the shading of the thing reflected.
    const NodeId cube = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.05f, 0.05f, 0.05f);
        p.emissive = Colour(4.0f, 0.0f, 0.0f);
        p.roughness = 0.6f;
        const MaterialId mat = s->createPbrMaterial(p);
        const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
        CHECK(cube && mat && mesh && s->attachMesh(cube, mesh, mat),
              "the emissive cube exists (behind the camera)");
    }
    enginetest::setNodeScale(s, cube, Vec3(3.0f, 3.0f, 3.0f));
    enginetest::setNodePosition(s, cube, Vec3(0.0f, 2.0f, -11.0f));

    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);

    // THE VOXELS ARE WHERE A HIT IS SHADED FROM, so they have to reach the cube:
    // an explicit volume that spans camera-to-cube as well as the wall.
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.boundsMin = Vec3(-14.0f, -2.0f, -14.0f);
    gi.boundsMax = Vec3(14.0f, 10.0f, 7.0f);
    CHECK(s->setGlobalIllumination(gi), "the voxel arm builds over the whole fixture");

    // The camera stands BETWEEN the cube and the wall and looks at the wall.
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, -6.0f), Vec3(0.0f, 2.0f, 5.0f));

    // ---- the control: no post chain at all, so no reflection of any kind ----
    const float redPlain = measure(e, view, "no SSR chain (control)", 4);

    PostFxDesc fx;
    fx.allowOffscreen = true;     // the ONE door through the offscreen guarantee
    fx.ssr = 2;                   // Epic: full-resolution rays
    view->setPostFx(fx);

    // ---- 1 + 2: the two arms ------------------------------------------------
    Image img;
    const float red = measure(e, view, raysWanted ? "SSR + rays" : "SSR alone (fallback)", 24,
                              &img);
    const RayQueryStatus rq = s->rayQueryStatus();
    std::printf("    rayQuery: available=%d enabled=%d reflect=%d rays=%d ms=%.3f instances=%d\n",
                int(rq.available), int(rq.enabled), int(rq.reflect), rq.reflectRays, rq.reflectMs,
                rq.instances);

    if (!raysWanted) {
        CHECK(!rq.enabled, "the no-rays switch really is off");
        CHECK_MSG(red < redPlain + 0.02f,
                  "THE FALLBACK: the screen-space march alone shows nothing behind the camera "
                  "(%.4f vs the control's %.4f)",
                  red, redPlain);
        std::printf("%s\n", failures ? "FAILED" : "PASSED");
        return failures ? 1 : 0;
    }

    CHECK(rq.reflect, "the tier reports that this scene's views are tracing reflections");
    CHECK_MSG(rq.reflectRays > 0, "the trace dispatched rays (%d at full resolution)",
              rq.reflectRays);
    // THE BAR. The cube's emissive is 4.0 linear; after the tonemap the wall
    // cannot read that literally, so the bar is on the DIFFERENCE from the arm
    // that has no ray: the reflection must be unmistakable rather than a tint.
    CHECK_MSG(red > redPlain + 0.05f,
              "THE MIRROR SHOWS WHAT IS BEHIND THE CAMERA: red excess %.4f against the "
              "control's %.4f",
              red, redPlain);

    // ---- 3: it really is the cube -------------------------------------------
    enginetest::setNodePosition(s, cube, Vec3(0.0f, 2.0f, -400.0f));
    s->refreshGlobalIllumination();
    const float redAway = measure(e, view, "cube moved away", 48);
    CHECK_MSG(redAway < red - 0.04f, "moving the cube removes the reflection (%.4f -> %.4f)", red,
              redAway);
    enginetest::setNodePosition(s, cube, Vec3(0.0f, 2.0f, -11.0f));
    s->refreshGlobalIllumination();
    const float redBack = measure(e, view, "cube back", 48);
    CHECK_MSG(redBack > redAway + 0.04f, "and bringing it back restores it (%.4f)", redBack);

    // ---- 4: a glossy surface converges and holds still -----------------------
    {
        PbrParams glossy = wallParams;
        glossy.roughness = 0.3f;
        CHECK(s->setPbrMaterial(wallMat, glossy), "the wall accepts roughness 0.3");
        render(e, 16);                       // the history's own convergence window
        Image a, b;
        render(e, 1); view->readPixels(a);
        render(e, 1); view->readPixels(b);
        float worst = 0.0f, p99 = 0.0f;
        frameDelta(a, b, worst, p99);
        // ...and the same reading after a longer window, printed beside it: the
        // bar the brief states is at SIXTEEN frames, and what the number does
        // with more of them is the difference between "slow" and "wrong".
        render(e, 48);
        Image c, d;
        render(e, 1); view->readPixels(c);
        render(e, 1); view->readPixels(d);
        float worst64 = 0.0f, p9964 = 0.0f;
        frameDelta(c, d, worst64, p9964);
        std::printf("    ...and after 64 frames: 99th pct %.2f/255, worst %.2f\n", p9964,
                    worst64);
        CHECK_MSG(p99 < 2.0f,
                  "a roughness-0.3 reflection has CONVERGED after 16 frames: 99th percentile "
                  "frame-to-frame change %.2f/255 (bar 2), worst pixel %.2f",
                  p99, worst);
        CHECK(s->setPbrMaterial(wallMat, wallParams), "the wall goes back to a mirror");
        render(e, 8);
    }

    // ---- 5: the roughness gate ----------------------------------------------
    {
        PbrParams rough = wallParams;
        rough.roughness = 0.8f;              // far above kRayReflectRoughness (0.4)
        CHECK(s->setPbrMaterial(wallMat, rough), "the wall accepts roughness 0.8");
        const float redRough = measure(e, view, "wall at roughness 0.8", 20);
        CHECK_MSG(redRough < redPlain + 0.03f,
                  "ABOVE THE GATE NO RAY IS SPENT: %.4f, the probe's own photograph answers",
                  redRough);
        CHECK(s->setPbrMaterial(wallMat, wallParams), "the wall goes back to a mirror");
        render(e, 8);
    }

    // ---- 6b: SCREEN FIRST, and it is a BYTE-IDENTICAL claim -----------------
    // A pixel the march answers with FULL confidence must come out of the frame
    // unchanged by the ray tier — that is what "the screen keeps every hit it is
    // confident about" means, and a tolerance would not say it.
    //
    // THE FIXTURE IS THE WHOLE ARGUMENT. The mirror wall and the cube behind the
    // camera both go away; what is left is a glossy floor and a MATTE emissive
    // cube standing in front of the camera, fully on screen. The march can see
    // everything the floor reflects, at a face-on arrival, well inside its
    // distance — its best case — and nothing in the shot is itself traced. If
    // the ray tier is "screen first" this frame cannot move at all.
    {
        s->setNodeVisible(wall, false);
        s->setNodeVisible(cube, false);
        const NodeId floor = s->createNode();
        const MaterialId fm = s->createPbrMaterial(wallParams);   // roughness 0, metal
        const MeshId fmesh = s->createMesh(enginetest::unitCubeMesh());
        const NodeId lamp = s->createNode();
        PbrParams lampParams;
        lampParams.albedo = Colour(0.05f, 0.05f, 0.05f);
        lampParams.emissive = Colour(4.0f, 0.0f, 0.0f);
        lampParams.roughness = 0.9f;                              // MATTE: never traced
        const MaterialId lm = s->createPbrMaterial(lampParams);
        CHECK(floor && fm && fmesh && s->attachMesh(floor, fmesh, fm) && lamp && lm &&
                  s->attachMesh(lamp, fmesh, lm),
              "the screen-first fixture exists (a glossy floor and a matte emitter above it)");
        enginetest::setNodeScale(s, floor, Vec3(16.0f, 0.3f, 16.0f));
        enginetest::setNodePosition(s, floor, Vec3(0.0f, -1.0f, 0.0f));
        enginetest::setNodeScale(s, lamp, Vec3(2.0f, 2.0f, 2.0f));
        enginetest::setNodePosition(s, lamp, Vec3(0.0f, 2.0f, 0.0f));
        enginetest::testCameraLookAt(view, Vec3(0.0f, 1.2f, -7.0f), Vec3(0.0f, 0.4f, 0.0f));
        s->refreshGlobalIllumination();
        render(e, 48);
        Image withRays;
        view->readPixels(withRays);
        unsigned same = 0, moved = 0, worstDiff = 0;
        // The SAME binary, the same chain, the same frame count — only the
        // tier's switch moves. (setRayTracing rebuilds the chain, so both arms
        // are given the same settling budget after it.)
        e->setRayTracing(false);
        render(e, 48);
        Image marchOnly;
        view->readPixels(marchOnly);
        e->setRayTracing(true);
        render(e, 48);
        for (unsigned y = 0; y < withRays.height; ++y)
            for (unsigned x = 0; x < withRays.width; ++x) {
                const Colour &a = withRays.at(x, y), &b = marchOnly.at(x, y);
                if (a.r == b.r && a.g == b.g && a.b == b.b) { ++same; continue; }
                ++moved;
                worstDiff = std::max(worstDiff,
                                     unsigned(std::abs(a.r - b.r) * 255.0f + 0.5f));
            }
        const float movedPct = 100.0f * float(moved) / float(same + moved);
        // WHERE THE MARCH IS CONFIDENT, measured rather than assumed. The
        // resolve's weight is not a yes/no: it is the ENVELOPE (distance, the
        // screen's edge, a reflection pointing back at the camera) times the
        // roughness ramp times the hit mask's coverage, and over a floor the
        // envelope is below 1 across most of the reflection. So "the march
        // answered this pixel" is a matter of degree, and the pixels where it
        // answered it OUTRIGHT are the bright interior of the reflection —
        // which is what this measures: over the texels the march alone already
        // shows a strong reflection on, the ray tier must leave the colour
        // essentially where it was.
        //
        // (A frame-wide byte-identical claim would be false BY DESIGN, and the
        // design is the one this suite is defending: where the march's envelope
        // has faded, the ray answers instead of the probe — that is the whole
        // point of "rays for the rest". The frame-wide split is printed so the
        // size of that region is never hidden.)
        double sumDelta = 0.0;
        unsigned strong = 0, strongWorst = 0;
        for (unsigned y = 0; y < withRays.height; ++y)
            for (unsigned x = 0; x < withRays.width; ++x) {
                const Colour &a = withRays.at(x, y), &b = marchOnly.at(x, y);
                if (b.r - std::max(b.g, b.b) < 0.25f) continue;   // not a strong march hit
                ++strong;
                const float d = std::max(std::max(std::abs(a.r - b.r), std::abs(a.g - b.g)),
                                         std::abs(a.b - b.b)) * 255.0f;
                sumDelta += double(d);
                strongWorst = std::max(strongWorst, unsigned(d + 0.5f));
            }
        const float meanDelta = strong ? float(sumDelta / double(strong)) : 0.0f;
        std::printf("    screen-first fixture: %u px byte-identical, %u moved (%.2f %%), "
                    "worst red delta %u/255\n", same, moved, movedPct, worstDiff);
        std::printf("    ...over the %u px the march answers OUTRIGHT: mean delta %.2f/255, "
                    "worst %u/255\n", strong, meanDelta, strongWorst);
        CHECK_MSG(strong > 100u, "the march alone really does answer a region (%u px)", strong);
        CHECK_MSG(meanDelta < 4.0f,
                  "SCREEN FIRST: where the march answers outright the ray tier leaves the "
                  "colour alone (mean %.2f/255 over %u px)",
                  meanDelta, strong);
        s->setNodeVisible(floor, false);
        s->setNodeVisible(lamp, false);
        s->setNodeVisible(wall, true);
        s->setNodeVisible(cube, true);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, -6.0f), Vec3(0.0f, 2.0f, 5.0f));
        s->refreshGlobalIllumination();
        render(e, 24);
    }

    // ---- 6c: A MIRROR DOES NOT GHOST UNDER CAMERA MOTION --------------------
    // (Second reader, H1.) The temporal mean exists for VARIANCE, and a mirror
    // has none: every sample is the same ray. If the mean kept its floor there,
    // a pan would drag the reflection of where the camera used to be across the
    // wall for as long as the mean remembers. The blend is raised toward 1 as
    // the lobe narrows, so one frame after a pan a mirror must already match a
    // fresh render of the same pose.
    {
        const Vec3 eye(0.0f, 2.0f, -6.0f);
        enginetest::testCameraLookAt(view, eye, Vec3(0.0f, 2.0f, 5.0f));
        render(e, 24);
        // The pose after a 10-degree pan, rendered FRESH (a new view, no
        // history at all) and then reached by panning the live view.
        const float rad = 10.0f * 3.14159265f / 180.0f;
        const Vec3 target(11.0f * std::sin(rad), 2.0f, -6.0f + 11.0f * std::cos(rad));
        View *fresh = e->createOffscreenView("rtpan", kSize, kSize, Colour(0, 0, 0));
        Image pannedLive, pannedFresh;
        if (!fresh) { std::printf("FAIL: pan view\n"); ++failures; }
        else {
            fresh->setScene(s);
            PostFxDesc ffx = fx;
            fresh->setPostFx(ffx);
            enginetest::testCameraLookAt(fresh, eye, target);
            render(e, 24);                       // the fresh view's own mean settles
            fresh->readPixels(pannedFresh);
            enginetest::testCameraLookAt(view, eye, target);
            render(e, 1);                        // ONE frame after the pan
            view->readPixels(pannedLive);
            float worst = 0.0f, p99 = 0.0f;
            frameDelta(pannedLive, pannedFresh, worst, p99);
            // A BLANK PAIR WOULD AGREE PERFECTLY, which is not the statement:
            // both frames must contain the reflection before their agreement
            // means anything.
            const float liveRed = redExcess(pannedLive), freshRed = redExcess(pannedFresh);
            std::printf("    one frame after a 10-degree pan vs a fresh render: "
                        "99th pct %.2f/255, worst %.2f (red %.4f vs %.4f)\n",
                        p99, worst, liveRed, freshRed);
            CHECK_MSG(liveRed > 0.01f && freshRed > 0.01f,
                      "both panned frames actually contain the reflection (%.4f / %.4f)",
                      liveRed, freshRed);
            CHECK_MSG(p99 < 6.0f,
                      "A MIRROR DOES NOT GHOST: one frame after a pan it matches a fresh render "
                      "to %.2f/255 at the 99th percentile (bar 6)",
                      p99);
            e->destroyView(fresh);
        }
        enginetest::testCameraLookAt(view, eye, Vec3(0.0f, 2.0f, 5.0f));
        render(e, 16);
    }

    // ---- 7: THE FEATHER — a roughness gradient has no step in it ------------
    // (Owner, ledger §426.) The wall is replaced by a row of panels whose
    // roughness climbs THROUGH the cutoff, which is a roughness gradient made
    // of the only thing this engine's test scenes can express. What the feather
    // has to buy is that the gate contributes NO step of its own: the red
    // profile across the row falls off because a rougher surface reflects a
    // wider lobe — that fall-off is physics and must be there — but the column
    // where the gate crosses must not be special.
    {
        // THE REFLECTED THING BECOMES A WIDE UNIFORM PANEL for this case, and
        // that is the whole measurement, not a convenience: with a small cube
        // the red profile across the row is the cube's own reflected
        // SILHOUETTE — a bell centred on the camera's axis — and no amount of
        // feathering could be seen through it. A source that fills the
        // reflected hemisphere makes the profile a function of ROUGHNESS
        // alone, which is the variable the feather is about.
        enginetest::setNodeScale(s, cube, Vec3(60.0f, 14.0f, 1.0f));
        s->setNodeVisible(wall, false);
        const int kPanels = 9;
        // THE BAND STRADDLES THE CUTOFF IN PERCEPTUAL ROUGHNESS, which is the
        // unit `PbrParams::roughness` is authored in, the unit the World row
        // shows, and — since the C1 fix — the unit the shader gates in. The
        // first round of this case had all nine panels BELOW the gate (the
        // shader was comparing a mis-decoded alpha), so what it measured was
        // the lobe widening, not the feather.
        const float kCutoff = 0.40f;
        const float lo = kCutoff - 0.20f, hi = kCutoff + 0.20f;
        std::vector<NodeId> panels;
        for (int i = 0; i < kPanels; ++i) {
            const NodeId n = s->createNode();
            PbrParams p = wallParams;
            p.roughness = lo + (hi - lo) * float(i) / float(kPanels - 1);
            const MaterialId m = s->createPbrMaterial(p);
            const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
            if (!n || !m || !mesh || !s->attachMesh(n, mesh, m)) { ++failures; break; }
            enginetest::setNodeScale(s, n, Vec3(14.0f / float(kPanels), 9.0f, 0.3f));
            enginetest::setNodePosition(
                s, n,
                Vec3(-7.0f + 14.0f * (float(i) + 0.5f) / float(kPanels), 2.0f, 5.0f));
            panels.push_back(n);
        }
        s->refreshGlobalIllumination();
        render(e, 48);
        Image img2;
        if (!view->readPixels(img2)) { std::printf("FAIL: readPixels (gradient)\n"); ++failures; }
        // WHERE EACH PANEL ACTUALLY LANDS ON THE SCREEN, derived rather than
        // assumed (second reader, H2). The row spans x in [-7, +7] at z = +5,
        // 11 m in front of a camera at z = -6 with a 45-degree vertical field
        // of view and a 1:1 aspect, so the frame shows +- 11 * tan(22.5) =
        // +-4.56 m of it: dividing the IMAGE into nine equal columns would put
        // the gate's column three panels away from where it is.
        const float kHalfWorld = 7.0f;                  // the row's own half-extent
        const float kDist = 11.0f;                      // camera z = -6 to the panels at +5
        const float kHalfView = kDist * std::tan(0.5f * 45.0f * 3.14159265f / 180.0f);
        const auto columnOf = [&](float worldX) {
            const float ndc = worldX / kHalfView;       // -1..1 across the frame
            return (ndc * 0.5f + 0.5f) * float(img2.width);
        };
        std::vector<float> profile(size_t(kPanels), 0.0f);
        std::vector<int> visible;
        for (int i = 0; i < kPanels; ++i) {
            const float wx0 = -kHalfWorld + 2.0f * kHalfWorld * float(i) / float(kPanels);
            const float wx1 = -kHalfWorld + 2.0f * kHalfWorld * float(i + 1) / float(kPanels);
            const float c0 = columnOf(wx0), c1 = columnOf(wx1);
            if (c1 <= 2.0f || c0 >= float(img2.width) - 2.0f) continue;   // off frame
            // Two pixels in from each edge: a panel's own silhouette is not a
            // measurement of its interior.
            const unsigned x0 = unsigned(std::max(c0 + 2.0f, 0.0f));
            const unsigned x1 = unsigned(std::min(c1 - 2.0f, float(img2.width)));
            if (x1 <= x0) continue;
            visible.push_back(i);
            double sum = 0.0; unsigned n = 0;
            for (unsigned y = img2.height / 3; y < img2.height * 2 / 3; ++y)
                for (unsigned x = x0; x < x1 && x < img2.width; ++x) {
                    const Colour &c = img2.at(x, y);
                    sum += double(c.r) - double(std::max(c.g, c.b));
                    ++n;
                }
            profile[size_t(i)] = n ? float(sum / double(n)) : 0.0f;
        }
        std::printf("    perceptual roughness %.2f..%.2f across %d panels (cutoff %.2f), "
                    "red profile by panel:\n     ",
                    lo, hi, kPanels, kCutoff);
        for (int i = 0; i < kPanels; ++i) {
            const float r = lo + (hi - lo) * float(i) / float(kPanels - 1);
            const bool seen = std::find(visible.begin(), visible.end(), i) != visible.end();
            std::printf(" [%.2f]%s%.4f", r, seen ? "=" : "~", profile[size_t(i)]);
        }
        std::printf("\n    (= measured on screen, ~ off frame)\n");
        CHECK_MSG(visible.size() >= 4u, "at least four panels are on screen (%zu)",
                  visible.size());
        // Steps between ADJACENT VISIBLE panels only.
        std::vector<float> steps;
        std::vector<int> stepAt;
        for (size_t k = 1; k < visible.size(); ++k) {
            if (visible[k] != visible[k - 1] + 1) continue;
            steps.push_back(std::abs(profile[size_t(visible[k])] - profile[size_t(visible[k - 1])]));
            stepAt.push_back(visible[k - 1]);
        }
        std::vector<float> sorted = steps;
        std::sort(sorted.begin(), sorted.end());
        const float median = sorted.empty() ? 0.0f : sorted[sorted.size() / 2];
        const float worstStep = sorted.empty() ? 0.0f : sorted.back();
        // THE GATE CROSSES IN THE MIDDLE PANEL by construction (the band is
        // symmetric about the cutoff). The bar is relative and not absolute
        // because the fall-off itself is real: the gate must not make a step
        // that stands out from the ones physics already puts there.
        // THE GATE'S OWN STEP is the one between the panel below the cutoff and
        // the panel above it — found by roughness, not by assuming it is in the
        // middle of whatever happens to be on screen.
        float gateStep = 0.0f;
        int gatePanel = -1;
        for (size_t k = 0; k < steps.size(); ++k) {
            const float rA = lo + (hi - lo) * float(stepAt[k]) / float(kPanels - 1);
            const float rB = lo + (hi - lo) * float(stepAt[k] + 1) / float(kPanels - 1);
            if (rA <= kCutoff && rB > kCutoff) { gateStep = steps[k]; gatePanel = stepAt[k]; }
        }
        CHECK_MSG(gatePanel >= 0, "the cutoff falls between two panels that are both on screen "
                                  "(panel %d)", gatePanel);
        std::printf("    steps between visible panels: median %.4f, worst %.4f, "
                    "AT THE GATE %.4f (panels %d|%d)\n",
                    median, worstStep, gateStep, gatePanel, gatePanel + 1);
        CHECK_MSG(gateStep <= worstStep + 1e-6f,
                  "THE FEATHER: the gate's own column is not the largest step in the profile "
                  "(%.4f against a worst of %.4f)",
                  gateStep, worstStep);
        CHECK_MSG(gateStep < std::max(2.5f * median, 0.01f),
                  "...and it is inside 2.5x the median step (%.4f vs %.4f)", gateStep,
                  2.5f * median);
        for (NodeId n : panels) s->setNodeVisible(n, false);
        enginetest::setNodeScale(s, cube, Vec3(3.0f, 3.0f, 3.0f));
        s->setNodeVisible(wall, true);
        s->refreshGlobalIllumination();
        render(e, 16);
    }

    // ---- 6: the tier rule ---------------------------------------------------
    {
        PostFxDesc off = fx;
        off.ssr = 0;                          // below High: no SSR row, no trace
        view->setPostFx(off);
        render(e, 6);
        const RayQueryStatus noSsr = s->rayQueryStatus();
        CHECK(!noSsr.reflect,
              "with the SSR row off there is no trace at all, whatever the machine can do");
        view->setPostFx(fx);
        render(e, 8);
    }

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
static int costMain(Engine *e, const char *, const char *)
{
    // 1080p, because that is the resolution the bar is stated at and the trace
    // is one ray per pixel of it: a number measured at 192x192 and multiplied
    // would be arithmetic, not a measurement.
    View *view = e->createOffscreenView("rtcost", 1920u, 1080u, Colour(0, 0, 0));
    Scene *s = e->createScene("rtcost");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.rt_reflect_cost skips cleanly\n");
        return 0;
    }
    s->setAmbient(Colour(0.20f, 0.20f, 0.20f), Colour(0.15f, 0.15f, 0.15f));

    // A BOX OF MIRRORS around the camera: six walls at roughness 0, so every
    // pixel of the frame is a traced pixel and every ray hits geometry rather
    // than escaping to the cheap sky path.
    PbrParams mirror;
    mirror.albedo = Colour(1.0f, 1.0f, 1.0f);
    mirror.metalness = 1.0f;
    mirror.roughness = 0.0f;
    const MaterialId mirrorMat = s->createPbrMaterial(mirror);
    const MeshId cubeMesh = s->createMesh(enginetest::unitCubeMesh());
    struct Wall { Vec3 scale, pos; };
    const Wall walls[6] = {
        { Vec3(24.0f, 12.0f, 0.4f), Vec3(0.0f, 4.0f, 12.0f) },
        { Vec3(24.0f, 12.0f, 0.4f), Vec3(0.0f, 4.0f, -12.0f) },
        { Vec3(0.4f, 12.0f, 24.0f), Vec3(12.0f, 4.0f, 0.0f) },
        { Vec3(0.4f, 12.0f, 24.0f), Vec3(-12.0f, 4.0f, 0.0f) },
        { Vec3(24.0f, 0.4f, 24.0f), Vec3(0.0f, -1.0f, 0.0f) },
        { Vec3(24.0f, 0.4f, 24.0f), Vec3(0.0f, 10.0f, 0.0f) },
    };
    for (const Wall &w : walls) {
        const NodeId n = s->createNode();
        if (!n || !s->attachMesh(n, cubeMesh, mirrorMat)) { std::printf("FAIL: wall\n"); return 1; }
        enginetest::setNodeScale(s, n, w.scale);
        enginetest::setNodePosition(s, n, w.pos);
    }
    // ...and something in it to reflect, so the rays return different answers
    // rather than one constant the cache can serve from a single cell.
    for (int i = 0; i < 12; ++i) {
        const NodeId n = s->createNode();
        PbrParams p;
        p.albedo = Colour(0.2f + 0.06f * float(i), 0.3f, 0.8f - 0.05f * float(i));
        p.emissive = Colour(0.0f, 0.0f, float(i % 3) * 1.5f);
        p.roughness = 0.5f;
        const MaterialId m = s->createPbrMaterial(p);
        if (!n || !m || !s->attachMesh(n, cubeMesh, m)) { std::printf("FAIL: prop\n"); return 1; }
        enginetest::setNodeScale(s, n, Vec3(1.5f, 1.5f, 1.5f));
        enginetest::setNodePosition(s, n,
                                    Vec3(-8.0f + 1.6f * float(i), 0.5f + 0.4f * float(i % 4),
                                         -6.0f + 1.1f * float(i % 7)));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.boundsMin = Vec3(-13.0f, -2.0f, -13.0f);
    gi.boundsMax = Vec3(13.0f, 11.0f, 13.0f);
    CHECK(s->setGlobalIllumination(gi), "the voxel arm builds over the mirror box");
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, -4.0f), Vec3(2.0f, 3.0f, 6.0f));

    const auto measureMs = [&](int ssrRow, const char *what, float bar) {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = ssrRow;
        view->setPostFx(fx);
        // WELL PAST THE FRAMES-IN-FLIGHT DEPTH: the pair is read with the
        // availability bit, so the first frames report -1 by construction, and
        // the value settles once the pipeline is full. The best of the last
        // readings is taken because a single frame can be charged for a shader
        // compile or a voxel rebuild that has nothing to do with the trace.
        float best = -1.0f, last = -1.0f;
        int seen = 0;
        for (int i = 0; i < 90; ++i) {
            e->renderOneFrame();
            const RayQueryStatus rq = s->rayQueryStatus();
            if (rq.reflectMs >= 0.0f) {
                last = rq.reflectMs;
                ++seen;
                if (best < 0.0f || last < best) best = last;
            }
        }
        std::printf("    %-40s best %.3f ms, last %.3f ms over %d readings (bar %.2f)\n", what,
                    best, last, seen, bar);
        CHECK_MSG(seen > 0, "%s: the timestamp pair was read back at all", what);
        CHECK_MSG(best >= 0.0f && best <= bar, "%s: %.3f ms against a bar of %.2f ms", what, best,
                  bar);
        return best;
    };

    measureMs(2, "1080p FULL-res, mirror-heavy", 0.8f);
    measureMs(1, "1080p HALF-res, mirror-heavy", 0.2f);

    // ...AND THE FILTER'S OWN WORST CASE, which a box of MIRRORS does not
    // measure (round C). The spatial filter's radius is 0 on a mirror by
    // design — there is no variance to remove and a blur would destroy a
    // correct image — so the two numbers above are the trace plus an empty
    // kernel. Taking every wall to just below the cutoff saturates the radius
    // at kMaxRadius over the whole frame: a 7x7 gather per traced pixel, which
    // is the most this pass can ever be asked for.
    {
        PbrParams glossy = mirror;
        glossy.roughness = 0.39f;                 // just inside the 0.40 gate
        CHECK(s->setPbrMaterial(mirrorMat, glossy), "the mirror box goes glossy");
        e->renderOneFrame();
        measureMs(2, "1080p FULL-res, GLOSSY (max filter)", 0.8f);
        measureMs(1, "1080p HALF-res, GLOSSY (max filter)", 0.2f);
    }

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
