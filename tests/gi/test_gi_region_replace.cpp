// gi.region_replace — A PROBE GRID FOLLOWS ITS CONTENT WITHOUT THE CHAIN
// (REGION-REBUILD-1, D8; the D4 read's H5).
//
// The hybrid places its reflection-probe grid in the content's region
// (computeProbeRegion). A refresh whose content moved that region materially
// (> 2 % of its size) must re-place the grid — a grid left around a moved object
// reflects a room that is not there. Until D8 it did that through the
// FROM-SCRATCH build: the whole cascade chain torn down and re-voxelised for a
// probe placement. The chain is camera-relative and re-voxelises the moved
// item's own box on its own dirt; it owes nothing to where the probes go.
//
// On a DENSE scene (a floor and 400 boxes, the hybrid at Medium — a grid tier on
// every machine), in ONE process:
//   (a) a small move INSIDE the region: no placement, no from-scratch build;
//   (b) a move that grows the region past the tolerance: exactly ONE placement
//       (a re-placement), the grid's fit now holds the moved box, the
//       from-scratch build count and every cascade's centre UNCHANGED, and the
//       cascades re-voxelised no more times than the edit's own box asks
//       (at most once each);
//   (c) THE COST, printed (frames counted; the milliseconds are the CPU time of
//       the refresh call and are a measurement, not a bar): the re-placement
//       against the from-scratch arm it replaced (setGlobalIllumination with
//       the same params after a teardown), paired in this process.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <chrono>
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

static void render(Engine *e, int frames)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

