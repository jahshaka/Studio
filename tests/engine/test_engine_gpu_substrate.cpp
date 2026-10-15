// engine.gpu_substrate — THE GPU SCENE'S SUBSTRATE: ONE WRITER PER SCENE MANAGER, ONE
// SCAN PER FRAME, ONE INSTANCE-TABLE COPY PER FRAME IN FLIGHT (ENGINE-UPGRADE-2 stage 1,
// SPECS/ENGINE-UPGRADE-2.md; the substrate: irisgl/engine/src/GpuSceneSubstrate.h).
//
// THE SUBJECT. The scene's GPU description (GpuScene.h) is written by ONE object per
// SceneManager, the substrate, on a frame protocol: the frame's first graph-current
// call WALKS the items (the dirty scan, a UniformScalableTask on the SceneManager's
// workers); every later call in the same frame composes only the slots named to it
// since (a GI build's converted materials), and is COUNTED as a call that found the
// frame scanned. The device table is one copy per frame in flight, each brought
// current when its frame writes it, so a frame still executing on the device never
// reads a table the next frame is rewriting.
//
// THE CASES (counts, never clocks):
//   1. ONE WALK PER FRAME, on frames that rebuild the GI chain too: a GI-visible cube
//      moved every frame through cascade 0 re-voxelises it and the frame reads the table
//      after the graph (OgreShadow.cpp) — ONE walk. SINCE ENGINE-UPGRADE-ASYNC-3 the
//      re-voxelisation, made in the GI step's Compute region, COMPOSES NOTHING (it reads the
//      table the last frame committed; its two ensureGpuScene calls were deleted): a mover
//      frame calls the substrate once, so its later calls are ZERO. Run over two movers.
//   2. ATOM'S WORDS STEP ONCE PER FRAME: the frame records' `engine.atomwords` stage
//      (MonitorRecord.stages) is filed once per frame. RED ON THE BASE: twice (before
//      and after the ray tier).
//   3. ONE COPY PER FRAME IN FLIGHT: the substrate holds an instance-table copy per
//      frame in flight plus the one a frame writes (a pre-frame reader may bind the
//      outgoing copy in the frame that switches away from it); consecutive writing
//      frames write DIFFERENT copies; no frame writes a copy a frame still in flight
//      may read; and every copy is brought current by the frame that next writes it
//      (the live copy, read back, agrees with the mirror slot for slot). RED ON THE
//      BASE: one table, rewritten in place.
//   4. A STILL FRAME WALKS NOTHING AND COPIES NOTHING, as before.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/GpuSubstrate.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
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

static GiParams chainGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;
    return gi;
}

/// Cascade re-voxelisations, summed over the chain (GiStatus::rebuilds counts whole-arm builds only).
static unsigned long long cascadeRebuilds(Scene *s)
{
    unsigned long long n = 0;
    for (const GiStatus::CascadeStatus &c : s->giStatus().cascades) n += c.rebuilds;
    return n;
}

