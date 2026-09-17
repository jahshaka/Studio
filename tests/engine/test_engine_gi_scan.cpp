// engine.gi_scan — WHAT COUNTS AS SCENE MOVEMENT, AT 90 Hz (lane VR-SCAN-1,
// 2026-09-18; the Fable read of VR-INPUT-1E-FIX, ledger §679).
//
// THE SUBJECT. The engine skips four O(scene) walks on a still frame by reading
// a MOVEMENT EPOCH: the host's transform-write counter plus the engine's own
// writes. `OgreScene::writeIsSceneMovement` decides which of the engine's own
// writes count, and its last clause used to be
//
//     item->getCastShadows() && n.shown && rq < kOverlayRenderQueue
//
// — where `getCastShadows()` is `mVisibilityFlags & LAYER_SHADOW_CASTER`, a bit
// Ogre sets on EVERY MovableObject at birth. So "it casts shadows" was true of
// every helper nobody had switched off, and the caster half of the test passed
// for all of the editor's furniture. It cost nothing while the furniture only
// moved when the camera did — and then VR-4-FIX moved the controller proxies
// and the pointing ray INSIDE the session's frame, where their poses are
// written on every frame a hand is located. Every VR frame with a hand or a ray
// in it therefore re-ran the full item scan, at 90 Hz.
//
// The caster WALK never worked that way (`walkItems`: `flags & channelsAll`
// first, and a helper carries kHelperBit instead of kVisibleBit), so the fix is
// to ask the same question: a write on a node in no caster channel cannot
// change a single answer any of the four walks gives.
//
// WHAT THIS SUITE ASSERTS, in counters and never in milliseconds:
//
//   1. A STILL SCENE SCANS NOTHING (the ENGINE-7 baseline this builds on).
//   2. THE WEARER'S FURNITURE IS NOT MOVEMENT: 30 frames that each write both
//      controller proxies' poses, the ray's pose and scale, and re-assert
//      their visibility, run ZERO movement scans and ask Ogre for ZERO world
//      AABBs. (Before the fix: one scan per frame, and the whole item list
//      walked with `getWorldAabbUpdated` — a parent-chain walk each.)
//   3. A REAL MOVER STILL IS: the same 30 frames moving a lit, shadow-casting
//      mesh run exactly 30 scans. Both sides, or the "fix" would be a way of
//      never noticing anything.
//   4. A VISIBILITY WRITE THAT CHANGES NOTHING DIRTIES NOTHING — the session's
//      `placeProxies` hides an unlocated hand on every frame it is not
//      located, and the mirror's show side is deliberately unconditional.
//   5. THE COST, measured in this process: what one scan costs at 1k nodes
//      (and at 5k / 10k with JAH_VR_SCAN_BENCH=1, which the gate does not
//      set). That number times 90 is what a session used to pay per second for
//      furniture no GI consumer and no shadow map can see. The A/B is arms 2
//      and 3: the same nodes, the same pushes, one classified as furniture and
//      one as content, in one process.
//
// The epoch only engages once a host has handed the engine a transform-write
// counter (a null one means "no epoch available; scan every frame"), so this
// suite holds its own — and never bumps it, because nothing document-side
// moves here.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <atomic>
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

static const unsigned kSize = 96;

