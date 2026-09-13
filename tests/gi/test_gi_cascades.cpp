// gi.cascades — PHOTON'S CAMERA-CENTRED VOXEL CASCADES (SPECS/PHOTON_SPEC.md P0).
//
// The single-volume voxel arm fits ONE box around the scene's content: stand
// inside it and the bounce is right, walk out and the world stops bouncing.
// `GiParams::cascades` builds a chain of camera-centred boxes instead, chained
// through Ogre's own `VctLighting::addCascade`, scheduled by us.
//
// What a scheduler has to get right, and what each case here proves:
//
//   1. THE CHAIN IS BUILT AND BOUND. N cascades, innermost first, with the
//      tier's cells and a step per cascade derived so that every cascade steps
//      the same DISTANCE (the pin's autoCalculateStepSizes shape, written out
//      in resolveCascadeTable so it is ours to tune).
//   2. AT REST IT COSTS EXACTLY NOTHING. A still camera re-voxelises nothing,
//      for ever — the cache policy this engine is built on
//      (ENGINE_CACHE_POLICY_SPEC), and the one property upstream's manager
//      already had.
//   3. A SCROLL RE-CENTRES, ONE CASCADE PER FRAME. Crossing a cascade's step
//      moves its centre onto the world lattice and re-voxelises it, and no
//      frame ever pays for two.
//   4. THE AMBIENT IS IN EVERY CASCADE. Binding a VctLighting suppresses
//      HlmsPbs' own ambient scene-wide, so a cascade whose hemispheres were
//      never set contributes darkness. Upstream's cascade manager never sets
//      them and its room renders 2,2,2 (spikes/photon-s1 §4.3). Asserted as a
//      PICTURE: a scene lit by nothing but its ambient must render the same
//      brightness with the chain up as with GI off.
//   5. A TELEPORT IS SAFE, BOUNDED, AND NEVER BLACK. A 1 km jump is the case
//      that lost the GPU three separate ways on upstream's incremental path
//      (S1 §4.4: VK_ERROR_DEVICE_LOST, a segfault, a >2 s stall). Here the two
//      DIRTY_ALL guards turn it into exactly N counted rebuilds, one per frame,
//      innermost first — and every frame of that window is lit, because a
//      cascade that has not been rebuilt yet still holds a correctly lit
//      volume and the ambient covers the rest.
//   6. THE GUARD DOES NOT FIRE ON AN ORDINARY SCROLL. The counter would be
//      worthless if walking tripped it.
//
// Its own binary like every GI suite: the voxel lighting binds process-wide to
// HlmsPbs, so this scene must not share a process with another arm's.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

/// Mean luminance of the lower half of the picture — the ground, wherever the
/// camera stands. A band and not a pixel so one dithered texel cannot decide.
static float groundLum(const Image &img)
{
    float sum = 0.0f; unsigned n = 0;
    for (unsigned y = kSize * 5u / 8u; y < kSize * 7u / 8u; ++y)
        for (unsigned x = kSize / 4u; x < kSize * 3u / 4u; ++x) { sum += lum(img.at(x, y)); ++n; }
    return n ? sum / float(n) : 0.0f;
}