static unsigned stageCount(const MonitorRecord &r, const char *name)
{
    unsigned n = 0;
    for (const FrameStage &s : r.stages) n += s.name == name ? 1u : 0u;
    return n;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-engine-gpu-substrate-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();

    View *view = e->createOffscreenView("substrate", kSize, kSize, Colour(0.05f, 0.06f, 0.08f, 1.0f));
    Scene *scene = e->createScene("substrate");
    if (!view || !scene) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.3f, 0.3f, 0.3f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 12.0f), Vec3(0.0f, 0.5f, 0.0f));

    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.8f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.55f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(40.0f, 0.1f, 40.0f));
    std::vector<NodeId> cubes;
    for (int i = 0; i < 64; ++i) {
        const NodeId n = enginetest::addTestCube(scene, Colour(0.2f + 0.01f * float(i), 0.5f, 0.6f), 0.0f, 0.7f);
        enginetest::setNodePosition(scene, n, Vec3(float(i % 8) - 3.5f, 0.3f, float(i / 8) - 3.5f));
        enginetest::setNodeScale(scene, n, Vec3(0.4f, 0.4f, 0.4f));
        cubes.push_back(n);
    }
    enginetest::frames(e, 4);
    CHECK(scene->setGlobalIllumination(chainGi()), "the GI chain built (its builds read the table before the frame)");
    enginetest::frames(e, 8);

    const GpuSubstrateStatus s0 = gpuSubstrateStatus(*scene);
    std::printf("   substrate: live %d, copies %u, frames in flight %u, workers %u\n", s0.live ? 1 : 0,
                s0.copies, s0.framesInFlight, s0.workers);
    CHECK(s0.live, "the scene's substrate is live on a Vulkan boot");

    // ---- 1 + 2 + 3. THE MOVER FRAMES, twice over two movers (since U0 3b there is no host counter to install or
    // withhold: every write reaches the engine named, so both runs are the product's) ----
    const auto moverRun = [&](const char *tag, NodeId mover) {
        e->setFrameMonitor(MonitorLevel::Review);
        {
            std::vector<MonitorRecord> drop;
            enginetest::frames(e, 1);
            e->takeMonitorRecords(drop);
        }
        const unsigned long long rebuilds0 = cascadeRebuilds(scene);
        unsigned maxWalks = 0, maxScans = 0, maxCopyWrites = 0, sameCopyTwice = 0, writeFrames = 0, laterCalls = 0;
        unsigned lastCopy = 0xFFFFFFFFu;
        unsigned long long extra = 0, overwrites = 0;
        const unsigned kMoverFrames = 24;
        for (unsigned f = 0; f < kMoverFrames; ++f) {
            const GpuSceneStatus a = scene->gpuSceneStatus();
            const GpuSubstrateStatus sa = gpuSubstrateStatus(*scene);
            enginetest::setNodePosition(scene, mover, Vec3(0.3f * float(f % 4) - 0.45f, 0.3f, -0.5f));
            enginetest::frames(e, 1);
            const GpuSceneStatus b = scene->gpuSceneStatus();
            const GpuSubstrateStatus sb = gpuSubstrateStatus(*scene);
            maxScans = std::max(maxScans, unsigned(b.scans - a.scans));
            maxWalks = std::max(maxWalks, unsigned(sb.walks - sa.walks));
            maxCopyWrites = std::max(maxCopyWrites, unsigned(sb.copyWrites - sa.copyWrites));
            laterCalls += unsigned(sb.laterCalls - sa.laterCalls);
            extra += sb.extraWalks - sa.extraWalks;
            overwrites += sb.inFlightOverwrites - sa.inFlightOverwrites;
            if (sb.copyWrites != sa.copyWrites) {
                ++writeFrames;
                if (sb.liveCopy == lastCopy) ++sameCopyTwice;
                lastCopy = sb.liveCopy;
            }
        }
        const unsigned long long rebuilds = cascadeRebuilds(scene) - rebuilds0;
        std::vector<MonitorRecord> recs;
        enginetest::frames(e, 6);
        e->setFrameMonitor(MonitorLevel::Off);
        e->takeMonitorRecords(recs);
        unsigned wordsMin = 0xFFFFFFFFu, wordsMax = 0;
        for (const MonitorRecord &r : recs) {
            wordsMin = std::min(wordsMin, stageCount(r, "engine.atomwords"));
            wordsMax = std::max(wordsMax, stageCount(r, "engine.atomwords"));
        }
        std::printf("   [%s] %u mover frames, %llu cascade rebuilds: walks/frame max %u, scans/frame max %u, "
                    "later calls %u, extra walks %llu, copies written/frame max %u, writing frames %u, in-flight "
                    "overwrites %llu | %zu records: atomwords/frame %u..%u\n",
                    tag, kMoverFrames, rebuilds, maxWalks, maxScans, laterCalls, extra, maxCopyWrites, writeFrames,
                    overwrites, recs.size(), wordsMin, wordsMax);
        const std::string at = std::string("[") + tag + "] ";
        CHECK(rebuilds > 0, (at + "the mover re-voxelised the chain on its frames (the pre-frame readers ran)").c_str());
        CHECK(laterCalls == 0, (at + "...and the GI step's re-voxelisation composed nothing (the Compute region reads "
                                     "the table the last frame committed: no later call)").c_str());
        CHECK(maxWalks == 1u && extra == 0u,
              (at + "ONE WALK PER FRAME: the frame's first graph-current call walks, the later ones do not").c_str());
        CHECK(maxScans == 1u, (at + "ONE SCAN PER FRAME by the GPU scene's own counter (GpuSceneStatus::scans)").c_str());
        CHECK(!recs.empty() && wordsMin == 1u && wordsMax == 1u,
              (at + "ATOM'S WORDS STEP RUNS ONCE PER FRAME (engine.atomwords)").c_str());
        CHECK(writeFrames >= kMoverFrames - 1u && sameCopyTwice == 0u,
              (at + "consecutive writing frames write DIFFERENT copies (the ring)").c_str());
        CHECK(maxCopyWrites == 1u, (at + "a frame writes ONE copy, however many commits it makes").c_str());
        CHECK(overwrites == 0u, (at + "no frame wrote a copy a frame still in flight may read").c_str());
    };
    CHECK(s0.framesInFlight >= 2u && s0.copies == s0.framesInFlight + 1u,
          "ONE INSTANCE-TABLE COPY PER FRAME IN FLIGHT, plus the one a frame writes");
    moverRun("mover 27", cubes[27]);
    enginetest::frames(e, 4);
    moverRun("mover 35", cubes[35]);

    // ---- 1b. THE LATER-CALL PATH IS STILL LIVE (the stage-1 read's F4): a GRAPHICS-REGION build - a
    // GI configuration change rebuilds the arm in order: it composes, converts, composes - made with a
    // mover moved: its second compose is a LATER call that finds the scan made, no call walks twice,
    // and the frame writes ONE copy.
    {
        const GpuSubstrateStatus sa = gpuSubstrateStatus(*scene);
        const GpuSceneStatus a = scene->gpuSceneStatus();
        enginetest::setNodePosition(scene, cubes[27], Vec3(0.45f, 0.3f, -0.5f));
        GiParams g = chainGi();
        g.numBounces = g.numBounces + 1;   // a configuration change: the arm rebuilds (graphics)
        CHECK(scene->setGlobalIllumination(g), "a GI configuration change rebuilds the arm (a graphics-region build)");
        enginetest::frames(e, 1);
        const GpuSubstrateStatus sb = gpuSubstrateStatus(*scene);
        const GpuSceneStatus b = scene->gpuSceneStatus();
        std::printf("   [graphics build] later calls %llu, walks %llu, scans %llu, copies written %llu\n",
                    (unsigned long long)(sb.laterCalls - sa.laterCalls), (unsigned long long)(sb.walks - sa.walks),
                    (unsigned long long)(b.scans - a.scans), (unsigned long long)(sb.copyWrites - sa.copyWrites));
        CHECK(sb.laterCalls > sa.laterCalls,
              "[graphics build] the arm build's composes are LATER calls that found the frame scanned (not vacuous)");
        // (Two walks here, not one, and the walk-twice counter moves: the verb's build composes BETWEEN
        // frames under the coming frame's number, then that frame scans again. A verb-time build is not
        // a frame of the protocol; the one-walk law is the mover frames' above.)
        CHECK(sb.copyWrites - sa.copyWrites == 1u, "[graphics build] ...and ONE copy written, however many commits");
        enginetest::frames(e, 8);
    }

    // ---- 3b. EVERY COPY CATCHES UP ------------------------------------------------
    // The mover stops; the next writing frames go round the ring, and each copy they
    // write is brought current with everything written to the others since it was
    // last live. Read back the live copy after each of the ring's writes.
    {
        const GpuSceneStatus st = scene->gpuSceneStatus();
        std::vector<NodeId> walkers(cubes.begin(), cubes.begin() + 8);
        unsigned agreeCopies = 0, rounds = 0;
        std::vector<unsigned> seen;
        for (unsigned k = 0; k < s0.copies * 2u && rounds < s0.copies; ++k) {
            const GpuSubstrateStatus before = gpuSubstrateStatus(*scene);
            enginetest::setNodePosition(scene, walkers[k % walkers.size()],
                                        Vec3(-3.5f + float(k % 8), 0.6f + 0.05f * float(k), -3.5f));
            enginetest::frames(e, 1);
            const GpuSubstrateStatus after = gpuSubstrateStatus(*scene);
            if (after.copyWrites == before.copyWrites) continue;
            ++rounds;
            std::vector<GpuSceneEntry> dev;
            const bool read = scene->gpuSceneDeviceEntries(0, st.slotCount, dev);
            unsigned disagree = 0;
            for (unsigned i = 0; read && i < st.slotCount; ++i) {
                GpuSceneEntry m;
                if (!scene->gpuSceneEntry(i, m) || std::memcmp(m.world, dev[i].world, sizeof(m.world)) != 0 ||
                    std::memcmp(m.prevWorld, dev[i].prevWorld, sizeof(m.prevWorld)) != 0 ||
                    m.flags != dev[i].flags || m.nodeId != dev[i].nodeId)
                    ++disagree;
            }
            if (read && disagree == 0u) ++agreeCopies;
            if (std::find(seen.begin(), seen.end(), after.liveCopy) == seen.end()) seen.push_back(after.liveCopy);
            std::printf("   copy %u written: %u of %u slots disagree with the mirror\n", after.liveCopy, disagree,
                        st.slotCount);
        }
        CHECK(rounds == s0.copies && seen.size() == s0.copies, "the ring went round every copy");
        CHECK(agreeCopies == s0.copies,
              "EVERY COPY, when its frame wrote it, agreed with the mirror slot for slot (the catch-up)");
    }

    // ---- 4. A STILL FRAME --------------------------------------------------------
    enginetest::frames(e, 4);
    {
        const GpuSceneStatus a = scene->gpuSceneStatus();
        const GpuSubstrateStatus sa = gpuSubstrateStatus(*scene);
        enginetest::frames(e, 8);
        const GpuSceneStatus b = scene->gpuSceneStatus();
        const GpuSubstrateStatus sb = gpuSubstrateStatus(*scene);
        CHECK(b.scans == a.scans && sb.walks == sa.walks && b.copyRuns == a.copyRuns && sb.copyWrites == sa.copyWrites,
              "A STILL FRAME walks nothing and copies nothing");
    }

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