static GiParams cascadeGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;          // no probe work: the subject is the item scan
    gi.cascades = true;
    gi.cascadeCount = 1;          // one small volume — this is not a GI suite
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 6.0f, 64, 0.0f };
    return gi;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-engine-gi-scan-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    // THE HOST'S EPOCH HALF. A real host passes the document's process-wide
    // counter; this suite passes one it owns and never bumps, which is the
    // truthful statement here — nothing outside the engine writes a transform
    // in this process.
    static std::atomic<unsigned long long> hostWrites{ 0ull };
    e->setTransformWriteCounter(&hostWrites);

    View *view = e->createOffscreenView("gi_scan", kSize, kSize, Colour(0.05f, 0.06f, 0.08f, 1.0f));
    if (!view) { std::printf("FAIL: offscreen view: %s\n", e->lastError().c_str()); return 1; }
    Scene *scene = e->createScene("gi_scan");
    if (!scene) { std::printf("FAIL: scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.25f, 0.25f, 0.25f), Colour(0.25f, 0.25f, 0.25f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.55f), 3.14159f);
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(40.0f, 0.1f, 40.0f));
    // The mover of arm 3: ordinary lit content, a shadow caster, in the view's
    // own channel — everything the proxies are not.
    const NodeId mover = enginetest::addTestCube(scene, Colour(0.85f, 0.2f, 0.15f), 0.0f, 0.5f);
    enginetest::setNodePosition(scene, mover, Vec3(0.0f, 0.5f, 0.0f));
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(2.5f, 1.8f, 3.0f), Vec3(0, 0.4f, 0)));
    CHECK(scene->setGlobalIllumination(cascadeGi()), "the one-cascade Photon arm accepts");

    // THE WEARER'S FURNITURE, exactly as the mirror builds it (scenemirror.cpp
    // syncVrProxies): a node per hand and a node per ray, each an unlit mesh
    // marked helper AND vrHelper — the two-bit rule — and nothing else.
    NodeId proxy[2] = { 0, 0 }, ray[2] = { 0, 0 };
    for (int i = 0; i < 2; ++i) {
        proxy[i] = enginetest::addTestCube(scene, Colour(0.6f, 0.6f, 0.7f), 0.0f, 0.6f);
        ray[i]   = enginetest::addTestCube(scene, Colour(0.3f, 0.7f, 1.0f), 0.0f, 0.6f);
        for (NodeId n : { proxy[i], ray[i] }) {
            scene->setNodeHelper(n, true);
            scene->setNodeVrHelper(n, true);
            scene->setNodeVisible(n, true);
        }
    }

    auto render = [&](int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); };
    render(30);                                   // settle: the chain builds, once

    // =====================================================================
    // 1. A STILL SCENE SCANS NOTHING
    // =====================================================================
    {
        const GiStatus from = scene->giStatus();
        render(20);
        const GiStatus st = scene->giStatus();
        std::printf("   still: %llu scans, %llu world-AABB reads over 20 frames\n",
                    st.giScans - from.giScans, st.giAabbReads - from.giAabbReads);
        CHECK(st.giScans == from.giScans && st.giAabbReads == from.giAabbReads,
              "still: 20 still frames run ZERO movement scans (the epoch is live at all)");
    }

    // =====================================================================
    // 2. AN INTERACTION SESSION'S FURNITURE IS NOT SCENE MOVEMENT
    // =====================================================================
    const unsigned kFrames = 30;
    {
        const GiStatus from = scene->giStatus();
        for (unsigned f = 0; f < kFrames; ++f) {
            const float t = 0.02f * float(f);
            for (int i = 0; i < 2; ++i) {
                // THE HAND: a pose, written inside the frame that draws it.
                scene->setNodeTransform(proxy[i],
                                        Vec3(0.3f + (i ? 0.4f : -0.4f), 1.2f + t, -0.6f),
                                        Quat(0.0f, 0.0f, 0.0f, 1.0f),
                                        Vec3(1.0f, 1.0f, 1.0f));
                // THE RAY: a rotation and a LENGTH, which is a scale write —
                // the ray's line is a unit segment the session stretches to the
                // hit distance, so its transform changes on every frame.
                scene->setNodeTransform(ray[i],
                                        Vec3(0.3f + (i ? 0.4f : -0.4f), 1.2f + t, -0.6f),
                                        Quat(0.0f, 0.0f, 0.0f, 1.0f),
                                        Vec3(1.0f, 1.0f, 2.0f + t));
                // ...AND THE VISIBILITY WRITES BOTH OF ITS WRITERS MAKE EVERY
                // FRAME (item 4): the mirror's unconditional show, and the
                // session's hide of a hand the runtime did not locate.
                scene->setNodeVisible(proxy[i], true);
                scene->setNodeVisible(ray[i], true);
            }
            e->renderOneFrame();
        }
        const GiStatus st = scene->giStatus();
        std::printf("   session furniture: %llu scans, %llu world-AABB reads over %u frames "
                    "that each wrote four poses and four visibilities\n",
                    st.giScans - from.giScans, st.giAabbReads - from.giAabbReads, kFrames);
        CHECK(st.giScans == from.giScans,
              "THE PROXIES AND THE RAY ARE NOT SCENE MOVEMENT: an interaction session runs "
              "ZERO movement scans at rest (it ran one per frame)");
        CHECK(st.giAabbReads == from.giAabbReads,
              "...and nothing in the engine's GI code asked Ogre for a world AABB");
    }

    // =====================================================================
    // 3. A REAL MOVER STILL IS — BOTH SIDES OR NOTHING
    // =====================================================================
    double moverScanMicros = 0.0;
    {
        const GiStatus from = scene->giStatus();
        for (unsigned f = 0; f < kFrames; ++f) {
            scene->setNodeTransform(mover, Vec3(0.0f, 0.5f + 0.01f * float(f), 0.0f),
                                    Quat(0.0f, 0.0f, 0.0f, 1.0f), Vec3(1.0f, 1.0f, 1.0f));
            e->renderOneFrame();
            const double us = scene->giStatus().giScanMicros;
            if (us > 0.0 && (moverScanMicros == 0.0 || us < moverScanMicros)) moverScanMicros = us;
        }
        const GiStatus st = scene->giStatus();
        std::printf("   a lit mover: %llu scans over %u frames (cheapest scan %.1f us at %d items)\n",
                    st.giScans - from.giScans, kFrames, moverScanMicros, 6);
        CHECK(st.giScans - from.giScans == (unsigned long long)kFrames,
              "A LIT, SHADOW-CASTING MESH THAT MOVES STILL SCANS, ONCE PER FRAME");
        CHECK(st.giAabbReads > from.giAabbReads,
              "...and that scan really reads the world AABBs (the arms above are not vacuous)");
    }

    // =====================================================================
    // 4. AN IDEMPOTENT VISIBILITY WRITE DIRTIES NOTHING
    // =====================================================================
    // Arm 2 already pushes them every frame; this states the rule on its own,
    // on the HIDE side, which is the one `VrSession::placeProxies` takes on
    // every frame a controller is not located — and where a re-derived subtree
    // used to re-ask the probe-visibility question and re-write every channel
    // bit for a state that had not moved.
    {
        scene->setNodeVisible(proxy[0], false);
        render(2);
        const GiStatus from = scene->giStatus();
        const unsigned long long serial = from.staleSerial, rebuilds = from.rebuilds;
        for (unsigned f = 0; f < kFrames; ++f) {
            scene->setNodeVisible(proxy[0], false);       // already hidden
            scene->setNodeVisible(ray[0], true);          // already shown
            e->renderOneFrame();
        }
        const GiStatus st = scene->giStatus();
        CHECK(st.giScans == from.giScans && st.staleSerial == serial && st.rebuilds == rebuilds,
              "a visibility write that changes nothing stales nothing and scans nothing");
        scene->setNodeVisible(proxy[0], true);
        render(2);
    }

    // =====================================================================
    // 5. THE COST OF ONE SCAN, AT SCALE (a measurement, not an assertion)
    // =====================================================================
    // What the 90 Hz path used to pay for the furniture above. One shared mesh
    // and one shared material for the filler, so this measures the WALK and not
    // the resource churn; the read is the CHEAPEST of five forced scans (a
    // walk's cost is a walk's cost — the slowest read is contention).
    {
        const char *bench = std::getenv("JAH_VR_SCAN_BENCH");
        std::vector<unsigned> steps = { 1000u };
        if (bench && *bench && *bench != '0') { steps.push_back(5000u); steps.push_back(10000u); }
        const MeshId filler = scene->createMesh(enginetest::unitCubeMesh());
        PbrParams fp;
        fp.albedo = Colour(0.7f, 0.7f, 0.72f);
        fp.roughness = 0.8f;
        const MaterialId fillerMat = scene->createPbrMaterial(fp);
        unsigned made = 0;
        for (unsigned target : steps) {
            for (; made < target; ++made) {
                const NodeId n = scene->createNode();
                scene->setNodeTransform(n,
                    Vec3(float(int(made % 100u) - 50) * 0.5f, 0.5f,
                         float(int(made / 100u)) * 0.5f - 25.0f),
                    Quat(0.0f, 0.0f, 0.0f, 1.0f), Vec3(0.2f, 0.2f, 0.2f));
                scene->attachMesh(n, filler, fillerMat);
            }
            render(3);
            double best = 0.0;
            for (int i = 0; i < 5; ++i) {
                scene->setNodeTransform(mover, Vec3(0.0f, 0.5f + 0.01f * float(i), 0.0f),
                                        Quat(0.0f, 0.0f, 0.0f, 1.0f), Vec3(1.0f, 1.0f, 1.0f));
                e->renderOneFrame();
                const double us = scene->giStatus().giScanMicros;
                if (us > 0.0 && (best == 0.0 || us < best)) best = us;
            }
            std::printf("   MEASURED: %u items -> one movement scan costs %.0f us "
                        "(%.2f ms/s at 90 Hz, which is what a located hand used to spend)\n",
                        made + 6u, best, best * 90.0 / 1000.0);
        }
        if (!bench)
            std::printf("   (5k / 10k arms skipped — set JAH_VR_SCAN_BENCH=1 to measure them)\n");
        // ...and the furniture is STILL free at that size: the arm the gate
        // cares about, re-run with a real scene under it.
        const GiStatus from = scene->giStatus();
        for (unsigned f = 0; f < 10u; ++f) {
            for (int i = 0; i < 2; ++i)
                scene->setNodeTransform(proxy[i], Vec3(0.3f, 1.2f + 0.01f * float(f), -0.6f),
                                        Quat(0.0f, 0.0f, 0.0f, 1.0f), Vec3(1.0f, 1.0f, 1.0f));
            e->renderOneFrame();
        }
        CHECK(scene->giStatus().giScans == from.giScans,
              "...and the furniture is free at that scale too (10 frames, zero scans)");
    }

    e->setTransformWriteCounter(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    std::printf("%s: %d failures\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
