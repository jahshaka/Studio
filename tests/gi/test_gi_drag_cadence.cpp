// gi.drag_cadence — WHAT A DRAG COSTS THE CACHES, AND WHAT IT LEAVES BEHIND
// (DRAG-1, from the render audit's REFLECT F2/F3 and the SMOKE-41 measurement).
//
// THE TWO DEFECTS THIS EXISTS FOR, both measured on the owner's smoke of
// push #41 ("dragging the teapot in the Mirror Room drops the frame to 5-6 fps"
// and "the reflected lights flicker inside the spheres when they are moved"):
//
//   1. THE PROBE GRID. A STILL item whose AABB moves by more than a 64th of its
//      own largest extent records a moved box every frame, and any moved box
//      staled the WHOLE reflection-probe grid; the budget then spent one
//      capture per frame — six faces at 512 square, HDR, with the probe shadow
//      node recalculated on EVERY face — on a photograph of a box that had
//      already moved again by the time it was displayed. SMOKE-41 measured
//      probeCaptures at 1.00 per frame through a drag, about 4.3 ms of the
//      8.3 ms the drag added to the frame. The capture waits for the content to
//      hold still now; the STALENESS is recorded either way, so the sweep
//      guarantee keeps its shape — every stale probe is re-captured within
//      probes/budget frames OF THE CONTENT COMING TO REST.
//
//   2. TWO WRITERS ON ONE VOLUME. Under a cascade chain the scheduler rebuilds
//      cascade 0 every frame of a drag and ends that rebuild with a full-count
//      light injection, while the host's every-tenth-frame in-motion tick
//      re-injected EVERY cascade at ZERO bounces with the coarse ray march.
//      Two different answers for one volume, ten frames apart: the reflected
//      radiance of every lit surface pulsed with a ten-frame period, which is
//      what a reflection reading those voxels shows. The tick computes what a
//      rebuild computes now, and skips any cascade a rebuild has already
//      injected since the last tick.
//
// THE CASES:
//   1. A drag spends NO probe captures, and the staleness is still recorded.
//   2. The settle spends them — the whole grid, once, at the budget.
//   3. A NON-MOTION input (a light) is NOT deferred: it reaches the probes in
//      the frame it arrives even while something is being dragged.
//   4. Under a chain, no frame of a drag runs an injection at a bounce count
//      other than the cascade's own — read out of the monitor's own rows: the
//      moving tick's row must not appear for a cascade the scheduler rebuilt.
//   5. And the drag's END is the same picture: the probes catch up and the
//      grid's stale count returns to zero.
//
// Its own binary like every GI suite.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                     \
    } while (0)

static const unsigned kSize = 128;

static void render(Engine *e, int frames = 1)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

/// THE PROBE CASES RUN ON ONE VOLUME AND THE CHAIN CASE ON A CHAIN, and that is
/// the fixture's shape rather than a convenience. The probe grid is placed
/// through the LIT VOLUME, and under a cascade chain that volume is the
/// OUTERMOST cascade's box — 120 m at this tier — so every probe in a 16 m room
/// photographs nothing inside its own share of it and the depth rule drops the
/// whole grid ("all 18 probes photographed nothing", which is the correct
/// answer for an open world and the wrong fixture for counting captures). The
/// deferral being measured is in the probe budget and has nothing to do with
/// the chain; the chain case below turns the cascades on for its own question.
static GiParams hybridGi(bool chain)
{
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;      // a PROBE GRID: this suite is about captures
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 1;                 // the shipped rate: one probe per frame
    gi.probeCaptureSize = 64;            // small captures: the COUNT is the subject
    gi.cascades = chain;
    // The room, pinned — see the fixture's note.
    gi.testBoundsMin = Vec3(-4.6f, -0.6f, -4.6f);
    gi.testBoundsMax = Vec3( 4.6f,  5.6f,  4.6f);
    gi.pccProbesX = 3; gi.pccProbesY = 1; gi.pccProbesZ = 3;   // nine: a real sweep
    return gi;
}

