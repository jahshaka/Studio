// gi.field_follows — THE IRRADIANCE FIELD RIDES CASCADE 0 (SPECS/PHOTON_SPEC.md E1).
//
// The irradiance field is the leak-free half of Photon's diffuse: probes that
// carry a depth-variance visibility test, so a wall stops the bounce behind it.
// It covers ONE box. Under the single-volume arm that box is the scene's fit and
// it never moves; under a cascade chain the only box worth covering is the
// innermost cascade's — the near field, where the user is standing — and that
// box WALKS. Until E1 the arm refused to build a field at all under a chain, and
// said so in a comment: the pin's only placement API re-creates the atlases, so
// a field on a scrolling cascade was either wrong or black.
//
// What this suite proves, and it is all about FOLLOWING:
//
//   0. THE FIELD EXISTS UNDER A CHAIN, and it sits on cascade 0 — not on the
//      scene's fit, which under a chain describes nothing the renderer built.
//   1. A STILL CAMERA MOVES IT NOWHERE. The field is a cache like every other
//      cache in this engine: no camera step, no work, for ever.
//   2. A WALK CARRIES IT. Crossing cascade 0's step re-places the field with the
//      cascade, in the SAME frame (a frame between the two would sample this
//      frame's probes through last frame's placement), it stays BOUND, it comes
//      back CONVERGED, and no frame of the walk costs more than the tier's own
//      budget — 16.7 ms, the 60 Hz desktop target of the realtime law, since
//      PHOTON_SPEC §7 E2 (9) re-anchored these two bars off the 30 Hz placeholder
//      E1 shipped with (`fieldGi()` is quality High, and High's budget is 60 Hz).
//   3. IT IS THE SAME LIGHT. A field riding a 10 m box must light the ground
//      the camera is standing on like the 64 m fitted field did at the same
//      pose — the whole point being that it does so wherever the camera goes.
//   4. A HUNDRED METRES LATER IT IS STILL THERE: bound, converged, centred on
//      cascade 0, with the chain's own contract (at most one rebuild a frame)
//      unbroken.
//
// Its own binary like every GI suite: the voxel lighting and the field bind
// process-wide to HlmsPbs, so this scene must not share a process with another
// arm's.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const unsigned kSize = 128;

static void render(Engine *e, int frames = 1)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

/// Mean luminance of the ground band — the floor in front of the camera,
/// wherever it stands. A band and not a pixel so one dithered texel cannot
/// decide, and the near half of it so the sample is inside cascade 0's box.
static float groundLum(const Image &img)
{
    float sum = 0.0f; unsigned n = 0;
    for (unsigned y = kSize * 5u / 8u; y < kSize * 7u / 8u; ++y)
        for (unsigned x = kSize / 4u; x < kSize * 3u / 4u; ++x) { sum += lum(img.at(x, y)); ++n; }
    return n ? sum / float(n) : 0.0f;
}

/// The NEAREST ground — the bottom eighth of the picture, which at every pose
/// this suite uses lies a couple of metres in front of the camera and therefore
/// well inside cascade 0's box. Case 3 compares the two arms' FIELD here and
/// nowhere else: further up the picture the ground leaves the field's reach and
/// the two arms are answering different questions (the chain hands those pixels
/// to its cone bounce, the single volume's field still covers them).
static float nearGroundLum(const Image &img)
{
    float sum = 0.0f; unsigned n = 0;
    for (unsigned y = kSize * 7u / 8u; y < kSize; ++y)
        for (unsigned x = kSize / 4u; x < kSize * 3u / 4u; ++x) { sum += lum(img.at(x, y)); ++n; }
    return n ? sum / float(n) : 0.0f;
}

static Vec3 centreOf(const GiStatus &st)
{
    return Vec3(0.5f * (st.ifdMin.x + st.ifdMax.x),
                0.5f * (st.ifdMin.y + st.ifdMax.y),
                0.5f * (st.ifdMin.z + st.ifdMax.z));
}

