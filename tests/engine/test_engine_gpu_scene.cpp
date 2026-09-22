// engine.gpu_scene — THE GPU SCENE'S TABLES (lane GPU-SCENE-1; ENGINE V2's
// V2-2 pulled forward, SPECS/atom/A3_GPU_SCENE_SLICE_DESIGN.md §2).
//
// THE SUBJECT. The scene's description now lives on the device: one 160-byte
// entry per ITEM SLOT holding this frame's world transform, LAST frame's world
// transform, the world bounds, the mesh table index and a flags word that is the
// one place the per-item predicates are computed. It is written for the CHANGED
// subset of slots each frame through one function, and the CPU mirror it is
// copied from is authoritative.
//
// WHAT THIS SUITE ASSERTS — and every one of them is a property a consumer will
// stand on (the ray tier's instance job, Atom's cull, Photon's reprojection):
//
//   1. THE DEVICE AGREES WITH THE MIRROR, for every slot, after one frame: a
//      thousand items, thirty-seven of them moved, read back out of the real
//      buffer and compared field by field. A table that is right on the CPU and
//      wrong on the device is the failure mode nothing else here could see.
//   2. prevWorld IS THE LAST FRAME'S WORLD, exactly — not two frames old, and
//      not the current one while an object is still moving. And a mover that
//      STOPS has prevWorld == world on the very next frame (without that, every
//      reprojection reads one frame of motion that did not happen).
//   3. A STILL FRAME WRITES NOTHING. No scan, no staging map, no copy.
//   4. A SWAP-REMOVE RE-WRITES EXACTLY ONE SLOT: the dense item index moves the
//      last item into the freed slot, and the table's entry travels with the
//      item that was renumbered — never the dead one's pose left in a live slot.
//   5. A GROW KEEPS EVERY ENTRY: the table doubles, and the entry written before
//      the doubling reads back identical out of the NEW buffer.
//   6. THE TABLES EXIST WITH RAYS OFF. The facility is not the ray tier's — a
//      machine with no ray queries (every Mac) still has a GPU scene.
//   7. THE COST OF THE DIRTY SCAN AT SCALE (a measurement, not an assertion):
//      the premise the design asked to be measured first — one cached-transform
//      compare plus one flags recompute per item, at 1k and (with
//      JAH_GPU_SCENE_BENCH=1) 8,001 items, in Debug.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
static const unsigned kFill = 1000u;
static const unsigned kMoved = 37u;

/// The flags word's bits, as Types.h documents them.
enum {
    kFlagVisible = 1u << 0,
    kFlagCaster = 1u << 1,
    kFlagMover = 1u << 2,
    kFlagGi = 1u << 3,
    kFlagAlpha = 1u << 4,
    kFlagSkinned = 1u << 5,
    kFlagOverlay = 1u << 6,
    kFlagRayTraced = 1u << 7,
};

