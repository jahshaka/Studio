// gi.cascade_dirty — AN EDIT UNDER THE CASCADE CHAIN COSTS AT MOST ONE CASCADE
// PER FRAME (SPECS/PHOTON_SPEC.md §7 G1, and E2's own suite row).
//
// THE DEFECT THIS EXISTS FOR. Before G1, every settle re-solve (a box dragged
// and released), every material edit, every spawn, hide, delete and mobility
// flip went `refreshVctFast` -> refuses under cascades -> `rebuildVct` ->
// `teardownVct` + `buildCascadeArm`: N voxelisers destroyed and rebuilt FROM
// SCRATCH in ONE frame, with every mesh buffer re-derived and re-uploaded. At
// room scale that is N x (3-6 ms GPU + 1.2-1.6 CPU); on the 8,026-instance
// lattice it is N x 17-108 ms (PHOTON_SPEC P0 §6.2). The scheduler's whole
// contract — at most one cascade re-voxelised per frame — was true of a camera
// SCROLL and false of every EDIT.
//
// WHAT EACH CASE ASSERTS, read out of the render-loop monitor's own records
// (the `vct.cascadeN` rows the scheduler files, one per re-voxelisation):
//
//   1. A GEOMETRY EDIT + settle: at most ONE cascade rebuild row in any frame,
//      and ZERO whole-chain builds (`GiStatus::rebuilds` does not move).
//   2. A MATERIAL edit (the case that needs a NEW voxeliser per cascade, since
//      VctMaterial caches its conversions by raw datablock pointer): still one
//      per frame, still no chain build — the replacement is swapped into the
//      EXISTING lighting, one cascade per frame (ogre-patch 0037).
//   3. A SPAWN: same, through `invalidateGiCaches` and the frame-time flush.
//   4. A LIGHT edit, which moves no geometry at all: ZERO cascade rebuild rows.
//      A light change is a re-INJECTION over voxels that are already there, on
//      every cascade, and it must never re-voxelise anything.
//   5. AND THE EDIT IS ANSWERED. Cheap is worthless if it is also wrong: the
//      chain's total rebuild count must actually MOVE after a geometry edit —
//      the cascades that can see it owe one each and the scheduler spends them.
//
// Its own binary like every GI suite: the voxel lighting binds process-wide to
// HlmsPbs, so this scene must not share a process with another arm's.
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

static GiParams chainGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;          // no probe work: this suite is about the voxels
    gi.cascades = true;
    return gi;
}

/// What the monitor recorded over the frames just rendered: the worst number of
/// cascade re-voxelisations in ANY ONE frame, and how many there were in total.
struct Spend { int worstPerFrame = 0; int total = 0; int frames = 0; };