/// Frames until the GI owes nothing and the grid has caught up (capped).
static int toRest(Engine *e, Scene *s)
{
    int n = 0;
    for (; n < 2000; ++n) {
        e->renderOneFrame();
        const GiStatus st = s->giStatus();
        if (n > 2 && st.giAtRest && st.pccBound && st.staleProbes == 0) break;
    }
    return n + 1;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-region-replace-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("region", 128, 128, Colour(0, 0, 0));
    Scene *s = e->createScene("region");
    view->setScene(s);
    view->setOffscreenContract(OffscreenContract::StillPicture);
    s->setAmbient(Colour(0.1f, 0.1f, 0.1f), Colour(0.05f, 0.05f, 0.05f));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 2.0f);

    // THE DENSE SCENE: a 24 m floor and a 20 x 20 lattice of 0.5 m boxes.
    const NodeId floorN = enginetest::addTestCube(s, Colour(0.6f, 0.6f, 0.6f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, floorN, Vec3(0.0f, -0.1f, 0.0f));
    enginetest::setNodeScale(s, floorN, Vec3(24.0f, 0.2f, 24.0f));
    std::vector<NodeId> boxes;
    for (int z = 0; z < 20; ++z)
        for (int x = 0; x < 20; ++x) {
            const NodeId n = enginetest::addTestCube(s, Colour(0.3f + 0.03f * float(x % 7), 0.5f,
                                                              0.3f + 0.03f * float(z % 5)),
                                                     0.0f, 0.8f);
            enginetest::setNodePosition(s, n, Vec3(-10.0f + float(x), 0.25f, -10.0f + float(z)));
            enginetest::setNodeScale(s, n, Vec3(0.5f, 0.5f, 0.5f));
            boxes.push_back(n);
        }
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 8.0f), Vec3(0.0f, 0.5f, 0.0f));

    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;     // no traced reflections at Medium: a grid tier everywhere
    gi.gather = GiToggle::Off;
    CHECK(s->setGlobalIllumination(gi), "the hybrid builds");
    render(e, 120);
    const GiStatus st0 = s->giStatus();
    std::printf("   built: %d probes, %u placements, %llu from-scratch builds, fit (%.2f %.2f %.2f)..(%.2f %.2f %.2f)\n",
                st0.probeCount, st0.probePlacements, st0.rebuilds, st0.probeFitMin.x, st0.probeFitMin.y,
                st0.probeFitMin.z, st0.probeFitMax.x, st0.probeFitMax.y, st0.probeFitMax.z);
    CHECK(st0.pccBound && st0.probeCount > 0, "the dense scene holds a probe grid");

    const auto cascadeRebuilds = [](const GiStatus &st) {
        std::vector<unsigned long long> r;
        for (const auto &c : st.cascades) r.push_back(c.rebuilds);
        return r;
    };

    // ---- (a) a small move inside the region --------------------------------
    enginetest::setNodePosition(s, boxes[0], Vec3(-10.0f, 0.25f, -9.8f));
    s->refreshGlobalIllumination(GiRefreshReason::Explicit);
    render(e, 30);
    const GiStatus sa = s->giStatus();
    CHECK(sa.probePlacements == st0.probePlacements && sa.rebuilds == st0.rebuilds,
          "(a) a move inside the region places nothing and builds nothing from scratch");

    // ---- (b) a move that grows the region -----------------------------------
    const std::vector<unsigned long long> before = cascadeRebuilds(sa);
    std::vector<Vec3> centres;
    for (const auto &c : sa.cascades) centres.push_back(c.centre);
    enginetest::setNodePosition(s, boxes[1], Vec3(-9.0f, 0.25f, -16.0f));
    const auto t0 = std::chrono::steady_clock::now();
    s->refreshGlobalIllumination(GiRefreshReason::Explicit);
    const int replaceFrames = toRest(e, s);
    const double replaceMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    render(e, 30);
    const GiStatus sb = s->giStatus();
    const std::vector<unsigned long long> after = cascadeRebuilds(sb);
    unsigned long long extra = 0;
    bool atMostOnce = before.size() == after.size();
    for (size_t i = 0; i < after.size() && i < before.size(); ++i) {
        extra += after[i] - before[i];
        atMostOnce = atMostOnce && after[i] - before[i] <= 1ull;
    }
    bool sameCentres = centres.size() == sb.cascades.size();
    for (size_t i = 0; sameCentres && i < centres.size(); ++i)
        sameCentres = centres[i].x == sb.cascades[i].centre.x && centres[i].y == sb.cascades[i].centre.y &&
                      centres[i].z == sb.cascades[i].centre.z;
    std::printf("   (b) placements %u -> %u (re-placements %u), from-scratch builds %llu -> %llu, cascade "
                "re-voxelisations +%llu over %zu cascades, fit z %.2f..%.2f, %d probes\n",
                sa.probePlacements, sb.probePlacements, sb.probeReplacements, sa.rebuilds, sb.rebuilds, extra,
                after.size(), sb.probeFitMin.z, sb.probeFitMax.z, sb.probeCount);
    CHECK(sb.probePlacements == sa.probePlacements + 1u && sb.probeReplacements == sa.probeReplacements + 1u,
          "(b) a move that grows the region re-places the grid: exactly one placement");
    CHECK(sb.probeFitMin.z <= -16.0f, "(b) ...and the grid's fit now holds the moved box");
    CHECK(sb.rebuilds == sa.rebuilds, "(b) the chain is NOT built from scratch for a probe placement");
    CHECK(sameCentres, "(b) no cascade moved (the chain is camera-relative)");
    CHECK(atMostOnce, "(b) each cascade re-voxelised at most once: the edit's own box, on its own dirt");
    CHECK(sb.pccBound && sb.probeCount > 0, "(b) the re-placed grid is bound");

    // ---- (c) the cost, paired ------------------------------------------------
    // THE ARM IT REPLACED: the from-scratch build over the same content (a
    // teardown, then the same params), timed the same way — the first frame.
    GiParams offGi = gi;
    offGi.mode = GiMode::Off;
    s->setGlobalIllumination(offGi);
    render(e, 4);
    const auto t1 = std::chrono::steady_clock::now();
    s->setGlobalIllumination(gi);
    const int scratchFrames = toRest(e, s);
    const double scratchMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    // THE SECOND PAIR, the order reversed in effect (the first pair's re-placement paid
    // whatever the process had not compiled yet): another region growth, re-placed,
    // then the from-scratch build again.
    enginetest::setNodePosition(s, boxes[2], Vec3(-8.0f, 0.25f, 16.0f));
    const auto t2 = std::chrono::steady_clock::now();
    s->refreshGlobalIllumination(GiRefreshReason::Explicit);
    const int replaceFrames2 = toRest(e, s);
    const double replaceMs2 =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t2).count();
    const GiStatus sc = s->giStatus();
    s->setGlobalIllumination(offGi);
    render(e, 4);
    const auto t3 = std::chrono::steady_clock::now();
    s->setGlobalIllumination(gi);
    const int scratchFrames2 = toRest(e, s);
    const double scratchMs2 =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t3).count();
    std::printf("   (c) pair 2: re-placement %d frames %.1f ms (placements %u, rebuilds %llu); from-scratch "
                "%d frames %.1f ms\n", replaceFrames2, replaceMs2, sc.probePlacements, sc.rebuilds,
                scratchFrames2, scratchMs2);
    std::printf("   (c) a re-placement to rest: %d frames, %.1f ms wall; the from-scratch build it replaced: "
                "%d frames, %.1f ms (a measurement, not a bar: Debug, this box, one process; both include "
                "the grid's placement and every probe capture)\n",
                replaceFrames, replaceMs, scratchFrames, scratchMs);
    std::printf(failures ? "gi.region_replace: FAIL (%d)\n" : "gi.region_replace: PASS\n", failures);
    return failures ? 1 : 0;
}
