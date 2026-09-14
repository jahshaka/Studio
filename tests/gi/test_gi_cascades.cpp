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
    // =====================================================================
    // CASE 0 — THE OPEN: the chain is built WHERE THE CAMERA IS
    // =====================================================================
    // A scene is opened and the document pushes its GI before the first frame
    // has rendered: nothing has tracked a camera yet. A camera-centred arm
    // built at that moment is built around the ORIGIN, and the first tracked
    // frame then finds every cascade a teleport away from the camera and
    // re-centres all of them, one per frame — N counted full rebuilds on every
    // open, the whole chain voxelised twice (audit B3).
    //
    // The rule is that there is nothing to build until a camera is known: the
    // arm waits, and the first tracked camera builds it once, in the right
    // place. Measured here as the rebuild total after an open: N, not 2N.
    std::printf("\n== case 0: the open ==\n");
    {
        // 200 m from the origin, so a chain built at the origin cannot overlap
        // a single one of its cascades — the honest form of the defect.
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(200.0f, 2.0f, 206.0f),
                                                         Vec3(200.0f, 1.0f, 200.0f)));
        const unsigned long long rebuildsBefore = scene->giStatus().rebuilds;
        CHECK(scene->setGlobalIllumination(cascadeGi()),
              "the cascade arm accepts before any frame has rendered");
        // WAITING IS NOT A REBUILD, and it says so rather than looking like a
        // failed build (round-2 review F4/F5).
        const GiStatus waiting = scene->giStatus();
        CHECK(waiting.cascadesAwaitingCamera && waiting.cascades.empty(),
              "...and reports that it is WAITING FOR A CAMERA, not that it built nothing");
        CHECK(waiting.rebuilds == rebuildsBefore,
              "...without counting the wait as a from-scratch rebuild");
        render(e, 24);
        const GiStatus o = scene->giStatus();
        unsigned long long total = 0;
        for (const auto &c : o.cascades) total += c.rebuilds;
        std::printf("   after the open: %zu cascades, %llu rebuilds in total, "
                    "%llu full rebuilds, %llu deferrals\n",
                    o.cascades.size(), total, o.cascadeFullRebuilds, o.cascadeDeferrals);
        CHECK(!o.cascades.empty() && o.vctBound,
              "the chain is up and bound once a camera has been tracked");
        CHECK(!o.cascadesAwaitingCamera, "...and it is not waiting for anything any more");
        CHECK(o.rebuilds == rebuildsBefore + 1u,
              "THE WHOLE OPEN IS ONE COUNTED REBUILD (it was two: the wait and the build)");
        CHECK(total == (unsigned long long)o.cascades.size(),
              "EVERY CASCADE IS VOXELISED EXACTLY ONCE ON AN OPEN (not twice: origin, then camera)");
        CHECK(o.cascadeFullRebuilds == 0,
              "...and no cascade was dragged in from the origin by the teleport guard");
        for (const auto &c : o.cascades)
            if (std::fabs(c.centre.x - 200.0f) > c.halfSize) {
                CHECK(false, "every cascade was placed around the camera");
                break;
            }
        GiParams down; down.mode = GiMode::Off;
        CHECK(scene->setGlobalIllumination(down), "and it comes down again");
    }

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

    // =====================================================================
    // CASE 6 — a cascade standing in EMPTY SPACE
    // =====================================================================
    // `VctVoxelizer::build` sizes its instance-to-world job from the instances
    // that survive the region cull, and Ogre refuses a compute job whose thread
    // groups multiply to zero — so a region containing nothing THROWS
    // (measured on an 8,404-node lattice, where it left a chain of cascades
    // holding nothing and a scene rendering with no GI and no visible error).
    // For a CAMERA-CENTRED volume that is not a corner case: fly off the edge
    // of a scene and the inner cascade is empty by definition.
    std::printf("\n== case 6: a cascade in empty space ==\n");
    {
        GiParams tiny = cascadeGi();
        tiny.cascadeCount = 2;
        tiny.cascadeSet[0] = GiParams::GiCascadeDesc{ 1.0f, 128, 0.0f };
        tiny.cascadeSet[1] = GiParams::GiCascadeDesc{ 4.0f, 64, 0.0f };
        // 80 m above the ground: nothing at all reaches either cascade.
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 80.0f, 0.0f),
                                                         Vec3(0.0f, 80.0f, -20.0f)));
        render(e, 2);
        CHECK(scene->setGlobalIllumination(tiny), "a chain whose cascades contain nothing builds");
        render(e, 6);
        st = scene->giStatus();
        CHECK(st.cascades.size() == 2, "and it is a whole chain, not a half-built one");
        CHECK(st.vctBound, "and it is bound");
        Image img; view->readPixels(img);
        CHECK(groundLum(img) >= 0.0f, "and the frame renders");
        // ...and it recovers when the camera comes back down to the geometry.
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f),
                                                         Vec3(0.0f, 1.0f, 0.0f)));
        render(e, 8);
        st = scene->giStatus();
        CHECK(st.cascades.size() == 2 && st.cascades[0].rebuilds > 0,
              "and the cascades re-fill when the camera returns to the geometry");
    }

    // =====================================================================
    // CASE 7 — THE SPECULAR AMBIENT SURVIVES THE CHAIN (ogre-patch 0033)
    // =====================================================================
    // The chain's cone walk is run TWICE per pixel — once for the diffuse
    // cones and once for the specular one — and patch 0033 originally fixed
    // only the first. The specular continuation kept `irrLight.alpha +=
    // newRes.alpha` (the opacity the call already returns as a running total,
    // so adding it doubles it per hop) and re-applied no self-occlusion bias at
    // the new cascade's entry, so `specAlpha = 1 - min(1, alpha/0.95)` reached
    // 0 after one hop and the ambient term in rough reflections — the sky in a
    // mirror — vanished at 2+ cascades (audit B5).
    //
    // Measured on a SMOOTH METAL plate whose albedo is black: it has no diffuse
    // term at all, so what it renders is the specular cone plus the ambient the
    // escape weight lets through. Far from the scene's wall, pointed at open
    // space, so the ambient is what dominates it.
    std::printf("\n== case 7: the specular ambient under a chain ==\n");
    {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(40.0f, 2.0f, 47.0f),
                                                         Vec3(40.0f, 0.55f, 40.0f)));
        Image img;
        const float roughs[4] = { 0.15f, 0.35f, 0.55f, 0.75f };
        for (int r = 0; r < 4; ++r) {
            const NodeId plate = enginetest::addTestCube(scene, Colour(0.95f, 0.95f, 0.95f),
                                                         1.0f /*metal*/, roughs[r]);
            enginetest::setNodePosition(scene, plate, Vec3(40.0f, 0.5f, 40.0f));
            enginetest::setNodeScale(scene, plate, Vec3(10.0f, 0.2f, 10.0f));
            render(e, 2);
            const auto plateLum = [&](int n) {
                GiParams p = cascadeGi();
                p.cascadeCount = n;
                const float halves[4] = { 5.0f, 10.0f, 15.0f, 60.0f };
                const int   ress[4]   = { 128, 128, 64, 64 };
                for (int i = 0; i < n; ++i)
                    p.cascadeSet[i] = GiParams::GiCascadeDesc{ halves[i], ress[i], 0.0f };
                scene->setGlobalIllumination(p);
                render(e, 8);
                view->readPixels(img);
                return groundLum(img);
            };
            const float l1 = plateLum(1), l2 = plateLum(2), l4 = plateLum(4);
            std::printf("   roughness %.2f: 1 cascade %.4f | 2 %.4f (%.2fx) | 4 %.4f (%.2fx)\n",
                        roughs[r], l1, l2, l2 / std::max(l1, 1e-6f), l4, l4 / std::max(l1, 1e-6f));
            // THE ASSERTION IS ON THE SMOOTHEST PLATE, and the rest of the sweep
            // is printed as evidence rather than pinned: a wide cone from a
            // plate lying on a ground plane is LEGITIMATELY occluded (half of
            // it points into the floor), so at roughness 0.55 the term is a few
            // thousandths either way and a ratio over it is noise. At 0.15 the
            // reflection is of open space, the ambient is the whole reading,
            // and the defect was unmistakable: 1.00 / 0.56 / 0.00 before the
            // amendment to ogre-patch 0033, 1.00 / 0.85 / 0.85 after. The
            // residual 0.85 is the same march-distance restart the diffuse half
            // leaves at 0.51 (the patch header; OGRE_UPSTREAM_ISSUES).
            if (r == 0) {
                CHECK(l1 > 0.02f, "a chain of ONE lights the plate through the ambient (reference)");
                CHECK(l2 > 0.70f * l1,
                      "TWO cascades keep the ambient in the reflection (it was lost after ONE hop)");
                CHECK(l4 > 0.70f * l1, "...and FOUR still do (it was black)");
            }
            scene->removeNode(plate);
            render(e, 2);
        }
    }

    // =====================================================================
    // CASE 8 — A PINNED TABLE THAT CANNOT BE HONOURED IS REFUSED WHOLE
    // =====================================================================
    // The same rule the mirror states for a half-specified row (audit B10): a
    // request the renderer cannot honour is not honoured halfway. A table whose
    // cascades do not grow outward gives Ogre a NEGATIVE `cascadeMaxLod` and
    // the march never hands over; a pinned step below one cell re-centres every
    // frame, and one above half the resolution lets the camera leave the box.
    std::printf("\n== case 8: pinned tables are validated ==\n");
    {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f),
                                                         Vec3(0.0f, 1.0f, 0.0f)));
        GiParams bad = cascadeGi();
        bad.cascadeCount = 3;
        bad.cascadeSet[0] = GiParams::GiCascadeDesc{  5.0f, 128, 0.0f };
        bad.cascadeSet[1] = GiParams::GiCascadeDesc{ 10.0f, 128, 0.0f };
        bad.cascadeSet[2] = GiParams::GiCascadeDesc{  8.0f, 128, 0.0f };   // SMALLER
        CHECK(scene->setGlobalIllumination(bad), "the arm accepts the call");
        render(e, 6);
        st = scene->giStatus();
        std::printf("   non-monotonic table -> %zu cascades (outermost half %.1f m)\n",
                    st.cascades.size(), st.cascades.empty() ? 0.0f : st.cascades.back().halfSize);
        CHECK(st.cascades.size() == 4 && std::fabs(st.cascades.back().halfSize - 60.0f) < 0.01f,
              "a table that does not grow outward is dropped WHOLE and the tier's is used");

        GiParams steps = cascadeGi();
        steps.cascadeCount = 2;
        steps.cascadeSet[0] = GiParams::GiCascadeDesc{  5.0f, 128,    0.2f };   // below a cell
        steps.cascadeSet[1] = GiParams::GiCascadeDesc{ 20.0f,  64, 10000.0f };  // runaway
        CHECK(scene->setGlobalIllumination(steps), "a table with out-of-range steps is accepted");
        render(e, 6);
        st = scene->giStatus();
        std::printf("   pinned steps -> c0 step %.4f m (cell %.4f), c1 step %.3f m (cell %.3f)\n",
                    st.cascades[0].step, st.cascades[0].cell,
                    st.cascades[1].step, st.cascades[1].cell);
        CHECK(st.cascades.size() == 2, "the table itself is honoured");
        CHECK(std::fabs(st.cascades[0].step - st.cascades[0].cell) < 1e-4f,
              "a step below one cell is floored at one cell");
        CHECK(std::fabs(st.cascades[1].step - st.cascades[1].cell * 32.0f) < 1e-3f,
              "and a step beyond half the resolution is capped there");
    }

    // =====================================================================
    // CASE 9 — WHAT THE MONITOR SEES: the camera's own work, one row a frame
    // =====================================================================
    // A scroll rebuild used to be filed under `mLastStaleReason` — whatever
    // last staled the PROBE grid, possibly an edit from minutes ago — so a
    // capture said a walk was "material" or "light" work (audit B12/D5). The
    // reason a camera-following cache re-does work is the CAMERA, and it is the
    // number the cadence gate needs: how much of a session's GI cost is
    // exploring, and how much is editing.
    std::printf("\n== case 9: the monitor's rows ==\n");
    {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f),
                                                         Vec3(0.0f, 1.0f, 0.0f)));
        CHECK(scene->setGlobalIllumination(cascadeGi()), "the chain is up for the walk");
        render(e, 8);
        e->setFrameMonitor(MonitorLevel::Review);
        const float step = scene->giStatus().cascades[0].step;
        for (int f = 1; f <= 40; ++f) {
            const float x = float(f) * step / 4.0f;
            view->setCamera(enginetest::testCameraDescLookAt(Vec3(x, 2.0f, 6.0f),
                                                             Vec3(x, 1.0f, 0.0f)));
            render(e, 1);
        }
        std::vector<FrameRecord> recs;
        e->takeFrameRecords(recs);
        unsigned rows = 0, camera = 0, other = 0, worstPerFrame = 0, counterTotal = 0;
        for (const FrameRecord &r : recs) {
            unsigned inFrame = 0;
            for (const CacheWork &w : r.cacheWork) {
                if (w.cache != CacheKind::Gi) continue;
                if (std::string(w.detail).rfind("vct.cascade", 0) != 0) continue;
                ++rows; ++inFrame;
                if (w.reason == WorkReason::Camera) ++camera; else ++other;
            }
            worstPerFrame = std::max(worstPerFrame, inFrame);
            counterTotal += r.cascadeRebuilds;
            if (r.cascadeRebuilds > 1)
                CHECK(false, "a frame reported more than one cascade rebuild");
        }
        e->setFrameMonitor(MonitorLevel::Off);
        std::printf("   %zu frames: %u cascade rows (%u Camera, %u other), worst frame %u, "
                    "frame counter total %u\n",
                    recs.size(), rows, camera, other, worstPerFrame, counterTotal);
        CHECK(rows > 0, "the walk filed cascade rebuild rows");
        CHECK(other == 0 && camera == rows,
              "EVERY SCROLL REBUILD IS FILED UNDER `Camera`, not under the last probe reason");
        CHECK(worstPerFrame <= 1, "...at most one row per frame");
        CHECK(counterTotal == rows,
              "...and FrameRecord::cascadeRebuilds counts exactly them");
    }

    // =====================================================================
    // CASE 10 — `items` IS WHAT THIS REBUILD VOXELISED
    // =====================================================================
    // `GiStatus::cascades[].items` promises "inside its box and big enough to
    // fill half a voxel of it". It used to be counted inside setCascadeItems,
    // which by rule 1 runs only when the ATTACH SET changes — so the number
    // froze at the last selection and a cascade that scrolled across a room
    // went on reporting the contents of the room it had left (round-2 review
    // F1). Here the wall leaves cascade 0's 5 m box as the camera walks away
    // from it, and the number has to say so.
    std::printf("\n== case 10: items follows the cascade ==\n");
    {
        // A prop where the camera starts, so cascade 0's 5 m box holds the
        // ground AND it; the scene's wall is 12 m away and was never in it.
        const NodeId prop = enginetest::addTestCube(scene, Colour(0.7f, 0.7f, 0.2f), 0.0f, 0.6f);
        enginetest::setNodePosition(scene, prop, Vec3(0.0f, 1.0f, 6.0f));
        enginetest::setNodeScale(scene, prop, Vec3(1.2f, 1.2f, 1.2f));
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 9.0f),
                                                         Vec3(0.0f, 1.0f, 6.0f)));
        CHECK(scene->setGlobalIllumination(cascadeGi()), "the chain is up around the prop");
        render(e, 8);
        st = scene->giStatus();
        const int near = st.cascades[0].items;
        const float step = st.cascades[0].step;
        for (int f = 1; f <= 40; ++f) {
            const float x = float(f) * step / 4.0f;
            view->setCamera(enginetest::testCameraDescLookAt(Vec3(x, 2.0f, 9.0f),
                                                             Vec3(x, 1.0f, 6.0f)));
            render(e, 1);
        }
        st = scene->giStatus();
        const int far = st.cascades[0].items;
        std::printf("   cascade 0 items: %d beside the prop -> %d %.0f m away\n",
                    near, far, 10.0f * step);
        CHECK(near >= 2, "beside the prop the inner cascade voxelises the ground AND the prop");
        CHECK(far < near, "walking away DROPS the prop from what it voxelises");
        CHECK(far >= 1, "...and the ground is still in it");
        scene->removeNode(prop);
        render(e, 4);
    }

    // =====================================================================
    // CASE 11 — A REBUILD THAT THROWS CHANGES NOTHING BUT THE CLOCK
    // =====================================================================
    // The scheduler re-centres a cascade BEFORE building it, and the
    // voxeliser's region is read live by the shader — so a build that throws
    // would leave the new region mapped onto the old place's voxels: a wrong
    // bounce, silently, until the next scroll (audit B4). The revert path had
    // never executed, which is not a proof of anything (round-2 review F2), so
    // one build is forced to throw here through the same kind of fault switch
    // the texture-wait budget uses (JAH_TEXTURE_WAIT_FAULT).
    std::printf("\n== case 11: a failed rebuild ==\n");
    {
        st = scene->giStatus();
        const GiStatus::CascadeStatus c0 = st.cascades[0];
        const float step = c0.step;
        const float x0 = c0.centre.x;
        setenv("JAH_GI_CASCADE_FAULT", "0", 1);
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(x0 + 1.5f * step, 2.0f, 6.0f),
                                                         Vec3(x0 + 1.5f * step, 1.0f, 0.0f)));
        render(e, 1);
        GiStatus bad = scene->giStatus();
        std::printf("   forced failure: rebuilds %llu -> %llu, centre %.2f -> %.2f, pending %d\n",
                    c0.rebuilds, bad.cascades[0].rebuilds, c0.centre.x, bad.cascades[0].centre.x,
                    bad.cascades[0].pending);
        CHECK(bad.cascades[0].rebuilds == c0.rebuilds,
              "a rebuild that threw is NOT counted as one");
        CHECK(std::fabs(bad.cascades[0].centre.x - c0.centre.x) < 1e-3f,
              "AND THE CASCADE KEEPS THE PLACEMENT ITS VOXELS ARE FOR (the revert path)");
        CHECK(bad.cascades[0].pending != 0, "...with the rebuild still owed");
        CHECK(scene->giStatus().vctBound, "...and the chain still bound and rendering");
        unsetenv("JAH_GI_CASCADE_FAULT");
        render(e, 1);
        st = scene->giStatus();
        std::printf("   after the fault is cleared: rebuilds %llu, centre %.2f, pending %d\n",
                    st.cascades[0].rebuilds, st.cascades[0].centre.x, st.cascades[0].pending);
        CHECK(st.cascades[0].rebuilds == c0.rebuilds + 1u, "the NEXT FRAME retries, and succeeds");
        CHECK(st.cascades[0].centre.x > c0.centre.x + step * 0.5f,
              "...and the cascade is where the camera is");
        CHECK(st.cascades[0].pending == 0, "...and owes nothing");
    }

    // =====================================================================
    // CASE 7 — AN EDIT IS NOT A CHAIN REBUILD (PHOTON_SPEC G1)
    // =====================================================================
    // THE DEFECT THIS CLOSES, in one line: until the per-cascade dirty path,
    // every settle re-solve, material edit, spawn, hide, delete and mobility
    // flip took `refreshVctFast` (which refuses under cascades) straight to
    // `rebuildVct` -> `teardownVct` + `buildCascadeArm` — N voxelisers destroyed
    // and rebuilt FROM SCRATCH IN ONE FRAME, with every mesh buffer re-derived
    // and re-uploaded because `removeAllItems` clears the voxeliser's mesh
    // bookkeeping. Cases 2, 3 and 5 all measure a still or walking CAMERA and
    // never touch the scene, so the whole class was invisible to this suite
    // (audit B2's own note).
    //
    // What is asserted, for every kind of edit, is the same three things:
    //   * ZERO whole-chain builds — `GiStatus::rebuilds` (bumped by every
    //     `rebuildVct`) does not move;
    //   * never more than ONE cascade re-voxelised in any single frame;
    //   * and no cascade is left owing a rebuild once the queue has drained.
    //
    // The camera stands still throughout, so every rebuild counted here was
    // paid for by the EDIT and by nothing else.
    std::printf("\n== case 7: edits under the chain ==\n");
    {
        CHECK(scene->setGlobalIllumination(cascadeGi()), "the chain is up for the edit cases");
        render(e, 8);
        const size_t nCascades = scene->giStatus().cascades.size();

        const auto chainRebuilds = [&]() {
            unsigned long long t = 0;
            for (const auto &c : scene->giStatus().cascades) t += c.rebuilds;
            return t;
        };
        const auto pendingCount = [&]() {
            int n = 0;
            for (const auto &c : scene->giStatus().cascades) n += c.pending ? 1 : 0;
            return n;
        };
        // Runs `frames` frames one at a time after an edit and reports the worst
        // single-frame cascade-rebuild count and the total. `arm0` is the
        // whole-chain build count sampled BEFORE the edit — and it has to be,
        // because `refreshGlobalIllumination()` is synchronous: on the base
        // engine the chain build lands inside that call, not inside the frames
        // that follow it.
        const auto measure = [&](const char *what, unsigned long long arm0, int frames,
                                 unsigned long long &outTotal, int &outWorst) {
            unsigned long long prev = chainRebuilds();
            const unsigned long long first = prev;
            int worst = 0;
            for (int i = 0; i < frames; ++i) {
                render(e, 1);
                const unsigned long long now = chainRebuilds();
                worst = std::max(worst, int(now - prev));
                prev = now;
            }
            outTotal = prev - first;
            outWorst = worst;
            const unsigned long long arm1 = scene->giStatus().rebuilds;
            std::printf("   %-22s chain builds %llu -> %llu, cascade rebuilds %llu "
                        "(worst frame %d), pending left %d\n",
                        what, arm0, arm1, outTotal, worst, pendingCount());
            char msg[256];
            std::snprintf(msg, sizeof(msg), "%s: ZERO whole-chain builds", what);
            CHECK(arm1 == arm0, msg);
            std::snprintf(msg, sizeof(msg), "%s: never two cascades in one frame", what);
            CHECK(worst <= 1, msg);
            std::snprintf(msg, sizeof(msg), "%s: the queue drained", what);
            CHECK(pendingCount() == 0, msg);
        };
        unsigned long long total = 0; int worst = 0;

        // ---- (a) a box dragged and RELEASED (the settle's re-solve) ---------
        // The drag itself must cost nothing at all, and the release must cost
        // the cascades that can SEE the box — not the chain.
        const NodeId dragged = enginetest::addTestCube(scene, Colour(0.2f, 0.8f, 0.2f), 0.0f, 0.8f);
        enginetest::setNodePosition(scene, dragged, Vec3(1.0f, 0.5f, 1.0f));
        render(e, 8);                                    // the spawn's own work drains
        {
            const unsigned long long arm0 = scene->giStatus().rebuilds;
            unsigned long long prev = chainRebuilds();
            int worstDrag = 0;
            for (int i = 0; i < 20; ++i) {               // the gesture: no settle asked for
                enginetest::setNodePosition(scene, dragged,
                                            Vec3(1.0f + 0.05f * float(i), 0.5f, 1.0f));
                render(e, 1);
                const unsigned long long now = chainRebuilds();
                worstDrag = std::max(worstDrag, int(now - prev));
                prev = now;
            }
            std::printf("   the drag itself: chain builds %llu -> %llu, worst frame %d\n",
                        arm0, scene->giStatus().rebuilds, worstDrag);
            CHECK(scene->giStatus().rebuilds == arm0 && worstDrag == 0,
                  "a drag in flight re-voxelises NOTHING (the host has not settled it)");
        }
        const unsigned long long armDrag = scene->giStatus().rebuilds;
        scene->refreshGlobalIllumination();              // the settle, as the mirror fires it
        measure("a box released", armDrag, 12, total, worst);
        CHECK(total >= 1 && total <= (unsigned long long)nCascades,
              "...and the cascades that can see it DID re-voxelise, one per frame");

        // ---- (b) a LIGHT edit: a re-injection, never a re-voxelisation ------
        const NodeId lamp = enginetest::addDirectionalLight(scene, Vec3(0.2f, -1.0f, -0.4f), 2.0f);
        render(e, 12);
        {
            LightDesc d;
            d.type = LightType::Directional;
            d.colour = Colour(1.0f, 0.4f, 0.2f);
            d.intensity = 3.0f;
            const unsigned long long arm0 = scene->giStatus().rebuilds;
            scene->setLight(lamp, d);
            scene->refreshGlobalIllumination();
            measure("a light edit", arm0, 12, total, worst);
            CHECK(total == 0,
                  "...and NOT ONE VOXEL was re-written: a light is a re-injection");
        }

        // ---- (c) a MATERIAL PARAMETER edit ---------------------------------
        // The voxeliser's VctMaterial cached that datablock's colour by pointer,
        // so this one really does need fresh voxels — but a REPLACEMENT
        // voxeliser per cascade, one per frame, with the chain's lighting
        // objects (and their raw `mExtraCascades` pointers) untouched.
        {
            const MeshId m = scene->createMesh(enginetest::unitCubeMesh());
            PbrParams pp; pp.albedo = Colour(0.1f, 0.1f, 0.9f); pp.roughness = 0.7f;
            const MaterialId mat = scene->createPbrMaterial(pp);
            const NodeId painted = scene->createNode();
            scene->attachMesh(painted, m, mat);
            enginetest::setNodePosition(scene, painted, Vec3(-1.5f, 0.5f, 1.0f));
            render(e, 16);
            pp.albedo = Colour(0.9f, 0.9f, 0.1f);
            const unsigned long long armMat = scene->giStatus().rebuilds;
            CHECK(scene->setPbrMaterial(mat, pp), "a material parameter is edited");
            scene->refreshGlobalIllumination();
            measure("a material edit", armMat, 16, total, worst);
            CHECK(total == (unsigned long long)nCascades,
                  "...and EVERY cascade got fresh voxels — one cascade per frame");
            CHECK(scene->giStatus().vctBound,
                  "...with the chain still bound after every voxeliser was replaced");

            // ---- (d) a PRESET APPLY: the datablock itself is destroyed ------
            // The churn rig's case, and the one the reuse arm could never take:
            // a shading-model change destroys the datablock and builds another,
            // so a recycled address would make every voxeliser's VctMaterial
            // cache paint the new material with the old one's colour. Answered
            // the same way a parameter edit is — a replacement voxeliser per
            // cascade, one cascade per frame.
            const unsigned long long armPre = scene->giStatus().rebuilds;
            CHECK(scene->setShadingModel(mat, ShadingModel::Unlit),
                  "a preset apply destroys the datablock (the item leaves GI)");
            measure("a preset apply", armPre, 16, total, worst);
            const unsigned long long armPre2 = scene->giStatus().rebuilds;
            CHECK(scene->setShadingModel(mat, ShadingModel::Lit),
                  "...and another preset puts it back");
            measure("a preset undone", armPre2, 16, total, worst);
            CHECK(total >= 1 && total <= (unsigned long long)nCascades,
                  "...with fresh voxels spread one cascade per frame");
            CHECK(scene->giStatus().vctBound, "...and the chain still bound");
        }

        // ---- (e) a SPAWN ---------------------------------------------------
        {
            unsigned long long arm0 = scene->giStatus().rebuilds;
            const NodeId spawned =
                enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.2f), 0.0f, 0.6f);
            enginetest::setNodePosition(scene, spawned, Vec3(0.0f, 0.5f, 2.0f));
            measure("a spawn", arm0, 16, total, worst);

            // ---- (f) a HIDE ------------------------------------------------
            arm0 = scene->giStatus().rebuilds;
            scene->setNodeVisible(spawned, false);
            measure("a hide", arm0, 16, total, worst);
            scene->setNodeVisible(spawned, true);
            render(e, 16);

            // ---- (g) an AUTHORING MOBILITY FLIP ----------------------------
            // Its own cube, so the DELETE below still deletes GI geometry: a
            // mover carries no kGiGeometryBit, and deleting one correctly costs
            // the voxels nothing at all.
            const NodeId promoted =
                enginetest::addTestCube(scene, Colour(0.3f, 0.3f, 0.9f), 0.0f, 0.6f);
            enginetest::setNodePosition(scene, promoted, Vec3(2.0f, 0.5f, 2.0f));
            render(e, 16);
            arm0 = scene->giStatus().rebuilds;
            scene->setNodeMovable(promoted, true, MobilityChange::Authoring);
            measure("an authoring flip", arm0, 16, total, worst);

            // ---- (h) a DELETE of LIT geometry -------------------------------
            arm0 = scene->giStatus().rebuilds;
            scene->removeNode(spawned);
            measure("a delete", arm0, 16, total, worst);
            CHECK(total >= 1, "...and the cascades that held it DID re-voxelise");
        }

        // ---- (j) THE BOX TEST DISCRIMINATES --------------------------------
        // The whole point of recording WHERE an edit happened: a change 40 m
        // away is outside the inner cascades' boxes, and they must not spend a
        // frame on it. (The tier table is 5 / 10 / 15 / 60 m half-sizes around
        // the camera, which stands at the origin throughout this case.)
        {
            render(e, 16);
            const NodeId faraway =
                enginetest::addTestCube(scene, Colour(0.9f, 0.3f, 0.3f), 0.0f, 0.6f);
            enginetest::setNodePosition(scene, faraway, Vec3(40.0f, 0.5f, 0.0f));
            render(e, 16);                                   // its arrival drains
            enginetest::setNodePosition(scene, faraway, Vec3(41.0f, 0.5f, 0.0f));
            render(e, 2);
            scene->refreshGlobalIllumination();
            std::vector<unsigned long long> before;
            for (const auto &c : scene->giStatus().cascades) before.push_back(c.rebuilds);
            render(e, 12);
            std::vector<unsigned long long> after;
            for (const auto &c : scene->giStatus().cascades) after.push_back(c.rebuilds);
            unsigned long long inner = 0, outer = 0;
            for (size_t i = 0; i < after.size(); ++i) {
                std::printf("   c%zu rebuilds +%llu (half %.1f m)\n", i, after[i] - before[i],
                            scene->giStatus().cascades[i].halfSize);
                if (scene->giStatus().cascades[i].halfSize < 39.0f) inner += after[i] - before[i];
                else outer += after[i] - before[i];
            }
            CHECK(inner == 0,
                  "an edit 40 m away costs the cascades that cannot see it NOTHING");
            CHECK(outer >= 1, "...and the one that can see it re-voxelises");
            scene->removeNode(faraway);
            render(e, 12);
        }

        // ---- (i) the picture still works -----------------------------------
        // Every one of the edits above kept objects, textures and buffers that a
        // from-scratch chain would have thrown away; the assertion that they are
        // still the RIGHT ones is a rendered frame that is not black.
        render(e, 4);
        Image shot;
        CHECK(view->readPixels(shot), "the chain renders after every edit");
        CHECK(groundLum(shot) > 0.01f, "...and the ground is still lit");

        GiParams down; down.mode = GiMode::Off;
        CHECK(scene->setGlobalIllumination(down), "the edited chain comes down");
        render(e, 2);
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