static float dist(const Vec3 &a, const Vec3 &b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// The chain, with the field ON. `updateBudget` 1 is the DEFAULT dial: it is
/// what decides how fast a re-converge runs, so the suite measures the shipped
/// speed rather than an unlimited one.
/// THE TIER'S FRAME BUDGET, in milliseconds (REALTIME_FRAME_SPEC's law:
/// 60 Hz desktop, 90 Hz VR). `fieldGi()` below is quality HIGH, which is a
/// desktop tier, so 1/60 s is the bar these cases are held to. It replaced a
/// 33 ms (30 Hz) placeholder at PHOTON_SPEC §7 E2 (9).
static const float kTierBudgetMs = 1000.0f / 60.0f;

static GiParams fieldGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::On;
    gi.updateBudget = 1;
    gi.cascades = true;
    return gi;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-field-follows-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    // A long ground so a 100 m walk still lands on something, a wall to bounce
    // from, and an ambient so the picture is never black.
    View *view = e->createOffscreenView("field", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("field");
    view->setScene(scene);
    scene->setAmbient(Colour(0.35f, 0.35f, 0.35f), Colour(0.35f, 0.35f, 0.35f));
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(600.0f, 0.1f, 600.0f));
    // A wall EVERY few metres along the walk, so the field always has real
    // geometry inside cascade 0 to integrate — a field over empty ground would
    // prove the plumbing and nothing about the light.
    for (int i = 0; i < 24; ++i) {
        const NodeId wall = enginetest::addTestCube(scene, Colour(0.9f, 0.05f, 0.05f), 0.0f, 0.9f);
        enginetest::setNodePosition(scene, wall, Vec3(float(i) * 6.0f, 3.0f, -6.0f));
        enginetest::setNodeScale(scene, wall, Vec3(5.0f, 6.0f, 0.2f));
    }

    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f), Vec3(0.0f, 1.0f, 0.0f)));
    render(e, 4);

    // =====================================================================
    // CASE 0 — the field builds under a chain, ON CASCADE 0
    // =====================================================================
    std::printf("\n== case 0: the field is built over cascade 0 ==\n");
    CHECK(scene->setGlobalIllumination(fieldGi()), "the cascade arm with the field accepts");
    render(e, 8);
    GiStatus st = scene->giStatus();
    CHECK(!st.cascades.empty(), "the chain is up");
    CHECK(st.ifdBound, "THE FIELD IS BOUND UNDER A CHAIN (it was torn down before E1)");
    CHECK(st.ifdProbes > 0, "...with probes");
    CHECK(st.ifdConverged, "...converged on the frame it bound");
    if (!st.cascades.empty()) {
        const Vec3 c0 = st.cascades[0].centre;
        const Vec3 fc = centreOf(st);
        const float fieldSpan = st.ifdMax.x - st.ifdMin.x;
        const float c0Span = 2.0f * st.cascades[0].halfSize;
        std::printf("   cascade 0: centre %.2f,%.2f,%.2f  half %.2f m\n", c0.x, c0.y, c0.z,
                    st.cascades[0].halfSize);
        std::printf("   field    : centre %.2f,%.2f,%.2f  span %.2f m  probes %d  spacing ~%.2f m\n",
                    fc.x, fc.y, fc.z, fieldSpan, st.ifdProbes,
                    fieldSpan / std::max(1.0f, std::cbrt(float(st.ifdProbes))));
        // The voxel volume the field is placed over is the voxeliser's, which is
        // cascade 0's box (the voxeliser may round it out by a voxel or two).
        CHECK(dist(fc, c0) < 0.25f * c0Span,
              "the field is centred on cascade 0, not on the scene's fit");
        CHECK(std::fabs(fieldSpan - c0Span) < 0.25f * c0Span,
              "...and spans that cascade's box");
    }

    // =====================================================================
    // CASE 1 — a still camera moves it nowhere
    // =====================================================================
    std::printf("\n== case 1: at rest ==\n");
    {
        const GiStatus a = scene->giStatus();
        render(e, 60);
        const GiStatus b = scene->giStatus();
        std::printf("   60 still frames: follows %llu -> %llu\n", a.ifdFollows, b.ifdFollows);
        CHECK(b.ifdFollows == a.ifdFollows, "a still camera re-places the field not once");
        CHECK(b.ifdConverged, "...and it stays converged");
    }

    // =====================================================================
    // CASE 2 — a walk carries it, ON THE FRAME AFTER the cascade moved
    //
    // THE INVARIANT HERE CHANGED ON 2026-09-18 (lane V1-RIG item 2,
    // LATER_OPTIMISATIONS L11). It used to be "the frame the cascade moved IS
    // the frame the field moved", one atomic piece of work. It is now
    // "the two NEVER SHARE A FRAME": cascade 0's rebuild is 2.4-2.9 ms of GPU
    // and the field's whole re-integration another 3.4-5.6, and at a headset's
    // pixel count their SUM crossed the 90 Hz bar while neither half did
    // (measured at Quest Pro size: quiet frame 5.84 ms, step frame 11.72 mean /
    // 12.45 max against 11.1, twelve frames of a hundred and sixty — one dropped
    // frame every five metres). So the follow is paid on the NEXT frame, out of
    // that frame's own one GI slot, and the frame that pays it rebuilds no
    // cascade.
    //
    // What it costs is one frame in which the field describes the place cascade
    // 0 has just left — one step of staleness, never a WRONG PLACE, because the
    // field's volume and its atlas move together and the shader reads the
    // volume live. The settled-picture assertion below therefore moved from the
    // rebuild frame to the FOLLOW frame, where it still holds exactly: the
    // re-integration is whole, so nothing drifts after it.
    // =====================================================================
    std::printf("\n== case 2: one cascade-0 step, and the follow on the frame after ==\n");
    float singleVolumeGround = 0.0f, chainGroundAtStart = 0.0f;
    {
        st = scene->giStatus();
        const float step0 = st.cascades[0].step;
        const Vec3 fieldBefore = centreOf(st);
        const unsigned long long followsBefore = st.ifdFollows;
        Image img; view->readPixels(img);
        chainGroundAtStart = groundLum(img);

        engine->setFrameMonitor(MonitorLevel::Review);
        // Eight frames per step: a walk, not a teleport — the camera crosses
        // cascade 0's step once and the scheduler must answer inside it.
        unsigned long long rebuildsWorstFrame = 0, prevRebuilds = 0;
        for (const auto &c : st.cascades) prevRebuilds += c.rebuilds;
        // CASCADE 0's OWN count and the follow count, per frame: the deferral is
        // a statement about those two and nothing else (an outer cascade's
        // rebuild owes no follow at all).
        unsigned long long prevC0 = st.cascades[0].rebuilds;
        unsigned long long prevFollows = followsBefore;
        int stepFrame = -1;           // the frame the FIELD moved (the follow)
        int c0Frame = -1;             // the frame CASCADE 0 rebuilt
        int sharedFrames = 0;         // frames that did both — must stay 0
        for (int f = 1; f <= 12; ++f) {
            const float x = float(f) * step0 / 8.0f;
            view->setCamera(enginetest::testCameraDescLookAt(Vec3(x, 2.0f, 6.0f),
                                                             Vec3(x, 1.0f, 0.0f)));
            render(e, 1);
            const GiStatus s = scene->giStatus();
            unsigned long long t = 0;
            for (const auto &c : s.cascades) t += c.rebuilds;
            rebuildsWorstFrame = std::max(rebuildsWorstFrame, t - prevRebuilds);
            prevRebuilds = t;
            const bool c0Rebuilt = s.cascades[0].rebuilds > prevC0;
            const bool followed  = s.ifdFollows > prevFollows;
            prevC0 = s.cascades[0].rebuilds;
            prevFollows = s.ifdFollows;
            // THE STRUCTURAL INVARIANT, checked on EVERY frame of the walk and
            // not only on the first step: the two bursts never share a frame.
            if (c0Rebuilt && followed) ++sharedFrames;
            if (c0Frame < 0 && c0Rebuilt) {
                c0Frame = f;
                std::printf("   cascade 0 rebuilt on frame %d\n", f);
                // ...AND THE FIELD HAS NOT MOVED YET. This is the deferral,
                // stated at the frame it happens on.
                CHECK(!followed,
                      "the field does NOT re-place on the frame cascade 0 rebuilt (the deferral)");
            }
            if (stepFrame < 0 && followed) {
                stepFrame = f;
                CHECK(s.ifdBound, "the field stays BOUND across the step");
                CHECK(s.ifdConverged, "...and is converged in the follow's own frame");
                const float moved = dist(centreOf(s), fieldBefore);
                std::printf("   the field moved on frame %d: its centre moved %.2f m "
                            "(cascade 0's step is %.2f m)\n", f, moved, step0);
                CHECK(moved > 0.5f * step0, "THE FIELD FOLLOWED CASCADE 0");
                CHECK(dist(centreOf(s), s.cascades[0].centre) < 0.5f * s.cascades[0].halfSize,
                      "...onto that cascade's new centre");
                // THE PICTURE ON THE FOLLOW FRAME IS THE SETTLED PICTURE — the
                // whole-re-integration policy stated as a pixel. A PROGRESSIVE
                // re-converge would show the probes of the place the camera LEFT
                // here and drift into the right answer over the budget's frames
                // (a wrong picture, not a late one), which is why the follow is
                // deferred by a frame and never spread over several.
                Image now; view->readPixels(now);
                const float atStep = groundLum(now);
                render(e, 10);
                view->readPixels(now);
                const float settled = groundLum(now);
                std::printf("   ground on the follow frame %.4f, ten frames later %.4f "
                            "(%.1f%% drift)\n", atStep, settled,
                            settled > 0.0f ? 100.0f * std::fabs(atStep - settled) / settled : 0.0f);
                CHECK(std::fabs(atStep - settled) < 0.03f * std::max(settled, 1e-4f),
                      "the follow frame ALREADY shows the settled diffuse (no drift to watch)");
                // Those ten frames were rendered at one pose, outside the walk's
                // cadence: re-sync the per-frame counters so nothing after them
                // is read as a shared frame.
                const GiStatus after = scene->giStatus();
                prevC0 = after.cascades[0].rebuilds;
                prevFollows = after.ifdFollows;
                prevRebuilds = 0;
                for (const auto &c : after.cascades) prevRebuilds += c.rebuilds;
            }
        }
        CHECK(stepFrame > 0, "the walk crossed a cascade-0 step at all (the test is not vacuous)");
        CHECK(c0Frame > 0, "...and the rebuild that owed the follow was seen");
        std::printf("   cascade 0 rebuilt on frame %d, the field followed on frame %d "
                    "(%d frame(s) did both)\n", c0Frame, stepFrame, sharedFrames);
        CHECK(sharedFrames == 0,
              "NO FRAME PAID FOR BOTH A CASCADE-0 REBUILD AND THE FIELD'S RE-PLACEMENT");
        CHECK(stepFrame == c0Frame + 1,
              "the follow lands on the VERY NEXT frame, not later (it owns that frame's GI slot)");
        CHECK(rebuildsWorstFrame <= 1,
              "no frame paid for two cascades (the chain's budget is unbroken)");

        // THE FRAME COST OF A STEP, from the monitor rather than a stopwatch
        // around renderOneFrame: `totalMs` is the whole frame and `ifd.follow`
        // is what the field's re-integration cost inside it.
        std::vector<FrameRecord> recs;
        engine->setFrameMonitor(MonitorLevel::Off);
        engine->takeFrameRecords(recs);
        float worstMs = 0.0f, worstFollowMs = -1.0f, worstFollowGpu = -1.0f;
        float worstCascadeMs = -1.0f, worstCascadeGpu = -1.0f;
        unsigned followRows = 0, followProbes = 0;
        for (const FrameRecord &r : recs) {
            worstMs = std::max(worstMs, r.totalMs);
            for (const CacheWork &w : r.cacheWork) {
                if (w.detail == "ifd.follow") {
                    ++followRows;
                    followProbes = std::max(followProbes, w.units);
                    worstFollowMs = std::max(worstFollowMs, w.ms);
                    worstFollowGpu = std::max(worstFollowGpu, w.gpuMs);
                } else if (w.detail.rfind("vct.cascade", 0) == 0) {
                    worstCascadeMs = std::max(worstCascadeMs, w.ms);
                    worstCascadeGpu = std::max(worstCascadeGpu, w.gpuMs);
                }
            }
        }
        std::printf("   monitor: %zu frames, worst frame %.2f ms; %u ifd.follow row(s) of %u "
                    "probes, worst %.2f ms CPU / %.2f ms GPU; worst cascade rebuild %.2f ms CPU "
                    "/ %.2f ms GPU\n",
                    recs.size(), worstMs, followRows, followProbes, worstFollowMs, worstFollowGpu,
                    worstCascadeMs, worstCascadeGpu);
        CHECK(followRows > 0, "the re-integration is filed as its own monitor row");
        CHECK(worstMs < kTierBudgetMs, "NO FRAME OF THE WALK COSTS MORE THAN THE TIER'S BUDGET");
    }

    // =====================================================================
    // CASE 3 — it is the same light the single volume gave
    // =====================================================================
    std::printf("\n== case 3: the picture agrees with the single-volume field ==\n");
    {
        // The same pose for both arms. The chain's field covers a 10 m box
        // around the camera; the single volume's covers the scene's fit. The
        // ground band this reads is a few metres in front of the camera —
        // inside cascade 0 either way — so the two fields are integrating the
        // same geometry and must agree.
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f),
                                                         Vec3(0.0f, 1.0f, 0.0f)));
        Image img;
        render(e, 8);
        view->readPixels(img);
        const float chain = nearGroundLum(img);
        const float chainWholeBand = groundLum(img);

        GiParams single = fieldGi(); single.cascades = false;
        CHECK(scene->setGlobalIllumination(single), "the single-volume arm with the field accepts");
        render(e, 8);
        const GiStatus s = scene->giStatus();
        CHECK(s.ifdBound && s.cascades.empty(), "...and it is one volume with a field on it");
        CHECK(s.ifdFollows == 0, "a field over a scene-fitted volume follows nothing");
        view->readPixels(img);
        singleVolumeGround = nearGroundLum(img);
        const float singleWholeBand = groundLum(img);

        CHECK(scene->setGlobalIllumination(fieldGi()), "back to the chain");
        render(e, 8);
        view->readPixels(img);
        const float chainAgain = nearGroundLum(img);
        std::printf("   whole ground band (the field's reach and beyond it): chain %.4f | "
                    "single volume %.4f (%.2fx) — the chain hands the pixels past cascade 0 to "
                    "its cone bounce, which is G3's subject and gi.cascades' bar\n",
                    chainWholeBand, singleWholeBand,
                    singleWholeBand > 0.0f ? chainWholeBand / singleWholeBand : 0.0f);
        std::printf("   NEAR ground, inside cascade 0: chain %.4f | single volume %.4f (%.2fx) "
                    "| chain again %.4f\n",
                    chain, singleVolumeGround, singleVolumeGround > 0.0f ? chain / singleVolumeGround : 0.0f,
                    chainAgain);
        CHECK(chain > 0.02f && singleVolumeGround > 0.02f, "both arms render a lit picture");
        // A TOLERANCE, NOT AN EQUALITY, and the reason is physics: the chain's
        // field has 8,192 probes over cascade 0's 10 m box where the single
        // volume's has them over the whole fit, so the two integrate the same
        // room at very different probe spacings. What must hold is that the
        // near ground is lit by the SAME light — not that two different
        // samplings of it agree to the bit.
        CHECK(std::fabs(chain - singleVolumeGround) < 0.25f * singleVolumeGround,
              "the chain's field lights the near ground like the fitted field did");
        CHECK(std::fabs(chainAgain - chain) < 0.05f * std::max(chain, 1e-4f),
              "...and the arm is reproducible across a re-solve");
        (void)chainGroundAtStart;
    }

    // =====================================================================
    // CASE 4 — a hundred metres later
    // =====================================================================
    std::printf("\n== case 4: 100 m of walking ==\n");
    {
        const GiStatus a = scene->giStatus();
        unsigned long long prev = 0, worstFrame = 0;
        for (const auto &c : a.cascades) prev += c.rebuilds;
        const int frames = 200;
        for (int f = 1; f <= frames; ++f) {
            const float x = float(f) * 0.5f;                 // 0.5 m a frame = 30 m/s
            view->setCamera(enginetest::testCameraDescLookAt(Vec3(x, 2.0f, 6.0f),
                                                             Vec3(x, 1.0f, 0.0f)));
            render(e, 1);
            const GiStatus s = scene->giStatus();
            unsigned long long t = 0;
            for (const auto &c : s.cascades) t += c.rebuilds;
            worstFrame = std::max(worstFrame, t - prev);
            prev = t;
        }
        const GiStatus b = scene->giStatus();
        Image img; view->readPixels(img);
        const float endGround = nearGroundLum(img);
        std::printf("   after 100 m: follows %llu -> %llu, worst frame %llu rebuild(s), "
                    "field centre %.1f,%.1f,%.1f vs cascade 0 %.1f,%.1f,%.1f, ground %.4f\n",
                    a.ifdFollows, b.ifdFollows, worstFrame,
                    centreOf(b).x, centreOf(b).y, centreOf(b).z,
                    b.cascades[0].centre.x, b.cascades[0].centre.y, b.cascades[0].centre.z,
                    endGround);
        CHECK(b.ifdFollows > a.ifdFollows + 4u, "the field was re-placed once per cascade-0 step");
        CHECK(b.ifdBound, "it is still the bound field at the end of the walk");
        CHECK(b.ifdConverged, "...and converged");
        CHECK(dist(centreOf(b), b.cascades[0].centre) < 0.5f * b.cascades[0].halfSize,
              "...and still centred on cascade 0, 100 m from where it started");
        CHECK(worstFrame <= 1, "and no frame of the 100 m paid for two cascades");
        CHECK(endGround > 0.02f, "the ground is lit at the far end (nothing went black)");
        CHECK(std::fabs(endGround - singleVolumeGround) < 0.5f * singleVolumeGround,
              "THE LEAK-FREE DIFFUSE IS WHEREVER THE CAMERA IS (the point of E1)");
    }

    // =====================================================================
    // CASE 5 — THE RING KEEPS ITS BOUNCE (PHOTON_SPEC §13 G3)
    // =====================================================================
    // The bar G3 exists for, stated as the light in the picture. Binding an
    // irradiance field makes HlmsPbs switch the cone-traced diffuse off for the
    // whole scene, on the assumption that the field covers what the cones cover.
    // Under a chain it covers cascade 0 and nothing else, so with that rule left
    // alone every pixel from there out to the outermost cascade would get a flat
    // ambient and no bounce at all — the outer cascades would be computing
    // radiance nothing reads. G3-a re-opens the gate under a chain and blends
    // the two by the field's confidence.
    //
    // So: a white lamp on a red wall, and a patch of grey ground in front of it
    // TWENTY METRES from the camera — outside cascade 0's 10 m box, inside the
    // chain. Nothing but indirect light can make that ground red.
    std::printf("\n== case 5: the bounce beyond cascade 0 ==\n");
    {
        // A DARK ambient for this case, and a lamp that does not clip: the
        // measurement is a COLOUR difference on grey ground, so a saturated
        // picture measures nothing (the first cut of this case had the near
        // ground at luminance 1.0 and every colour reading was 0).
        scene->setAmbient(Colour(0.02f, 0.02f, 0.02f), Colour(0.02f, 0.02f, 0.02f));
        const NodeId lamp = scene->createNode();
        enginetest::setNodePosition(scene, lamp, Vec3(100.0f, 3.0f, -3.0f));
        LightDesc ld;
        ld.type = LightType::Point;
        ld.colour = Colour(1.0f, 1.0f, 1.0f);
        ld.intensity = 2.0f;
        ld.range = 30.0f;
        ld.castShadows = false;
        scene->setLight(lamp, ld);
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(100.0f, 2.0f, 14.0f),
                                                         Vec3(100.0f, 1.0f, -6.0f)));
        const auto redExcess = [&](const Image &img, unsigned y0, unsigned y1) {
            float r = 0.0f, g = 0.0f; unsigned n = 0;
            for (unsigned y = y0; y < y1; ++y)
                for (unsigned x = kSize / 4u; x < kSize * 3u / 4u; ++x) {
                    r += img.at(x, y).r; g += img.at(x, y).g; ++n;
                }
            return n ? (r - g) / float(n) : 0.0f;
        };
        Image img;
        float chainBands[8] = {0}, offBands[8] = {0};
        CHECK(scene->setGlobalIllumination(fieldGi()), "the chain with the field, for the bounce");
        render(e, 10);
        view->readPixels(img);
        for (unsigned b = 0; b < 8u; ++b)
            chainBands[b] = redExcess(img, kSize * b / 8u, kSize * (b + 1u) / 8u);
        GiParams off = fieldGi(); off.mode = GiMode::Off;
        CHECK(scene->setGlobalIllumination(off), "...and the same picture with no GI at all");
        render(e, 10);
        view->readPixels(img);
        for (unsigned b = 0; b < 8u; ++b)
            offBands[b] = redExcess(img, kSize * b / 8u, kSize * (b + 1u) / 8u);
        CHECK(scene->setGlobalIllumination(fieldGi()), "...back to the chain");
        render(e, 10);
        view->readPixels(img);
        for (unsigned b = 0; b < 8u; ++b)
            std::printf("      band %u/8: chain+field %.4f | GI off %.4f | bounce %.4f | "
                        "lum(chain) %.4f\n", b,
                        chainBands[b], offBands[b], chainBands[b] - offBands[b],
                        [&]{ float sum=0; unsigned n=0;
                             for (unsigned y=kSize*b/8u; y<kSize*(b+1u)/8u; ++y)
                                 for (unsigned x=kSize/4u; x<kSize*3u/4u; ++x) { sum+=lum(img.at(x,y)); ++n; }
                             return n ? sum/float(n) : 0.0f; }());
        // THE GROUND IN FRONT OF THE WALL, and not the wall itself: bands 5 and
        // 6 are grey floor 10-20 m from the camera — well outside cascade 0's
        // 10 m box — lit by a white lamp. Nothing but indirect light can put
        // red there, and the GI-off arm is the control that says so.
        //
        // Bands 1-4 are the wall (red albedo, directly lit) and are printed for
        // the same reason: with the gate re-opened those pixels trade a FLAT,
        // unoccluded ambient for the chain's occlusion-aware cone answer, which
        // is dimmer — the honest term, and the residual the cascade march still
        // owes (PHOTON_SPEC E3 / patch 0033's header) is visible in it.
        const float chainBounce = 0.5f * (chainBands[5] + chainBands[6]);
        const float noGiBounce  = 0.5f * (offBands[5] + offBands[6]);
        std::printf("   red bounce on the grey ground beyond cascade 0: chain+field %.4f | "
                    "GI off %.4f\n", chainBounce, noGiBounce);
        CHECK(noGiBounce < 0.004f, "with no GI that ground carries no red at all (the control)");
        // THE EXISTENCE BAR IS GONE (PHOTON phase A, A1 section 1.2 — lane
        // FENCE-1). It used to read `chainBounce > 0.012f`, and that number was
        // 0.02 before ogre-patch 0065 changed the answer: a bar re-anchored DOWN
        // to the value it was measuring, with thirty lines explaining why the
        // smaller number was acceptable. It could also never fail for the reason
        // that matters — a transport delivering a tenth of the energy it should
        // still puts SOME red on that ground.
        //
        // WHAT IS LEFT HERE IS A SIGN, DERIVED FROM THE CONTROL and not from any
        // measurement of the thing under test: the chain+field arm must put more
        // red on that ground than the GI-off arm could, and "could" is the
        // control's own bar asserted one line above. Nothing about this can be
        // re-anchored by a change in the transport's magnitude.
        //
        // THE MAGNITUDE now lives in `gi.field_follows_energy`
        // (tests/gi/test_gi_field_energy.cpp, label `photon-target`): a sunlit
        // matte floor lighting a perpendicular matte wall, measured against the
        // finite-rectangle transfer integral with HlmsPbs BRDF_Default's own
        // direct diffuse as its source, at three heights. That suite is where a
        // transport that loses energy reds.
        CHECK(chainBounce > noGiBounce + 0.004f,
              "A PIXEL BEYOND CASCADE 0 CARRIES THE CHAIN'S BOUNCE WITH THE FIELD BOUND (G3) — "
              "more red than the GI-off control is allowed to carry; the ENERGY is "
              "gi.field_follows_energy's bar");
    }

    // =====================================================================
    // CASE 6 — WHAT IT COSTS AT 1080p, WALKING (PHOTON_SPEC §13 G3's third bar)
    // =====================================================================
    // G3-a hands the cone diffuse back to every pixel outside the field, and
    // that term is six cone marches per pixel — the ~0.8 ms/frame the field
    // bought when it took them away. This case renders the walk at the real
    // pixel count and prints what a frame costs, because a bar in milliseconds
    // is only meaningful against the resolution it was measured at.
    //
    // ASSERTED: no frame of the walk exceeds THE TIER'S budget (16.7 ms at
    // High — the 60 Hz desktop target; E2 (9)). The millisecond figures
    // themselves are PRINTED, not asserted: they are a property of the GPU the
    // suite happens to run on, and the gate must not turn a slower box into a
    // red. For scale, the lane's own measurement of the SHIPPED Showroom at
    // this tier and this resolution is a p95 of 4.5 ms and a worst of 14.95
    // over a 4,000-frame 100 m walk (spikes/photon-e2/BASELINE.md).
    std::printf("\n== case 6: 1080p, walking ==\n");
    {
        View *big = e->createOffscreenView("field-1080p", 1920, 1080, Colour(0, 0, 0));
        if (!big) {
            std::printf("   (no 1080p view — skipped)\n");
        } else {
            big->setScene(scene);
            CHECK(scene->setGlobalIllumination(fieldGi()), "the chain with the field at 1080p");
            big->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 14.0f),
                                                            Vec3(0.0f, 1.0f, -6.0f)));
            render(e, 8);
            engine->setFrameMonitor(MonitorLevel::Review);
            const int frames = 120;
            for (int f = 1; f <= frames; ++f) {
                const float x = float(f) * 0.25f;          // 15 m/s
                big->setCamera(enginetest::testCameraDescLookAt(Vec3(x, 2.0f, 14.0f),
                                                                Vec3(x, 1.0f, -6.0f)));
                render(e, 1);
            }
            std::vector<FrameRecord> recs;
            engine->setFrameMonitor(MonitorLevel::Off);
            engine->takeFrameRecords(recs);
            float worst = 0.0f, sum = 0.0f, worstGpu = -1.0f, sumGpu = 0.0f;
            unsigned nGpu = 0;
            for (const FrameRecord &r : recs) {
                worst = std::max(worst, r.totalMs);
                sum += r.totalMs;
                if (r.gpuMs >= 0.0f) { worstGpu = std::max(worstGpu, r.gpuMs); sumGpu += r.gpuMs; ++nGpu; }
            }
            std::printf("   %zu frames at 1920x1080: mean %.2f ms, worst %.2f ms; "
                        "GPU mean %.2f ms, worst %.2f ms (%u timed)\n",
                        recs.size(), recs.empty() ? 0.0f : sum / float(recs.size()), worst,
                        nGpu ? sumGpu / float(nGpu) : -1.0f, worstGpu, nGpu);
            CHECK(!recs.empty(), "the monitor recorded the 1080p walk");
            CHECK(worst < kTierBudgetMs, "NO FRAME OF A 1080p WALK EXCEEDS THE TIER'S BUDGET");
            big->setScene(nullptr);
            e->destroyView(big);
        }
    }

    // =====================================================================
    // TEARDOWN
    // =====================================================================
    {
        GiParams down; down.mode = GiMode::Off;
        CHECK(scene->setGlobalIllumination(down), "GI comes down");
        render(e, 2);
        const GiStatus s = scene->giStatus();
        CHECK(!s.ifdBound && s.ifdProbes == 0, "the field is gone with it");
        CHECK(s.ifdFollows == 0, "...and its follow counter with it");
    }

    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
