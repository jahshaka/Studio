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
//      moved every frame through cascade 0 re-voxelises it (the pre-frame gather reads
//      the table: OgreGi.cpp gatherAndBuild, twice) and the frame reads it after the
//      graph (OgreShadow.cpp) — three calls, ONE walk, the other two counted as the
//      frame's later calls. Run twice: with no host transform counter (most fixtures;
//      RED ON THE BASE — three scans a frame, GpuSceneStatus::scans, because without
//      the epoch every call walked) and with the product's (one on the base too: the
//      epoch already collapsed the calls there).
//   2. ATOM'S WORDS STEP ONCE PER FRAME: the frame records' `engine.atomwords` stage
//      (FrameRecord.stages) is filed once per frame. RED ON THE BASE: twice (before
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

static unsigned stageCount(const FrameRecord &r, const char *name)
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
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    // The host's transform-write counter: NOT installed for the first mover run (every
    // fixture that installs none — the epoch is unavailable, and the base walked on
    // every call), installed for the second (the product's configuration: the mirror
    // installs it, which is what lets a still frame skip the walk at all).
    static std::atomic<unsigned long long> hostWrites{ 0ull };

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
    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    CHECK(scene->setGlobalIllumination(chainGi()), "the GI chain built (its builds read the table before the frame)");
    for (int i = 0; i < 8; ++i) e->renderOneFrame();

    const GpuSubstrateStatus s0 = gpuSubstrateStatus(*scene);
    std::printf("   substrate: live %d, copies %u, frames in flight %u, workers %u\n", s0.live ? 1 : 0,
                s0.copies, s0.framesInFlight, s0.workers);
    CHECK(s0.live, "the scene's substrate is live on a Vulkan boot");

    // ---- 1 + 2 + 3. THE MOVER FRAMES, twice: no host counter, then the product's ----
    const auto moverRun = [&](const char *tag, NodeId mover) {
        e->setFrameMonitor(MonitorLevel::Review);
        {
            std::vector<FrameRecord> drop;
            e->renderOneFrame();
            e->takeFrameRecords(drop);
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
            hostWrites.fetch_add(1);
            e->renderOneFrame();
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
        std::vector<FrameRecord> recs;
        for (int i = 0; i < 6; ++i) e->renderOneFrame();
        e->setFrameMonitor(MonitorLevel::Off);
        e->takeFrameRecords(recs);
        unsigned wordsMin = 0xFFFFFFFFu, wordsMax = 0;
        for (const FrameRecord &r : recs) {
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
        CHECK(laterCalls > 0, (at + "...and those frames called the substrate more than once (not vacuous)").c_str());
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
    moverRun("no host counter", cubes[27]);
    e->setTransformWriteCounter(&hostWrites);
    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    moverRun("the product's counter", cubes[35]);

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
            hostWrites.fetch_add(1);
            e->renderOneFrame();
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
    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    {
        const GpuSceneStatus a = scene->gpuSceneStatus();
        const GpuSubstrateStatus sa = gpuSubstrateStatus(*scene);
        for (int i = 0; i < 8; ++i) e->renderOneFrame();
        const GpuSceneStatus b = scene->gpuSceneStatus();
        const GpuSubstrateStatus sb = gpuSubstrateStatus(*scene);
        CHECK(b.scans == a.scans && sb.walks == sa.walks && b.copyRuns == a.copyRuns && sb.copyWrites == sa.copyWrites,
              "A STILL FRAME walks nothing and copies nothing");
    }

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