/// A point lamp at a position. Not in enginetesthelpers because only this suite
/// needs one placed by hand; the directional helper there carries the intensity
/// convention (setLight applies powerScale = intensity * pi) and this follows it.
static NodeId addPointLamp(Scene *s, const Vec3 &pos, float power)
{
    const NodeId node = s->createNode();
    if (!node) return 0;
    s->setNodeTransform(node, pos, Quat{0.f, 0.f, 0.f, 1.f}, Vec3{1.f, 1.f, 1.f});
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1.f, 1.f, 1.f);
    l.intensity = power / 3.14159265358979323846f;
    l.range = 30.0f;
    if (!s->setLight(node, l)) return 0;
    return node;
}

/// An UNLIT cube: it carries kVisibleBit but NOT kGiGeometryBit, so the probe
/// faces photograph it while the voxels never see it — a "probe-only" item
/// (OgreScene::itemVisibilityFlags). A backdrop card, an image plane, a logo on
/// a wall. Moving one stales the probe grid through `mProbeOnlyChanged` and is
/// invisible to `mGiMovedBoxes`, which is the whole of case 6.
static NodeId addUnlitCube(Scene *s, const Colour &albedo)
{
    const NodeId node = s->createNode();
    if (!node) return 0;
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = albedo;
    p.shadingModel = ShadingModel::Unlit;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    return node;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-drag-cadence-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("drag", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("drag");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.2f, 0.2f, 0.2f), Colour(0.2f, 0.2f, 0.2f));

    // THE CLOSED ROOM, and the same shape gi.pcc_mirror uses for the same
    // reason: the probe grid is placed through the LIT VOLUME and each probe is
    // kept only if its own depth photograph came back with something INSIDE its
    // share of that volume. A room whose walls are much closer than the volume
    // — which is what an automatic fit around a cascade chain gives — drops
    // every probe ("all 18 probes photographed nothing"), which is the right
    // answer for an open world and no fixture at all for counting captures. So
    // the bounds are pinned to the room and the grid is explicit.
    const Colour white(0.85f, 0.85f, 0.85f);
    const auto slab = [&](const Colour &c, const Vec3 &pos, const Vec3 &scale) {
        const NodeId n = enginetest::addTestCube(scene, c, 0.0f, 0.9f);
        enginetest::setNodePosition(scene, n, pos);
        enginetest::setNodeScale(scene, n, scale);
        return n;
    };
    slab(white, Vec3(0.0f, -0.2f, 0.0f),  Vec3(8.8f, 0.4f, 8.8f));      // floor
    slab(white, Vec3(0.0f,  5.2f, 0.0f),  Vec3(8.8f, 0.4f, 8.8f));      // ceiling
    slab(white, Vec3(0.0f,  2.5f, -4.2f), Vec3(8.8f, 5.0f, 0.4f));      // -Z
    slab(Colour(1.0f, 0.02f, 0.02f), Vec3(0.0f, 2.5f, 4.2f), Vec3(8.8f, 5.0f, 0.4f));
    slab(white, Vec3(-4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));       // -X
    slab(white, Vec3( 4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));       // +X
    const NodeId prop = enginetest::addTestCube(scene, Colour(0.2f, 0.8f, 0.3f), 0.0f, 0.5f);
    enginetest::setNodePosition(scene, prop, Vec3(2.0f, 1.0f, 2.0f));
    addPointLamp(scene, Vec3(-2.0f, 4.0f, 1.0f), 6.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.5f, 3.5f), Vec3(0.0f, 2.0f, -1.0f));

    CHECK(scene->setGlobalIllumination(hybridGi(false)), "the hybrid arm builds");
    render(e, 16);
    GiStatus st = scene->giStatus();
    std::printf("   %d probes, budget %d, %zu cascades, dropped %d\n",
                st.probeCount, st.probeUpdatesPerFrame, st.cascades.size(), st.probesDropped);
    if (st.probeCount <= 0) {
        std::printf("FAIL: no probe grid — this suite's fixture must keep one\n");
        return 1;
    }
    // Let the grid catch up to a quiet state before measuring.
    for (int i = 0; i < 200 && scene->giStatus().staleProbes > 0; ++i) render(e, 1);
    CHECK(scene->giStatus().staleProbes == 0, "the grid starts quiet");

    // =====================================================================
    // CASE 1 — A DRAG SPENDS NO CAPTURES, AND STILL RECORDS THE STALENESS
    // =====================================================================
    std::printf("\n== case 1: 60 frames of drag ==\n");
    const unsigned long long deferredBefore = scene->giStatus().probeCapturesDeferred;
    int capturesDuringDrag = 0;
    Vec3 p(2.0f, 1.0f, 2.0f);
    for (int i = 0; i < 60; ++i) {
        p.x -= 0.05f;                     // 5 cm a frame: well over the 1/64-extent quantum
        enginetest::setNodePosition(scene, prop, p);
        render(e, 1);
        capturesDuringDrag += scene->giStatus().probeCapturesLastFrame;
    }
    const GiStatus mid = scene->giStatus();
    std::printf("   captures during the drag: %d; stale probes now %d of %d; deferred frames %llu\n",
                capturesDuringDrag, mid.staleProbes, mid.probeCount,
                mid.probeCapturesDeferred - deferredBefore);
    // NOT ZERO, and both of the reasons are deliberate. (a) Nothing can know at
    // the FIRST move whether a second is coming, so a gesture's opening frame
    // spends one. (b) The deferral has a CEILING — one capture every
    // kProbeDeferredCaptureEvery (30) frames — so that a motion which never
    // stops still re-captures at a bounded rate (case 7). Sixty frames of drag
    // therefore buy 1 + 60/30 = 3 at the very most, against the 59 the
    // un-deferred budget spent on the same fixture.
    CHECK(capturesDuringDrag <= 1 + 60 / 30,
          "A DRAG SPENDS THE OPENING FRAME AND THE CEILING, AND NOTHING ELSE");
    CHECK(mid.staleProbes == mid.probeCount, "...and every probe is RECORDED stale");
    CHECK(mid.probeCapturesDeferred > deferredBefore, "the deferral counter says why");

    // =====================================================================
    // CASE 2 — THE SETTLE SPENDS THEM, ONCE, AT THE BUDGET
    // =====================================================================
    std::printf("\n== case 2: the settle ==\n");
    int settleCaptures = 0, worstInOneFrame = 0, settleFrames = 0;
    for (int i = 0; i < 400; ++i) {
        render(e, 1);
        ++settleFrames;
        const int c = scene->giStatus().probeCapturesLastFrame;
        settleCaptures += c;
        worstInOneFrame = std::max(worstInOneFrame, c);
        if (scene->giStatus().staleProbes == 0 && settleCaptures > 0) break;
    }
    std::printf("   %d captures over %d frames, worst frame %d; stale now %d\n",
                settleCaptures, settleFrames, worstInOneFrame, scene->giStatus().staleProbes);
    CHECK(settleCaptures >= mid.probeCount, "THE WHOLE GRID IS RE-CAPTURED AT THE SETTLE");
    CHECK(worstInOneFrame <= 1, "...one per frame, never a hitch");
    CHECK(scene->giStatus().staleProbes == 0, "...and the grid ends quiet: the picture caught up");

    // =====================================================================
    // CASE 3 — A NON-MOTION INPUT IS NOT DEFERRED
    // =====================================================================
    // Motion defers the captures MOTION asked for and nothing else: a lamp
    // switched on while something is being dragged has to reach the probes in
    // the frame it is switched on, or an author loses the connection between
    // the edit and the picture for as long as they keep dragging.
    std::printf("\n== case 3: a light edit during a drag is not deferred ==\n");
    {
        int captures = 0;
        for (int i = 0; i < 6; ++i) {       // get motion going again
            p.x += 0.05f;
            enginetest::setNodePosition(scene, prop, p);
            render(e, 1);
            captures += scene->giStatus().probeCapturesLastFrame;
        }
        CHECK(captures <= 1, "at most the one frame a gesture cannot predict");
        addPointLamp(scene, Vec3(2.0f, 4.0f, -1.0f), 4.0f);
        int afterLight = 0;
        for (int i = 0; i < 6; ++i) {
            p.x += 0.05f;                   // the drag CONTINUES
            enginetest::setNodePosition(scene, prop, p);
            render(e, 1);
            afterLight += scene->giStatus().probeCapturesLastFrame;
        }
        std::printf("   captures in the six frames after the lamp: %d\n", afterLight);
        CHECK(afterLight > 0, "A LIGHT REACHES THE PROBES MID-DRAG");
    }

    // =====================================================================
    // CASE 4 — ONE WRITER PER CASCADE PER FRAME
    // =====================================================================
    // The moving tick must not re-inject a cascade the scheduler has already
    // rebuilt since the last tick: that is the two-writers defect, and the
    // monitor files a row for each ("vct.light.moving" against "vct.cascadeN").
    std::printf("\n== case 4: the moving tick does not write over a rebuild ==\n");
    {
        // THE CHAIN, for this case only — see hybridGi's note.
        CHECK(scene->setGlobalIllumination(hybridGi(true)), "the cascade chain builds");
        render(e, 16);
        CHECK(!scene->giStatus().cascades.empty(), "the chain is up");
        engine->setFrameMonitor(MonitorLevel::Review);
        for (int i = 0; i < 40; ++i) {
            p.z -= 0.05f;
            enginetest::setNodePosition(scene, prop, p);
            scene->refreshGiLighting(true);            // the host's in-motion tick
            render(e, 1);
        }
        std::vector<FrameRecord> recs;
        engine->takeFrameRecords(recs);
        engine->setFrameMonitor(MonitorLevel::Off);
        const size_t chainSize = scene->giStatus().cascades.size();
        int bothInOneFrame = 0, movingRows = 0, cascadeRows = 0;
        int worstCascadesInAFrame = 0, overlapUnits = -1;
        std::vector<int> tickUnitsSeen;
        for (const FrameRecord &r : recs) {
            bool moving = false, rebuilt = false;
            unsigned tickUnits = 0;
            int inThisFrame = 0;
            for (const CacheWork &w : r.cacheWork) {
                if (w.detail == "vct.light.moving") { moving = true; ++movingRows; tickUnits = w.units; }
                if (w.detail.rfind("vct.cascade", 0) == 0) { rebuilt = true; ++cascadeRows; ++inThisFrame; }
            }
            worstCascadesInAFrame = std::max(worstCascadesInAFrame, inThisFrame);
            if (moving && rebuilt) {
                ++bothInOneFrame;
                overlapUnits = std::max(overlapUnits, int(tickUnits));
            }
            if (moving) tickUnitsSeen.push_back(int(tickUnits));
        }
        std::printf("   %zu frames, chain of %zu: %d moving-tick rows, %d cascade rebuild rows, "
                    "%d frames with both; worst tick injections in such a frame %d\n",
                    recs.size(), chainSize, movingRows, cascadeRows, bothInOneFrame, overlapUnits);
        CHECK(movingRows > 0, "the moving tick still runs: the far field follows the drag");
        CHECK(worstCascadesInAFrame <= 1, "no frame paid for two cascades");
        // THE ASSERTION THIS CASE EXISTS FOR. In a frame where the scheduler
        // rebuilt a cascade — which ends in a full-count injection of it — the
        // tick must inject the OTHER cascades and not that one. Its work row's
        // `units` is how many it injected, so on a chain of N that number must
        // be at most N-1 whenever a rebuild happened in the same frame. Before
        // DRAG-1 it was N every time, at a DIFFERENT bounce count, and the
        // reflected radiance of every lit surface pulsed with the tick's own
        // ten-frame period.
        int fullChainTicks = 0, minUnits = 99;
        for (int u : tickUnitsSeen) {
            if (u >= int(chainSize)) ++fullChainTicks;
            minUnits = std::min(minUnits, u);
        }
        std::printf("   tick injection counts: %zu ticks, min %d, %d of them injected the whole "
                    "chain of %zu\n", tickUnitsSeen.size(), minUnits, fullChainTicks, chainSize);
        if (chainSize > 1) {
            // ONE of them may: the first tick after the monitor was armed has no
            // rebuild behind it yet. Every one after it must skip the cascade
            // the scheduler rebuilt in the frame between.
            CHECK(fullChainTicks <= 1,
                  "THE TICK NEVER RE-INJECTS A CASCADE THE REBUILD JUST INJECTED");
            CHECK(minUnits >= 1, "...and it still injects the cascades the rebuild did not");
        }
    }

    // =====================================================================
    // CASE 6 — A PROBE-ONLY (UNLIT) MOVER IS A GESTURE TOO
    // =====================================================================
    // `mGiMovedBoxes` holds GI GEOMETRY only. Unlit geometry the probe faces
    // capture — a backdrop card, an image plane — reports through
    // `mProbeOnlyChanged`, and it stales the grid exactly the same way. The
    // first cut of the deferral read the boxes alone, so a dragged image plane
    // looked like a still scene and spent a capture on every frame of the drag.
    std::printf("\n== case 6: an unlit mover defers too ==\n");
    {
        CHECK(scene->setGlobalIllumination(hybridGi(false)), "back to one volume");
        render(e, 16);
        const NodeId card = addUnlitCube(scene, Colour(0.9f, 0.9f, 0.2f));
        CHECK(card != 0, "the unlit card exists");
        enginetest::setNodePosition(scene, card, Vec3(0.0f, 2.0f, 0.0f));
        enginetest::setNodeScale(scene, card, Vec3(1.5f, 1.5f, 0.05f));
        for (int i = 0; i < 400 && scene->giStatus().staleProbes > 0; ++i) render(e, 1);
        CHECK(scene->giStatus().staleProbes == 0, "the grid is quiet before the drag");
        Vec3 c(0.0f, 2.0f, 0.0f);
        int captures = 0;
        unsigned long long staleBefore = scene->giStatus().staleSerial;
        for (int i = 0; i < 40; ++i) {
            c.x += 0.05f;
            enginetest::setNodePosition(scene, card, c);
            render(e, 1);
            captures += scene->giStatus().probeCapturesLastFrame;
        }
        const GiStatus st6 = scene->giStatus();
        std::printf("   40 frames of an unlit drag: %d captures; stale %d; serial moved %s\n",
                    captures, st6.staleProbes,
                    st6.staleSerial > staleBefore ? "yes" : "NO");
        CHECK(st6.staleSerial > staleBefore,
              "an unlit mover really does stale the grid (the fixture is honest)");
        CHECK(captures <= 2, "A PROBE-ONLY MOVER IS A GESTURE: it spends at most the "
                             "frames a gesture cannot predict");
    }

    // =====================================================================
    // CASE 7 — A MOTION THAT NEVER STOPS STILL RE-CAPTURES, AT A CEILING
    // =====================================================================
    // A drag ends; a keyframed object in play, a settling physics body or a
    // script does not. Without a ceiling that is ONE endless gesture and the
    // probes would hold the pre-motion room for as long as it lasts.
    std::printf("\n== case 7: the ceiling on an endless motion ==\n");
    {
        for (int i = 0; i < 400 && scene->giStatus().staleProbes > 0; ++i) render(e, 1);
        int captures = 0;
        const int kFrames = 200;
        for (int i = 0; i < kFrames; ++i) {
            p.y = 1.0f + 0.05f * float(i % 20);      // never stops
            enginetest::setNodePosition(scene, prop, p);
            render(e, 1);
            captures += scene->giStatus().probeCapturesLastFrame;
        }
        // The engine's ceiling is one capture every N frames while deferred.
        // The suite reads N from the behaviour rather than from a copy of the
        // constant: the floor is what the ceiling guarantees and the top allows
        // the one or two frames at the start of a gesture that nothing can
        // predict.
        const int kEvery = 30;
        std::printf("   %d frames of continuous motion: %d captures (ceiling one per %d)\n",
                    kFrames, captures, kEvery);
        CHECK(captures >= kFrames / kEvery,
              "AN ENDLESS MOTION STILL RE-CAPTURES: the deferral has a ceiling");
        CHECK(captures <= kFrames / kEvery + 2,
              "...and the ceiling really is a ceiling (not the un-deferred rate)");
    }

    // =====================================================================
    // CASE 5 — THE DRAG'S END IS A CAUGHT-UP GRID
    // =====================================================================
    std::printf("\n== case 5: the picture catches up ==\n");
    {
        CHECK(scene->setGlobalIllumination(hybridGi(false)), "back to one volume");
        render(e, 16);
        for (int i = 0; i < 600 && scene->giStatus().staleProbes > 0; ++i) render(e, 1);
        const GiStatus fin = scene->giStatus();
        std::printf("   stale %d, captures last frame %d, deferred frames %llu\n",
                    fin.staleProbes, fin.probeCapturesLastFrame, fin.probeCapturesDeferred);
        CHECK(fin.staleProbes == 0, "the grid is quiet again once the scene holds still");
        CHECK(fin.probeCapturesLastFrame == 0, "...and a still scene spends nothing");
    }

    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    std::printf("\n%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