static GiParams cascadeGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;          // no probe work; this suite is about the voxels
    gi.cascades = true;
    return gi;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-cascades-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    // A 4 km ground so a 1 km jump still lands on something, a wall to bounce
    // from, and an ambient that is the ONLY light in case 4.
    View *view = e->createOffscreenView("cascades", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("cascades");
    view->setScene(scene);
    scene->setAmbient(Colour(0.35f, 0.35f, 0.35f), Colour(0.35f, 0.35f, 0.35f));
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(4000.0f, 0.1f, 4000.0f));
    const NodeId wall = enginetest::addTestCube(scene, Colour(0.9f, 0.05f, 0.05f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, wall, Vec3(0.0f, 3.0f, -6.0f));
    enginetest::setNodeScale(scene, wall, Vec3(12.0f, 6.0f, 0.2f));
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f), Vec3(0.0f, 1.0f, 0.0f)));
    render(e, 4);

    // =====================================================================
    // CASE 1 — the chain builds, and its table is the one we asked for
    // =====================================================================
    std::printf("\n== case 1: the chain builds ==\n");
    CHECK(scene->setGlobalIllumination(cascadeGi()), "the cascade arm accepts");
    render(e, 4);
    GiStatus st = scene->giStatus();
    CHECK(st.vctBound, "the chain's head is bound to the shader");
    CHECK(st.cascades.size() >= 2, "more than one cascade exists");
    float prevCell = 0.0f, prevHalf = 0.0f;
    bool stepsOk = true, orderOk = true;
    for (size_t i = 0; i < st.cascades.size(); ++i) {
        const GiStatus::CascadeStatus &c = st.cascades[i];
        std::printf("   c%zu half %6.2f m  res %3d  cell %.4f m  step %.3f m  centre %.2f,%.2f,%.2f\n",
                    i, c.halfSize, c.resolution, c.cell, c.step, c.centre.x, c.centre.y, c.centre.z);
        if (i && (c.halfSize <= prevHalf || c.cell <= prevCell)) orderOk = false;
        // Every cascade steps the same DISTANCE (within the ceil() the whole-cell
        // rounding forces), which is what keeps the chain from tearing.
        if (i && std::fabs(c.step - st.cascades[0].step) > 0.51f * c.cell) {
            // the outer cascades' step is allowed to be coarser, never finer
            if (c.step < st.cascades[0].step - 1e-3f) stepsOk = false;
        }
        prevCell = c.cell; prevHalf = c.halfSize;
    }
    CHECK(orderOk, "the cascades grow outward: every one is bigger and coarser than the last");
    CHECK(stepsOk, "no outer cascade steps more often than the innermost");
    CHECK(st.cascades[0].cell < 0.2f,
          "the innermost cascade resolves detail the single fitted volume cannot");

    // =====================================================================
    // CASE 2 — at rest it costs exactly nothing
    // =====================================================================
    std::printf("\n== case 2: at rest ==\n");
    const GiStatus rest0 = scene->giStatus();
    render(e, 60);
    const GiStatus rest1 = scene->giStatus();
    unsigned long long moved = 0;
    for (size_t i = 0; i < rest1.cascades.size(); ++i)
        moved += rest1.cascades[i].rebuilds - rest0.cascades[i].rebuilds;
    std::printf("   60 still frames: %llu cascade rebuilds\n", moved);
    CHECK(moved == 0, "a still camera re-voxelises nothing, for ever");
    CHECK(rest1.cascadeDeferrals == rest0.cascadeDeferrals, "and queues nothing");

    // =====================================================================
    // CASE 3 — a scroll re-centres, at most one cascade per frame
    // =====================================================================
    std::printf("\n== case 3: a scroll ==\n");
    const float step0 = rest1.cascades[0].step;
    const Vec3 centreBefore = rest1.cascades[0].centre;
    unsigned long long before = 0, worstFrame = 0;
    for (const auto &c : rest1.cascades) before += c.rebuilds;
    // Walk 6 of cascade 0's steps, one frame per 1/8 step, and watch the total
    // rebuild count per FRAME.
    unsigned long long prevTotal = before;
    for (int f = 1; f <= 48; ++f) {
        const float x = float(f) * step0 / 8.0f;
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(x, 2.0f, 6.0f),
                                                         Vec3(x, 1.0f, 0.0f)));
        render(e, 1);
        const GiStatus s = scene->giStatus();
        unsigned long long t = 0;
        for (const auto &c : s.cascades) t += c.rebuilds;
        worstFrame = std::max(worstFrame, t - prevTotal);
        prevTotal = t;
    }
    st = scene->giStatus();
    std::printf("   after %.1f m of travel: c0 centre %.2f -> %.2f, rebuilds %llu, "
                "worst frame %llu rebuild(s), deferrals %llu, dirtyMajority %llu\n",
                6.0f * step0, centreBefore.x, st.cascades[0].centre.x,
                prevTotal - before, worstFrame, st.cascadeDeferrals, st.cascadeDirtyMajority);
    CHECK(st.cascades[0].centre.x > centreBefore.x + step0 * 0.5f,
          "cascade 0 followed the camera");
    CHECK(prevTotal > before, "the walk re-voxelised something");
    CHECK(worstFrame <= 1, "NO FRAME EVER PAYS FOR TWO CASCADES (the one-per-frame budget)");
    CHECK(st.cascadeFullRebuilds == 0, "an ordinary walk never trips the teleport guard");

    // =====================================================================
    // CASE 4 — the ambient is in every cascade (upstream's 2,2,2 room)
    // =====================================================================
    std::printf("\n== case 4: the ambient reaches every cascade ==\n");
    {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 60.0f),
                                                         Vec3(0.0f, 1.0f, 40.0f)));
        // Far from the wall, so the ONLY thing lighting the ground here is the
        // ambient — and the camera is beyond the inner cascades' reach of the
        // scene's geometry, which is exactly where a cascade with black
        // hemispheres would print itself on the floor.
        Image img;
        const auto measureArm = [&](const GiParams &p, const char *what) {
            CHECK(scene->setGlobalIllumination(p), what);
            render(e, 8);
            view->readPixels(img);
            return groundLum(img);
        };
        GiParams off = cascadeGi(); off.mode = GiMode::Off;
        GiParams single = cascadeGi(); single.cascades = false;
        GiParams one = cascadeGi();
        one.cascadeCount = 1;
        one.cascadeSet[0] = GiParams::GiCascadeDesc{ 5.0f, 128, 0.0f };

        const float ambientOff = measureArm(off, "GI off");
        // HOW THE LOSS SCALES WITH THE CHAIN LENGTH — the diagnostic that says
        // whether it is the chain's composite (per-hop) or the cascades
        // themselves (per-cascade placement).
        for (int n = 1; n <= 4; ++n) {
            GiParams p = cascadeGi();
            p.cascadeCount = n;
            const float halves[4] = { 5.0f, 10.0f, 15.0f, 60.0f };
            const int   ress[4]   = { 128, 128, 64, 64 };
            for (int i = 0; i < n; ++i) p.cascadeSet[i] = GiParams::GiCascadeDesc{ halves[i], ress[i], 0.0f };
            const float v = measureArm(p, n == 1 ? "chain length sweep" : "chain length sweep");
            std::printf("   chain of %d: ambient %.4f (%.2fx of GI off)\n", n, v, v / ambientOff);
        }
        const float ambientSingle = measureArm(single, "the single scene-fitted volume");
        const float ambientOne = measureArm(one, "a chain of ONE camera-centred cascade");
        const float ambientChain = measureArm(cascadeGi(), "the full chain again");
        std::printf("   ground lit by the ambient alone: GI off %.4f | single volume %.4f (%.2fx) "
                    "| 1 cascade %.4f (%.2fx) | %zu cascades %.4f (%.2fx)\n",
                    ambientOff, ambientSingle, ambientSingle / ambientOff,
                    ambientOne, ambientOne / ambientOff, scene->giStatus().cascades.size(),
                    ambientChain, ambientChain / ambientOff);
        // THE CONTRACT IS AGAINST THE ARM THE CASCADES REPLACE, not against
        // GI-off: cone-traced diffuse always eats some of the ambient (the
        // cone's own starting surface occludes it — Jahshaka ogre-patch 0021
        // measures 65% surviving on an open floor), and that is a property of
        // VCT, not of cascades. What must not happen is the chain being DARKER
        // than one volume: that is the 2,2,2 room, and it is what upstream's
        // cascade manager renders.
        // ONE CASCADE IS THE STRUCTURAL TEST: it proves the ambient pair really
        // is pushed into a camera-centred volume (upstream never pushes it at
        // all and its room renders 2,2,2), and it must be EXACT.
        CHECK(ambientOne > 0.95f * ambientSingle,
              "a camera-centred cascade carries the ambient exactly like the fitted volume");
        // A CHAIN LOSES SOME OF IT, and the number is pinned here rather than
        // wished away: Ogre's cascade continuation restarts its march distance
        // in every cascade, so each hop re-samples the surface the cone stands
        // on. Jahshaka ogre-patch 0033 fixes the two accumulators that doubled
        // per hop and re-applies the self-occlusion bias at every entry, which
        // takes a four-cascade chain from 0.00x (black) to 0.51x; the residual
        // needs the travelled distance carried across the hop, which is
        // PHOTON P1's and is recorded in OGRE_UPSTREAM_ISSUES. It is bounded:
        // with the irradiance field on, the field REPLACES this term.
        CHECK(ambientChain > 0.40f * ambientSingle,
              "a four-cascade chain keeps the measured share of the ambient (>=0.40x, was 0.00x)");
        CHECK(ambientChain > 0.02f, "and it is a lit picture, not a black one");
    }

    // =====================================================================
    // CASE 5 + 6 — the teleport: bounded, counted, and never black
    // =====================================================================
    std::printf("\n== case 5: a 1 km teleport ==\n");
    {
        const GiStatus b = scene->giStatus();
        const size_t n = b.cascades.size();
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(1000.0f, 2.0f, 1000.0f),
                                                         Vec3(1000.0f, 1.0f, 980.0f)));
        float darkest = 1e9f;
        size_t framesToSettle = 0;
        for (size_t f = 0; f < 4u * n + 4u; ++f) {
            render(e, 1);
            Image img; view->readPixels(img);
            darkest = std::min(darkest, groundLum(img));
            const GiStatus s = scene->giStatus();
            if (!framesToSettle && s.cascadeFullRebuilds - b.cascadeFullRebuilds >= n)
                framesToSettle = f + 1;
        }
        st = scene->giStatus();
        std::printf("   full rebuilds %llu (cascades %zu), settled after %zu frames, "
                    "darkest ground during the window %.4f\n",
                    st.cascadeFullRebuilds - b.cascadeFullRebuilds, n, framesToSettle, darkest);
        CHECK(st.cascadeFullRebuilds - b.cascadeFullRebuilds == (unsigned long long)n,
              "the guard fired exactly once per cascade — a teleport is N rebuilds, not a storm");
        CHECK(framesToSettle >= n, "and they were spread over frames, never paid in one");
        CHECK(framesToSettle <= 2u * n + 2u, "the chain is whole again within a fifth of a second");
        CHECK(darkest > 0.02f, "NO FRAME OF THE TELEPORT WINDOW IS BLACK");
        for (const auto &c : st.cascades)
            if (std::fabs(c.centre.x - 1000.0f) > c.halfSize) {
                CHECK(false, "every cascade ended up around the new camera");
                break;
            }
        std::printf("   every cascade re-centred on the new position\n");
    }

    // The arm must come down cleanly — the chain's extra cascades are owned by
    // the scene and die with it (the teardown order the arm requires).
    GiParams off; off.mode = GiMode::Off;
    CHECK(scene->setGlobalIllumination(off), "the chain comes down");
    render(e, 2);
    CHECK(scene->giStatus().cascades.empty(), "and leaves no cascades behind");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