static Spend spendOf(Engine *engine)
{
    std::vector<FrameRecord> recs;
    engine->takeFrameRecords(recs);
    Spend s;
    s.frames = int(recs.size());
    for (const FrameRecord &r : recs) {
        int inThisFrame = 0;
        for (const CacheWork &w : r.cacheWork)
            if (w.detail.rfind("vct.cascade", 0) == 0) ++inThisFrame;
        s.total += inThisFrame;
        s.worstPerFrame = std::max(s.worstPerFrame, inThisFrame);
    }
    return s;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-cascade-dirty-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("dirty", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("dirty");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.2f, 0.2f, 0.2f), Colour(0.2f, 0.2f, 0.2f));

    // A room the whole chain can see into: a ground, a wall, and a prop to edit.
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(200.0f, 0.1f, 200.0f));
    const NodeId wall = enginetest::addTestCube(scene, Colour(0.9f, 0.05f, 0.05f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, wall, Vec3(0.0f, 3.0f, -6.0f));
    enginetest::setNodeScale(scene, wall, Vec3(12.0f, 6.0f, 0.2f));
    const NodeId prop = enginetest::addTestCube(scene, Colour(0.1f, 0.8f, 0.2f), 0.0f, 0.5f);
    enginetest::setNodePosition(scene, prop, Vec3(1.0f, 1.0f, 0.0f));
    const NodeId sun = enginetest::addDirectionalLight(scene, Vec3(0.2f, -1.0f, -0.4f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 9.0f), Vec3(0.0f, 1.0f, -1.0f));

    CHECK(scene->setGlobalIllumination(chainGi()), "the cascade chain builds");
    render(e, 8);
    GiStatus st = scene->giStatus();
    CHECK(!st.cascades.empty(), "the chain is up");
    if (st.cascades.empty()) return 1;
    std::printf("   chain: %zu cascades, %llu whole-chain builds so far\n",
                st.cascades.size(), st.rebuilds);

    const auto totalRebuilds = [&]() {
        unsigned long long n = 0;
        for (const GiStatus::CascadeStatus &c : scene->giStatus().cascades) n += c.rebuilds;
        return n;
    };

    // =====================================================================
    // CASE 1 — A GEOMETRY EDIT
    // =====================================================================
    std::printf("\n== case 1: a geometry edit is at most one cascade per frame ==\n");
    {
        const unsigned long long chainBefore = scene->giStatus().rebuilds;
        const unsigned long long spentBefore = totalRebuilds();
        engine->setFrameMonitor(MonitorLevel::Review);
        enginetest::setNodePosition(scene, prop, Vec3(-2.0f, 1.0f, 1.5f));
        scene->refreshGlobalIllumination();          // the host's settle
        render(e, 30);
        const Spend s = spendOf(engine.get());
        engine->setFrameMonitor(MonitorLevel::Off);
        const unsigned long long chainAfter = scene->giStatus().rebuilds;
        std::printf("   %d frames, %d cascade rebuild rows, worst frame %d; whole-chain builds "
                    "%llu -> %llu\n", s.frames, s.total, s.worstPerFrame, chainBefore, chainAfter);
        CHECK(s.worstPerFrame <= 1, "NO FRAME PAID FOR TWO CASCADES");
        CHECK(chainAfter == chainBefore, "and NOT ONE whole-chain rebuild");
        CHECK(totalRebuilds() > spentBefore, "...while the edit WAS answered (cascades re-voxelised)");
    }

    // =====================================================================
    // CASE 2 — A MATERIAL EDIT (the one that needs a new voxeliser)
    // =====================================================================
    std::printf("\n== case 2: a material edit is at most one cascade per frame ==\n");
    {
        const unsigned long long chainBefore = scene->giStatus().rebuilds;
        const unsigned long long spentBefore = totalRebuilds();
        engine->setFrameMonitor(MonitorLevel::Review);
        PbrParams p;
        p.albedo = Colour(0.05f, 0.1f, 0.9f);
        p.metalness = 0.0f;
        p.roughness = 0.5f;
        const MaterialId blue = scene->createPbrMaterial(p);
        CHECK(blue != 0, "a new material");
        const MeshId mesh = scene->createMesh(enginetest::unitCubeMesh());
        CHECK(scene->attachMesh(prop, mesh, blue), "the prop takes it (a material edit)");
        scene->refreshGlobalIllumination();
        render(e, 30);
        const Spend s = spendOf(engine.get());
        engine->setFrameMonitor(MonitorLevel::Off);
        std::printf("   %d frames, %d cascade rebuild rows, worst frame %d\n",
                    s.frames, s.total, s.worstPerFrame);
        CHECK(s.worstPerFrame <= 1, "NO FRAME PAID FOR TWO CASCADES");
        CHECK(scene->giStatus().rebuilds == chainBefore, "and NOT ONE whole-chain rebuild");
        CHECK(totalRebuilds() > spentBefore, "...while the edit WAS answered");
    }

    // =====================================================================
    // CASE 3 — A SPAWN
    // =====================================================================
    std::printf("\n== case 3: a spawn is at most one cascade per frame ==\n");
    {
        const unsigned long long chainBefore = scene->giStatus().rebuilds;
        engine->setFrameMonitor(MonitorLevel::Review);
        const NodeId spawned = enginetest::addTestCube(scene, Colour(0.9f, 0.9f, 0.1f), 0.0f, 0.4f);
        enginetest::setNodePosition(scene, spawned, Vec3(3.0f, 1.0f, -1.0f));
        render(e, 30);
        const Spend s = spendOf(engine.get());
        engine->setFrameMonitor(MonitorLevel::Off);
        std::printf("   %d frames, %d cascade rebuild rows, worst frame %d\n",
                    s.frames, s.total, s.worstPerFrame);
        CHECK(s.worstPerFrame <= 1, "NO FRAME PAID FOR TWO CASCADES");
        CHECK(scene->giStatus().rebuilds == chainBefore, "and NOT ONE whole-chain rebuild");
    }

    // =====================================================================
    // CASE 4 — A LIGHT EDIT RE-VOXELISES NOTHING
    // =====================================================================
    // Nothing geometric moved, so the answer is a re-INJECTION over the voxels
    // that are already there, on every cascade, at the full bounce count. A
    // cascade rebuild row here would mean the chain re-voxelised a scene that
    // did not change.
    std::printf("\n== case 4: a light edit re-voxelises nothing ==\n");
    {
        render(e, 8);                                 // drain anything still owed
        engine->setFrameMonitor(MonitorLevel::Review);
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.0f, 0.85f, 0.7f);
        l.intensity = 4.0f / 3.14159265358979f;
        CHECK(scene->setLight(sun, l), "the sun changes colour and strength");
        scene->refreshGlobalIllumination();
        render(e, 20);
        const Spend s = spendOf(engine.get());
        engine->setFrameMonitor(MonitorLevel::Off);
        std::printf("   %d frames, %d cascade rebuild rows\n", s.frames, s.total);
        CHECK(s.total == 0, "A LIGHT EDIT RE-VOXELISES NOTHING AT ALL");
    }

    // The arm comes down cleanly.
    GiParams off; off.mode = GiMode::Off;
    CHECK(scene->setGlobalIllumination(off), "the chain comes down");
    render(e, 2);
    CHECK(scene->giStatus().cascades.empty(), "and leaves no cascades behind");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