static bool sameFloats(const float *a, const float *b, unsigned n) {
    return std::memcmp(a, b, n * sizeof(float)) == 0;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-engine-gpu-scene-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    // The epoch's host half. This suite writes every transform through the
    // engine's own verbs, which count themselves, so the counter is here only
    // to make the epoch AVAILABLE — the still-frame gate does not engage
    // without one.
    static std::atomic<unsigned long long> hostWrites{ 0ull };
    e->setTransformWriteCounter(&hostWrites);

    View *view = e->createOffscreenView("gpu_scene", kSize, kSize, Colour(0.05f, 0.06f, 0.08f, 1.0f));
    if (!view) { std::printf("FAIL: offscreen view: %s\n", e->lastError().c_str()); return 1; }
    Scene *scene = e->createScene("gpu_scene");
    if (!scene) { std::printf("FAIL: scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.25f, 0.25f, 0.25f), Colour(0.25f, 0.25f, 0.25f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.55f), 3.14159f);
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(6.0f, 6.0f, 14.0f), Vec3(0, 0, 0)));

    auto render = [&](int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); };

    // ONE mesh and ONE material for the filler: the subject is the table, not
    // resource churn, and a shared mesh is also the case the mesh table's
    // reference counting is for.
    const MeshId filler = scene->createMesh(enginetest::unitCubeMesh());
    PbrParams fp;
    fp.albedo = Colour(0.7f, 0.7f, 0.72f);
    fp.roughness = 0.8f;
    const MaterialId fillerMat = scene->createPbrMaterial(fp);
    std::vector<NodeId> nodes;
    nodes.reserve(kFill);
    for (unsigned i = 0; i < kFill; ++i) {
        const NodeId n = scene->createNode();
        scene->setNodeTransform(n,
            Vec3(float(int(i % 40u) - 20) * 0.6f, 0.5f, float(int(i / 40u)) * 0.6f - 12.0f),
            Quat(0.0f, 0.0f, 0.0f, 1.0f), Vec3(0.25f, 0.25f, 0.25f));
        scene->attachMesh(n, filler, fillerMat);
        nodes.push_back(n);
    }
    render(4);

    // =====================================================================
    // 0. THE TABLES ARE THERE, AND THEY HOLD THE SCENE
    // =====================================================================
    {
        const GpuSceneStatus st = scene->gpuSceneStatus();
        std::printf("   tables: live=%d slotCount=%u capacity=%u meshEntries=%u grows=%llu "
                    "writes=%llu copyRuns=%llu\n",
                    st.live ? 1 : 0, st.slotCount, st.capacity, st.meshEntries, st.grows, st.writes,
                    st.copyRuns);
        CHECK(st.live, "the GPU scene's tables exist on a Vulkan boot");
        CHECK(st.slotCount == kFill, "one slot per item in the scene's dense item index");
        CHECK(st.capacity >= st.slotCount, "the table holds every slot");
        CHECK(st.meshEntries == 1u,
              "ONE mesh entry for a thousand instances of one mesh (the table is per MESH)");
    }

    // =====================================================================
    // 1. THE DEVICE AGREES WITH THE MIRROR, FOR EVERY SLOT
    // 2. prevWorld IS THE LAST FRAME'S WORLD
    // =====================================================================
    // The previous poses, taken from the table BEFORE the move, so the
    // assertion compares against a number this suite owns rather than against
    // the table's own answer.
    std::vector<GpuSceneEntry> before(kFill);
    for (unsigned i = 0; i < kFill; ++i) scene->gpuSceneEntry(i, before[i]);

    for (unsigned k = 0; k < kMoved; ++k)
        scene->setNodeTransform(nodes[k * 7u % kFill],
                                Vec3(float(k) * 0.3f, 2.0f + float(k) * 0.05f, -3.0f),
                                Quat(0.0f, 0.0f, 0.0f, 1.0f), Vec3(0.25f, 0.25f, 0.25f));
    const GpuSceneStatus beforeMove = scene->gpuSceneStatus();
    render(1);
    {
        const GpuSceneStatus st = scene->gpuSceneStatus();
        std::printf("   one frame with %u movers: lastDirty=%u copyRuns +%llu scans +%llu "
                    "aabbReads +%llu lastCopyMs=%.3f\n",
                    kMoved, st.lastDirty, st.copyRuns - beforeMove.copyRuns,
                    st.scans - beforeMove.scans, st.aabbReads - beforeMove.aabbReads,
                    st.lastCopyMs);
        CHECK(st.lastDirty == kMoved,
              "THE WRITE PATH TOOK EXACTLY THE MOVED SLOTS — no more, no fewer");
        CHECK(st.aabbReads - beforeMove.aabbReads == kMoved,
              "...and asked Ogre for exactly those world AABBs (a still item costs none)");

        std::vector<GpuSceneEntry> device;
        const bool read = scene->gpuSceneDeviceEntries(0u, kFill, device);
        CHECK(read && device.size() == kFill, "the device table reads back");
        unsigned disagree = 0u, movedSeen = 0u, prevWrong = 0u, prevStill = 0u;
        for (unsigned i = 0; i < kFill && read; ++i) {
            GpuSceneEntry mirror;
            scene->gpuSceneEntry(i, mirror);
            const GpuSceneEntry &d = device[i];
            if (!sameFloats(mirror.world, d.world, 12) ||
                !sameFloats(mirror.prevWorld, d.prevWorld, 12) ||
                !sameFloats(mirror.boundsMin, d.boundsMin, 3) ||
                !sameFloats(mirror.boundsMax, d.boundsMax, 3) ||
                mirror.flags != d.flags || mirror.meshIndex != d.meshIndex ||
                mirror.nodeId != d.nodeId)
                ++disagree;
            // prevWorld: a slot that moved this frame must carry the pose the
            // table held BEFORE the move; a slot that did not must have
            // prevWorld == world.
            const bool moved = !sameFloats(before[i].world, d.world, 12);
            if (moved) {
                ++movedSeen;
                if (!sameFloats(before[i].world, d.prevWorld, 12)) ++prevWrong;
            } else if (!sameFloats(d.world, d.prevWorld, 12)) {
                ++prevStill;
            }
        }
        std::printf("   compared %u slots: %u disagreements, %u movers seen, "
                    "%u wrong previous poses, %u still slots with a previous pose\n",
                    kFill, disagree, movedSeen, prevWrong, prevStill);
        CHECK(read && disagree == 0u,
              "THE DEVICE TABLE AND THE CPU MIRROR AGREE FOR EVERY SLOT, field by field");
        CHECK(movedSeen == kMoved, "the movers really moved in the table");
        CHECK(prevWrong == 0u, "prevWorld IS the pose the table held last frame, for every mover");
        CHECK(prevStill == 0u, "a slot that did not move has prevWorld == world");
        // THE FLAGS WORD is what every consumer reads instead of re-deriving.
        GpuSceneEntry one;
        scene->gpuSceneEntry(0u, one);
        std::printf("   slot 0 flags = 0x%03x meshIndex=%u nodeId=%u\n", one.flags, one.meshIndex,
                    one.nodeId);
        CHECK((one.flags & kFlagVisible) && (one.flags & kFlagGi) && (one.flags & kFlagRayTraced),
              "a lit, shown cube reads visible, GI-visible and TRACED in the flags word");
        CHECK(!(one.flags & (kFlagSkinned | kFlagOverlay | kFlagAlpha)),
              "...and neither skinned, nor overlay, nor alpha-tested");
        CHECK(one.meshIndex == 0u && one.nodeId == unsigned(nodes[0]),
              "...and it names its mesh entry and its node");
    }

    // =====================================================================
    // 2b. A MOVER THAT STOPS HAS NO MOTION ON THE NEXT FRAME
    // =====================================================================
    {
        render(1);   // nothing moved this frame
        std::vector<GpuSceneEntry> device;
        scene->gpuSceneDeviceEntries(0u, kFill, device);
        unsigned stale = 0u;
        for (const GpuSceneEntry &d : device)
            if (!sameFloats(d.world, d.prevWorld, 12)) ++stale;
        std::printf("   the frame after the movers stopped: %u slots still carry motion\n", stale);
        CHECK(stale == 0u,
              "A MOVER THAT STOPS CARRIES NO MOTION ON THE NEXT FRAME (prevWorld closed)");
    }

    // =====================================================================
    // 3. A STILL FRAME WRITES NOTHING
    // =====================================================================
    {
        render(2);   // let the close above settle out of the dirty set
        const GpuSceneStatus from = scene->gpuSceneStatus();
        render(20);
        const GpuSceneStatus st = scene->gpuSceneStatus();
        std::printf("   20 still frames: +%llu writes, +%llu copy runs, +%llu updates\n",
                    st.writes - from.writes, st.copyRuns - from.copyRuns,
                    st.updates - from.updates);
        CHECK(st.writes == from.writes && st.copyRuns == from.copyRuns &&
                  st.updates == from.updates,
              "A STILL SCENE STAGES NOTHING AND COPIES NOTHING");
    }

    // =====================================================================
    // 4. A SWAP-REMOVE RE-WRITES EXACTLY ONE SLOT
    // =====================================================================
    {
        // The item in slot 0 goes. The dense index moves the LAST item into
        // slot 0, so slot 0 must now hold that item's pose and node id, and the
        // table must be one slot shorter.
        GpuSceneEntry last;
        const unsigned lastSlot = scene->gpuSceneStatus().slotCount - 1u;
        scene->gpuSceneEntry(lastSlot, last);
        GpuSceneEntry dying;
        scene->gpuSceneEntry(0u, dying);
        const GpuSceneStatus from = scene->gpuSceneStatus();
        scene->removeNode(nodes[0]);
        render(1);
        const GpuSceneStatus st = scene->gpuSceneStatus();
        GpuSceneEntry now;
        scene->gpuSceneEntry(0u, now);
        std::printf("   swap-remove: slots %u -> %u, lastDirty=%u, slot 0's node %u -> %u "
                    "(the tail's was %u)\n",
                    from.slotCount, st.slotCount, st.lastDirty, dying.nodeId, now.nodeId, last.nodeId);
        CHECK(st.slotCount == from.slotCount - 1u, "the item index is one slot shorter");
        CHECK(now.nodeId == last.nodeId,
              "THE TAIL'S ITEM IS IN THE FREED SLOT, with its own node id");
        CHECK(sameFloats(last.world, now.world, 12),
              "...and its own pose — never the dead item's left behind");
        CHECK(st.lastDirty == 1u, "and exactly ONE slot was re-written for it");
        std::vector<GpuSceneEntry> device;
        scene->gpuSceneDeviceEntries(0u, 1u, device);
        CHECK(!device.empty() && sameFloats(device[0].world, now.world, 12) &&
                  device[0].nodeId == now.nodeId,
              "...on the device as well as in the mirror");
        nodes.erase(nodes.begin());
    }

    // =====================================================================
    // 5. A GROW KEEPS EVERY ENTRY
    // =====================================================================
    {
        const GpuSceneStatus from = scene->gpuSceneStatus();
        // The entry that must survive the doubling, by value.
        GpuSceneEntry keep;
        scene->gpuSceneEntry(5u, keep);
        unsigned added = 0u;
        while (scene->gpuSceneStatus().slotCount < from.capacity + 1u) {
            const NodeId n = scene->createNode();
            scene->setNodeTransform(n, Vec3(0.0f, 40.0f + float(added) * 0.1f, 0.0f),
                                    Quat(0.0f, 0.0f, 0.0f, 1.0f), Vec3(0.1f, 0.1f, 0.1f));
            scene->attachMesh(n, filler, fillerMat);
            nodes.push_back(n);
            ++added;
        }
        render(2);
        const GpuSceneStatus st = scene->gpuSceneStatus();
        std::printf("   grow: %u -> %u slots, capacity %u -> %u, grows %llu -> %llu\n",
                    from.slotCount, st.slotCount, from.capacity, st.capacity, from.grows, st.grows);
        CHECK(st.capacity > from.capacity && st.grows > from.grows,
              "THE TABLE DOUBLED (it did not reallocate per frame)");
        std::vector<GpuSceneEntry> device;
        const bool read = scene->gpuSceneDeviceEntries(0u, st.slotCount, device);
        CHECK(read && device.size() >= 6u && sameFloats(device[5].world, keep.world, 12) &&
                  device[5].nodeId == keep.nodeId && device[5].flags == keep.flags,
              "A GROW KEPT EVERY ENTRY: the pre-grow slot reads back identical out of the "
              "NEW buffer");
        unsigned disagree = 0u;
        for (unsigned i = 0; i < st.slotCount && read; ++i) {
            GpuSceneEntry mirror;
            scene->gpuSceneEntry(i, mirror);
            if (!sameFloats(mirror.world, device[i].world, 12) ||
                mirror.nodeId != device[i].nodeId || mirror.flags != device[i].flags)
                ++disagree;
        }
        CHECK(read && disagree == 0u, "...and the whole table still agrees with the mirror");
    }

    // =====================================================================
    // 6. THE TABLES EXIST WITH RAYS OFF
    // =====================================================================
    {
        const bool was = e->rayTracing();
        e->setRayTracing(false);
        Scene *noRays = e->createScene("gpu_scene_norays");
        CHECK(noRays != nullptr, "a scene with the no-rays latch in force");
        if (noRays) {
            View *v2 = e->createOffscreenView("gpu_scene_norays", kSize, kSize,
                                              Colour(0.0f, 0.0f, 0.0f, 1.0f));
            v2->setScene(noRays);
            noRays->setAmbient(Colour(0.25f, 0.25f, 0.25f), Colour(0.25f, 0.25f, 0.25f));
            const MeshId m2 = noRays->createMesh(enginetest::unitCubeMesh());
            const MaterialId t2 = noRays->createPbrMaterial(fp);
            for (unsigned i = 0; i < 8u; ++i) {
                const NodeId n = noRays->createNode();
                noRays->setNodeTransform(n, Vec3(float(i), 0.0f, 0.0f),
                                         Quat(0.0f, 0.0f, 0.0f, 1.0f), Vec3(1.0f, 1.0f, 1.0f));
                noRays->attachMesh(n, m2, t2);
            }
            v2->setCamera(enginetest::testCameraDescLookAt(Vec3(4.0f, 4.0f, 10.0f), Vec3(0, 0, 0)));
            render(3);
            const GpuSceneStatus st = noRays->gpuSceneStatus();
            std::printf("   rays off: live=%d slotCount=%u meshEntries=%u\n", st.live ? 1 : 0,
                        st.slotCount, st.meshEntries);
            CHECK(st.live && st.slotCount == 8u && st.meshEntries == 1u,
                  "THE GPU SCENE IS NOT THE RAY TIER'S: rays off, the same tables");
            e->destroyView(v2);
            e->destroyScene(noRays);
        }
        e->setRayTracing(was);
    }

    // =====================================================================
    // 7. THE DIRTY SCAN'S COST AT SCALE (a measurement — the design's premise)
    // =====================================================================
    {
        const char *bench = std::getenv("JAH_GPU_SCENE_BENCH");
        std::vector<unsigned> steps = { 1000u };
        if (bench && *bench && *bench != '0') steps.push_back(8001u);
        unsigned made = unsigned(nodes.size());
        const NodeId mover = nodes.back();
        for (unsigned target : steps) {
            for (; made < target; ++made) {
                const NodeId n = scene->createNode();
                scene->setNodeTransform(n,
                    Vec3(float(int(made % 100u) - 50) * 0.5f, 0.5f,
                         float(int(made / 100u)) * 0.5f - 25.0f),
                    Quat(0.0f, 0.0f, 0.0f, 1.0f), Vec3(0.2f, 0.2f, 0.2f));
                scene->attachMesh(n, filler, fillerMat);
                nodes.push_back(n);
            }
            render(3);
            // The CHEAPEST of five forced scans: a walk's cost is a walk's cost,
            // and the slowest read is contention (engine.gi_scan's rule).
            // BOTH FORMS, because they differ by a factor of three and the
            // frame only ever pays the cheap one: with the scene graph already
            // updated (the frame path, a cached read per node) and without it (a
            // reader before the frame, where Ogre recomputes each derived
            // transform up the parent chain — it keeps no per-node dirty bit).
            for (int form = 0; form < 2; ++form) {
                const bool current = form == 0;
                double best = 0.0;
                for (int i = 0; i < 5; ++i) {
                    scene->setNodeTransform(mover, Vec3(0.0f, 30.0f + 0.01f * float(i), 0.0f),
                                            Quat(0.0f, 0.0f, 0.0f, 1.0f), Vec3(0.2f, 0.2f, 0.2f));
                    scene->measureGpuSceneScan(current);
                    const double us = scene->gpuSceneStatus().lastScanMicros;
                    if (us > 0.0 && (best == 0.0 || us < best)) best = us;
                    e->renderOneFrame();
                }
                std::printf("   MEASURED: %u items, ONE mover -> one dirty scan costs %.0f us "
                            "(%s)\n",
                            unsigned(nodes.size()), best,
                            current ? "the FRAME's form: the graph is already updated"
                                    : "a PRE-FRAME reader's form: Ogre recomputes each transform");
            }
            // THE ROW THIS SLICE EXISTS FOR: what writing the ray tier's
            // instance array costs now that it is read out of the table instead
            // of re-derived per item (`world.rayQueryStatus().gatherMs`). The
            // walk it replaces measured 3.4-4.0 ms at 8,001 instances in Debug
            // (inventory LAT-L8 / F11-WALKS).
            const RayQueryStatus rq = scene->rayQueryStatus();
            std::printf("   MEASURED: the ray tier at %d instances (%d structures): "
                        "instance write %.3f ms CPU\n",
                        rq.instances, rq.blasCount, double(rq.gatherMs));
        }
        if (!bench)
            std::printf("   (the 8,001-item arm is skipped — set JAH_GPU_SCENE_BENCH=1)\n");
    }

    e->setTransformWriteCounter(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    std::printf("%s: %d failures\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
