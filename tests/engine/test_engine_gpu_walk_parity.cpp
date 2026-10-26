// engine.gpu_walk_parity (archived: archive/tests/engine) — THE NAMED WALK'S GUARD (ENGINE-UPGRADE-2B stage 2).
//
// The dirty scan walks only the slots the frame NAMED (the change set's moved subtrees and the
// engine's seams); every flags word that moves without a transform is marked by its seam
// (markGpuSlotDirty, the PBS change log). Before the named walk, a whole walk with a per-slot
// flags compare (the backstop) re-staged whatever a seam forgot to mark — on every frame that
// moved anything — and so HID the forgetting. This row is what replaces that safety: with the
// arm `gpuscene.walkParity` on, after every frame's compose the engine compares EVERY slot's
// world transform and flags word with what the table holds; a slot the table does not hold is
// a MISS (GpuSubstrateStatus::walkMissed). The bar: 0 misses, on a scene where every kind of
// writer of a slot's transform or flags word runs — a mover, a material edit (the metal answer),
// a cull edit (a cull twin), an alpha-test edit, a cast-shadow flip, a hidden parent, a drag
// promoted and demoted, a mesh rebuild, a datablock swap, a planar reflector armed and disarmed —
// and (the stage 3 read, finding E) the detach tail (swap-remove MOVES another slot into the
// hole: first), a light-mask write, the Atom switch off and on, a render-queue refile (a Blend
// edit), a skin attached (a bone pose writes the bone buffer, never the slot's transform or flags
// word: not a writer of the table), a mobility flip (setNodeMovable) and a nodeless write (a
// transform on a node that carries no mesh, its slot-carrying child moving under it).
// Each writer is followed by frames; each one must also have CHANGED what the table holds for
// its node (its flags word or its transform), or the step tested nothing. AND THE READS AFTER
// THE SCENE GRAPH (ENGINE-UPGRADE-U4 decision 13.2): a recomputing read in the frame's
// `engine.plan` would overwrite a node's SoA-derived transform with its scalar recompute (an
// ulp apart) and the table would miss it — a decal riding a moving cube (the GI walk reads its
// box there) and the view's camera socketed on a moving cube (the plan reads its position there).
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/GpuSubstrate.h"
#include "jahshaka/engine/EngineInspect.h"
#include "../support/enginetesthelpers.h"
#include "../support/gesture.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        char m_[512]; std::snprintf(m_, sizeof m_, __VA_ARGS__);                \
        if (cond) std::printf("ok: %s\n", m_);                                  \
        else { std::printf("FAIL: %s\n", m_); ++failures; }                     \
    } while (0)

// engine.gpu_walk_parity_free (archived: archive/tests/engine) (`--free-running`, ENGINE-UPGRADE-U3 decision 56): THE INTERPOLATED POSE IS THE TABLE'S.
// The owner's mode on its render thread: a document mover recorded at 60 Hz steps and drawn by FREE frames at a 90 Hz
// display period (inspect::EngineEcho::presentationFrame, an exact count) — every in-between pose the interpolation
// writes, and the cur it puts back when the motion stops, must be named to the GPU-scene walk, or the table rests
// between grid states while the raster shows another pose. The guard's arm compares every slot after every compose:
// 0 misses while it moves and after it stops.
static int freeRunningArm()
{
    std::string err;
    EngineConfig cfg = enginetest::testEngineConfig("test-engine-gpu-walk-parity-free-ogre.log");
    cfg.renderThread = true;
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    const inspect::EngineEcho *echo = inspect::engine(e);
    if (!echo) { std::printf("FAIL: the engine's probes\n"); return 1; }
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("parity-free", 96, 96, Colour(0.05f, 0.06f, 0.08f, 1.0f), jahshaka::engine::OffscreenContract::Live);
    Scene *scene = e->createScene("parity-free");
    if (!view || !scene) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.3f, 0.3f, 0.3f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 12.0f), Vec3(0.0f, 0.5f, 0.0f));
    const MeshId cubeMesh = scene->createMesh(enginetest::unitCubeMesh());
    std::vector<NodeId> movers;
    for (int i = 0; i < 4; ++i) {
        const NodeId n = scene->createDocumentNode(reserveNodeId());
        PbrParams p;
        p.albedo = Colour(0.3f + 0.1f * float(i), 0.5f, 0.6f);
        scene->attachMesh(n, cubeMesh, scene->createPbrMaterial(p));
        movers.push_back(n);
    }
    // ON ITS RENDER THREAD every engine entry is a request (the affinity law); the probes ride the same door.
    const auto arm = [&](double v) { bool ok = false; e->request([&](Engine &x) { ok = x.setArm("gpuscene.walkParity", v); }); return ok; };
    const auto status = [&]() { GpuSubstrateStatus st; e->request([&](Engine &) { st = gpuSubstrateStatus(*scene); }); return st; };
    const auto record = [&](unsigned long long step) {
        FrameInput in;
        in.frameDelta = 1.0f / 60.0f;
        in.stepSerial = step;
        in.stepPeriod = 1.0f / 60.0f;
        FrameInputScene part;
        part.scene = scene;
        for (size_t i = 0; i < movers.size(); ++i) {
            NodeRecord r;
            r.id = movers[i];
            r.fields = NodeRecord::Trs;
            r.position = Vec3(float(i) - 1.5f + 0.05f * float(step), 0.3f, 0.0f);
            r.rotation = Quat(0.0f, 0.0f, 0.0f, 1.0f);
            r.scale = Vec3(0.4f, 0.4f, 0.4f);
            part.nodes.push_back(r);
            part.changes.moved.push_back(r.id);
        }
        in.scenes.push_back(part);
        e->submitFrame(in);
    };
    unsigned long long step = 1;
    record(step);
    enginetest::frames(e, 6);
    CHECK(arm(1.0), "the guard's arm");
    enginetest::frames(e, 2);
    const GpuSubstrateStatus s0 = status();
    CHECK(s0.live && s0.walkChecked > 0 && s0.walkMissed == 0, "the guard runs on a settled scene (%llu checked, %llu "
          "missed)", s0.walkChecked, s0.walkMissed);
    e->setDisplayPeriod(1.0f / 90.0f);
    auto *ech = const_cast<inspect::EngineEcho *>(echo);
    int between = 0;
    // MOVING: three free frames per two steps (90 over 60), the records arriving between them.
    for (int pair = 0; pair < 8; ++pair)
        for (int k : { 2, 1 }) {
            record(++step);
            for (int f = 0; f < k; ++f) {
                ech->presentationFrame();
                e->latchSnapshot();
                const float a = e->published().presentAlpha;
                if (a > 1e-4f && a < 1.0f - 1e-4f) ++between;
            }
        }
    const GpuSubstrateStatus s1 = status();
    // STOPPED: no more records, the free frames go on — the interpolation puts cur back, named.
    for (int f = 0; f < 6; ++f) ech->presentationFrame();
    const GpuSubstrateStatus s2 = status();
    std::printf("    free frames drawn between two grid states: %d; misses moving %llu, after the stop %llu "
                "(%llu slots checked)\n", between, s1.walkMissed - s0.walkMissed, s2.walkMissed - s1.walkMissed,
                s2.walkChecked - s0.walkChecked);
    CHECK(between >= 8, "the free frames drew in-between poses (%d)", between);
    CHECK(s1.walkMissed == s0.walkMissed, "every interpolated pose is the table's while it moves (%llu missed)",
          s1.walkMissed - s0.walkMissed);
    CHECK(s2.walkMissed == s1.walkMissed, "...and the cur put back when it stops (%llu missed)",
          s2.walkMissed - s1.walkMissed);
    arm(0.0);
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "engine.gpu_walk_parity_free: FAILED\n" : "engine.gpu_walk_parity_free: PASS\n");
    return failures ? 1 : 0;
}

// engine.gpuscene_identity (`--compose-identity`, ENGINE-UPGRADE-U4 stage 4): THE PARALLEL COMPOSE IS THE SERIAL ONE, BYTE
// FOR BYTE. The compose runs inside the compare's task (each worker share stages its own slots' rows) and replays what
// is not lane-local — the Atom route, the material store's queue, the observers — after the barrier in slot order. The
// arm gpuscene.serialCompose composes after the barrier on the frame's thread instead. A scene with 8 workers and 1,024
// moving cubes (>= 512 named slots: the walk goes wide) is walked with the digest on (every composed slot's index, row
// and tail, in replay order), the poses alternating between two states so frame f and f + 2 compose the same input: the
// pairs are compared serial against parallel, A B B A. The row also runs under the clang TSan arm (tsan-render).
// THE RAY FEED (RAY-FEED-OBSERVER-1): the ray tier LIVE on the view (the viewport's post chain), so its change feed is
// an observer with a lane half and its traced set — records, keys, pending key events, sets, words, rows — is folded
// into the digest after the replay; the scene holds the four kinds of slot it keeps apart, every one changing with the
// pose: a CHAINED slot (a two-level cube 2.5 m from the camera or 400 m away, its scale alternating so the far copy's
// hand-over width moves and the slot owes the ordered body in the lane - its near copy is level 0 wherever it stands
// since PHOTON-7 stage 6, ONE RAY LEVEL PER MESH), an ALPHA slot (a Cutout cube), a
// RIGGED slot (a GPU-skinned cube) and a MESH SWAP (a cube re-attached to the other of two meshes every frame).
static int composeIdentityArm()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-engine-gpuscene-identity-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("identity", 96, 96, Colour(0.05f, 0.06f, 0.08f, 1.0f), jahshaka::engine::OffscreenContract::Live);
    Scene *scene = e->createScene("identity", 8u);
    if (!view || !scene) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.3f, 0.3f, 0.3f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 30.0f, 40.0f), Vec3(0.0f, 0.0f, 0.0f));
    const MeshId cubeMesh = scene->createMesh(enginetest::unitCubeMesh());
    const int kCubes = 1024;
    std::vector<NodeId> cubes;
    for (int i = 0; i < kCubes; ++i) {
        const NodeId n = scene->createDocumentNode(reserveNodeId());
        PbrParams p;
        p.albedo = Colour(0.2f + 0.0005f * float(i), 0.5f, 0.6f);
        p.roughness = 0.4f + 0.0004f * float(i);
        scene->attachMesh(n, cubeMesh, scene->createPbrMaterial(p));
        cubes.push_back(n);
    }
    // THE RAY TIER LIVE: the viewport's post chain on the offscreen view (the editor screenshot's door).
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    view->setPostFx(fx);
    const bool rays = e->rayQueryAvailable() && e->rayTracing();
    MeshData chained = enginetest::unitCubeMesh();
    chained.lodIndices.push_back(chained.indices);
    chained.lodBounds.push_back(0.3f);
    const MeshId chainMesh = scene->createMesh(chained);
    const MeshId swapA = scene->createMesh(enginetest::unitCubeMesh()), swapB = scene->createMesh(enginetest::unitCubeMesh());
    MeshData skinned = enginetest::unitCubeMesh();
    {
        const size_t nv = skinned.positions.size() / 3;
        skinned.blendIndices.assign(nv * 4, 0);
        skinned.blendWeights.assign(nv * 4, 0.0f);
        for (size_t v = 0; v < nv; ++v) skinned.blendWeights[v * 4] = 1.0f;
    }
    const MeshId skinMesh = scene->createMesh(skinned);
    SkeletonDesc skel;
    skel.id = "identity/rig";
    BoneDesc bone;
    bone.name = "root";
    skel.bones.push_back(bone);
    PbrParams cut;
    cut.alphaMode = PbrAlphaMode::Cutout;
    cut.alphaCutoff = 0.5f;
    const MaterialId plainMat = scene->createPbrMaterial(PbrParams()), cutMat = scene->createPbrMaterial(cut);
    const NodeId chainedNode = scene->createDocumentNode(reserveNodeId());
    scene->attachMesh(chainedNode, chainMesh, plainMat);
    const NodeId alphaNode = scene->createDocumentNode(reserveNodeId());
    scene->attachMesh(alphaNode, cubeMesh, cutMat);
    const NodeId rigNode = scene->createDocumentNode(reserveNodeId());
    scene->attachSkinnedMesh(rigNode, skinMesh, plainMat, skel);
    const NodeId swapNode = scene->createDocumentNode(reserveNodeId());   // the last slot: its free moves no other slot
    scene->attachMesh(swapNode, swapA, plainMat);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.bounces = true;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;
    unsigned long long step = 0;
    const auto record = [&](int pose, bool send = true) {
        FrameInput in;
        in.frameDelta = 1.0f / 60.0f;
        in.stepSerial = send ? ++step : step;   // a frame that sends nothing is no new step (it would interpolate)
        in.stepPeriod = 1.0f / 60.0f;
        FrameInputScene part;
        part.scene = scene;
        part.gi = gi;
        for (int i = 0; send && i < kCubes; ++i) {
            NodeRecord r;
            r.id = cubes[size_t(i)];
            r.fields = NodeRecord::Trs;
            r.position = Vec3(float(i % 32) * 1.2f - 19.0f, 0.3f + 0.05f * float(pose), float(i / 32) * 1.2f - 19.0f);
            r.rotation = Quat(0.0f, 0.0f, 0.0f, 1.0f);
            r.scale = Vec3(0.5f, 0.5f, 0.5f);
            part.nodes.push_back(r);
            part.changes.moved.push_back(r.id);
        }
        if (send) {
            // The ray feed's four slots: the chained cube 2.5 m from the camera or 400 m away (its scale alternating), the
            // alpha and the rigged cubes stepping with the pose, the swap re-attached to the pose's mesh.
            const auto place = [&](NodeId id, const Vec3 &at, float scale) {
                NodeRecord r;
                r.id = id;
                r.fields = NodeRecord::Trs;
                r.position = at;
                r.rotation = Quat(0.0f, 0.0f, 0.0f, 1.0f);
                r.scale = Vec3(scale, scale, scale);
                part.nodes.push_back(r);
                part.changes.moved.push_back(id);
            };
            place(chainedNode, pose ? Vec3(0.0f, 29.0f, -360.0f) : Vec3(0.0f, 28.5f, 38.0f), pose ? 0.7f : 0.5f);
            place(alphaNode, Vec3(-21.0f, 0.3f + 0.05f * float(pose), 0.0f), 0.5f);
            place(rigNode, Vec3(21.0f, 0.3f + 0.05f * float(pose), 0.0f), 0.5f);
            place(swapNode, Vec3(0.0f, 0.3f + 0.05f * float(pose), 21.0f), 0.5f);
            scene->attachMesh(swapNode, pose ? swapB : swapA, plainMat);
        }
        in.scenes.push_back(part);
        enginetest::frames(e, 1, in);
    };
    for (int f = 0; f < 6; ++f) record(f % 2);
    const inspect::SceneEcho *echo = inspect::scene(scene);
    CHECK(echo && e->setArm("gpuscene.composeDigest", 1.0), "the compose's digest arm");
    if (!echo) return 1;
    // THE PATTERN (decision 18.4): the OLD compose (gpuscene.serialCompose: per slot compose -> tail -> stage -> every
    // observer whole) on frames 0-1 and 8-9, the parallel one on 2-7 — with a THROW in a worker's share on frame 4 and in
    // the frame thread's share on frame 5 (gpuscene.throwInLane, decision 18.3: recomposed on the frame's thread). The
    // poses alternate, so every even frame composes the same input after the same input, and every odd one: the
    // digests of a parity class — the staged rows, the tails and the observers' notifies, in their order — must all be
    // equal, serial against parallel against recovered.
    const int pattern[10] = { 1, 1, 0, 0, 0, 0, 0, 0, 1, 1 };
    // share + 1 (9 = the frame thread's share 8) throws before its first stage; 17 + share throws AFTER it (decision
    // 22.4 a: the row staged, its tail and lane halves not collected — the redo starts the share over, the row kept).
    const int throwAt[10] = { 0, 0, 0, 0, 3, 9, 0, 20, 0, 0 };
    unsigned long long digest[10] = {}, recomposes[10] = {}, raysTold[10] = {}, raysOwed[10] = {};
    unsigned composed[10] = {}, shares[10] = {}, atoms[10] = {}, queues[10] = {};
    const auto raysOf = [](const inspect::SceneEcho::GpuWalk &g, unsigned long long &told, unsigned long long &owed) {
        for (const inspect::SceneEcho::GpuWalk::Observer &o : g.observers)
            if (o.name == "rays" && o.laneHalf) { told = o.told; owed = o.owed; return true; }
        return false;
    };
    unsigned long long told0 = 0, owed0 = 0;
    const bool raysLive = raysOf(echo->gpuWalk(), told0, owed0);
    for (int f = 0; f < 10; ++f) {
        e->setArm("gpuscene.serialCompose", double(pattern[f]));
        e->setArm("gpuscene.throwInLane", double(throwAt[f]));
        record(f % 2);
        const inspect::SceneEcho::GpuWalk g = echo->gpuWalk();
        unsigned long long told = 0, owed = 0;
        raysOf(g, told, owed);
        raysTold[f] = told - told0;
        raysOwed[f] = owed - owed0;
        told0 = told;
        owed0 = owed;
        digest[f] = g.composeDigest;
        composed[f] = g.slotsComposed;
        shares[f] = g.shares;
        atoms[f] = g.tailAtoms;
        queues[f] = g.tailQueues;
        recomposes[f] = g.shareRecomposes;
    }
    e->setArm("gpuscene.throwInLane", 0.0);
    e->setArm("gpuscene.serialCompose", 0.0);
    e->setArm("gpuscene.composeDigest", 0.0);
    for (int f = 0; f < 10; ++f)
        std::printf("   frame %d (%s%s): %u slots composed on %u shares, tails %u atom / %u queued, recomposes %llu, "
                    "digest %016llx\n", f, pattern[f] ? "serial" : "parallel",
                    throwAt[f] ? (throwAt[f] == 9 ? ", the frame thread's share throws"
                                                  : throwAt[f] > 16 ? ", a worker's share throws after its stage"
                                                                    : ", a worker's share throws") : "",
                    composed[f], shares[f], atoms[f], queues[f], recomposes[f], digest[f]);
    for (int f = 0; f < 10; ++f)
        if (!pattern[f])
            CHECK(composed[f] >= 512u && shares[f] == 9u, "frame %d went wide: %u slots on %u shares", f, composed[f],
                  shares[f]);
    // THE RAY FEED IN THE DIGEST (RAY-FEED-OBSERVER-1): live, lane-split, its lane halves answering the movers and its
    // ordered half replaying the slots whose facts moved (the chained slot's far hand-over width).
    if (rays) {
        CHECK(raysLive, "the ray tier is live: its change feed is a registered observer with a lane half ('rays')");
        for (int f = 0; f < 10; ++f) {
            std::printf("   frame %d: the ray feed handed %llu composed slots, owed %llu\n", f, raysTold[f], raysOwed[f]);
            if (pattern[f] || throwAt[f]) continue;   // a serial frame tells every slot whole; a throw's redo varies
            CHECK(raysTold[f] >= 512u && raysOwed[f] >= 1u && raysOwed[f] * 10u <= raysTold[f],
                  "frame %d: the ray feed's lane halves answered the movers (%llu of %llu owed the ordered body: the "
                  "chained slot's moved hand-over width, never the transform-only movers)", f, raysOwed[f], raysTold[f]);
        }
    } else {
        std::printf("ok:   no ray-query device here: the ray feed is not a registered observer (its digest is not folded)\n");
    }
    for (int f = 2; f < 10; ++f)
        CHECK(digest[f] == digest[f % 2], "frame %d (%s) folds what frame %d (serial) folded: the same rows, tails and "
              "notifies in the same order (%016llx / %016llx)", f, pattern[f] ? "serial" : "parallel", f % 2, digest[f],
              digest[f % 2]);
    CHECK(recomposes[4] == recomposes[3] + 1u && recomposes[5] == recomposes[4] + 1u && recomposes[6] == recomposes[5] &&
          recomposes[7] == recomposes[6] + 1u && recomposes[9] == recomposes[7],
          "each throw recomposed its one share on the frame's thread, and nothing after it (%llu, %llu, %llu, %llu)",
          recomposes[3], recomposes[4], recomposes[5], recomposes[7]);
    CHECK(digest[6] == digest[0] && digest[8] == digest[0] && digest[9] == digest[1], "...and the frames after the throws "
          "composed whole: every barrier paired");

    // THE SHARE THAT THROWS EVERY TIME (decision 22.4 b): share 3 throws in its lane AND in the frame thread's redo on
    // frame 10 (pose 0). The frame says so, every other share's replay runs, and the share's slots are owed to the next
    // compose (MARKED: a frame nothing moves in runs no walk, so a name would wait): frame 11 sends NOTHING and is no
    // new step — only the marks compose there (to pose 0) — and frames 12 (pose 1) and 13 (pose 0) fold exactly what
    // frames 1 and 0 folded: the table caught up. (Unmarked, the share's slots keep pose 1 in the table while Ogre holds
    // pose 0; frame 12's compare then finds them equal and frame 12 folds 910 slots, not 1024 — measured with names.)
    const unsigned long long giveUps0 = echo->gpuWalk().shareGiveUps;
    e->setArm("gpuscene.composeDigest", 1.0);
    record(1);   // from pose 1: frame 10's pose 0 moves every slot
    e->setArm("gpuscene.throwInLane", 36.0);   // 33 + share 3
    record(0);
    const inspect::SceneEcho::GpuWalk g10 = echo->gpuWalk();
    const std::string err10 = e->published().frameError;
    e->setArm("gpuscene.throwInLane", 0.0);
    record(0, false);
    record(1);
    const unsigned long long d12 = echo->gpuWalk().composeDigest;
    record(0);
    const unsigned long long d13 = echo->gpuWalk().composeDigest;
    e->setArm("gpuscene.composeDigest", 0.0);
    std::printf("   frame 10 (share 3 throws every time): %u composed, give-ups %llu -> %llu; the frame's error: %s\n"
                "   frame 11 (nothing sent, no step: nothing moves, no walk runs — only the marks compose); frames 12 / 13 "
                "digests %016llx / %016llx\n", g10.slotsComposed, giveUps0, g10.shareGiveUps, err10.c_str(), d12, d13);
    CHECK(g10.shareGiveUps == giveUps0 + 1u, "the share that threw twice gave up once (%llu -> %llu)", giveUps0,
          g10.shareGiveUps);
    CHECK(err10.find("again on the frame's thread") != std::string::npos, "...and the frame said so");
    CHECK(g10.slotsComposed > 0u && g10.slotsComposed < unsigned(kCubes), "...while every other share composed "
          "(%u of %d)", g10.slotsComposed, kCubes);
    CHECK(d12 == digest[1] && d13 == digest[0], "...and the table caught up: frames 12 and 13 fold what frames 1 and 0 "
          "folded (%016llx / %016llx)", d12, d13);
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "engine.gpuscene_identity: FAILED\n" : "engine.gpuscene_identity: PASS\n");
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--compose-identity") return composeIdentityArm();
    if (argc > 1 && std::string(argv[1]) == "--free-running") return freeRunningArm();
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-engine-gpu-walk-parity-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("parity", 96, 96, Colour(0.05f, 0.06f, 0.08f, 1.0f), jahshaka::engine::OffscreenContract::Live);
    Scene *scene = e->createScene("parity");
    if (!view || !scene) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.3f, 0.3f, 0.3f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 12.0f), Vec3(0.0f, 0.5f, 0.0f));
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.8f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.55f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(40.0f, 0.1f, 40.0f));
    std::vector<NodeId> cubes;
    std::map<NodeId, MaterialId> matOf;
    const MeshId cubeMesh = scene->createMesh(enginetest::unitCubeMesh());
    for (int i = 0; i < 24; ++i) {
        const NodeId n = scene->createNode();
        PbrParams p;
        p.albedo = Colour(0.2f + 0.02f * float(i), 0.5f, 0.6f);
        p.roughness = 0.7f;
        const MaterialId m = scene->createPbrMaterial(p);
        scene->attachMesh(n, cubeMesh, m);
        matOf[n] = m;
        enginetest::setNodePosition(scene, n, Vec3(float(i % 6) - 2.5f, 0.3f, float(i / 6) - 2.0f));
        enginetest::setNodeScale(scene, n, Vec3(0.4f, 0.4f, 0.4f));
        cubes.push_back(n);
    }
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.bounces = true;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;
    gi.dragMoverChannel = true;
    enginetest::frames(e, 4);
    CHECK(scene->setGlobalIllumination(gi), "the GI chain (the drag mover channel needs a cascade chain)");
    enginetest::frames(e, 8);
    CHECK(e->setArm("gpuscene.walkParity", 1.0), "the guard's arm");
    enginetest::frames(e, 2);
    const GpuSubstrateStatus s0 = gpuSubstrateStatus(*scene);
    CHECK(s0.live && s0.walkChecked > 0, "the guard runs (%llu slots checked)", s0.walkChecked);
    CHECK(s0.walkMissed == 0, "a settled scene: 0 misses (%llu)", s0.walkMissed);

    // Each writer: run it, render frames, the misses stay 0 and the walk re-composed something.
    struct Step { const char *name; std::function<void()> run; };
    const NodeId a = cubes[3], b = cubes[7], c = cubes[11], d = cubes[13], f = cubes[17], g = cubes[19],
                 h = cubes[21], parent = cubes[1], child = cubes[2];
    const MaterialId other = scene->createPbrMaterial(PbrParams());
    const MeshId rebuilt = scene->createMesh(enginetest::unitCubeMesh());
    // The nodeless write's group: a node with no mesh, one cube parented under it (settled first).
    const NodeId group = scene->createNode(), grouped = cubes[9];
    scene->setNodeParent(grouped, group);
    // The skin: a GPU-skinned cube (one bone, every vertex weighted 1 to it).
    MeshData skinned = enginetest::unitCubeMesh();
    const size_t nv = skinned.positions.size() / 3;
    skinned.blendIndices.assign(nv * 4, 0);
    skinned.blendWeights.assign(nv * 4, 0.0f);
    for (size_t v = 0; v < nv; ++v) skinned.blendWeights[v * 4] = 1.0f;
    const MeshId skinMesh = scene->createMesh(skinned);
    SkeletonDesc skel;
    skel.id = "parity/rig";
    BoneDesc bone;
    bone.name = "root";
    skel.bones.push_back(bone);
    const NodeId rig = scene->createNode();
    // The decal's rider: a decal node parented under a cube (its box is read by the GI walk after the scene graph).
    TextureId decalTex = 0;
    {
        const unsigned n = 16;
        unsigned char hdr[18];
        std::memset(hdr, 0, sizeof hdr);
        hdr[2] = 2; hdr[12] = (unsigned char)n; hdr[14] = (unsigned char)n; hdr[16] = 32; hdr[17] = 0x28;
        std::vector<unsigned char> px(size_t(n) * n * 4u, 255u);
        if (FILE *tf = std::fopen("gpu_walk_parity_decal.tga", "wb")) {
            std::fwrite(hdr, 1, sizeof hdr, tf);
            std::fwrite(px.data(), 1, px.size(), tf);
            std::fclose(tf);
            decalTex = scene->loadDecalTexture("gpu_walk_parity_decal.tga", DecalMap::Diffuse);
        }
    }
    const NodeId decalRider = cubes[23], decalNode = scene->createNode();
    scene->setNodeParent(decalNode, decalRider);
    DecalDesc dd;
    dd.diffuse = decalTex;
    dd.width = dd.height = dd.depth = 1.0f;
    CHECK(decalTex != 0 && scene->setDecal(decalNode, dd), "the decal on its rider (a cube)");
    const NodeId socket = cubes[22];
    enginetest::frames(e, 4);
    std::vector<Step> steps = {
        { "the detach tail (a middle slot detached: the last slot is moved into its hole)", [&] {
              scene->detachMesh(cubes[5]); } },
        { "a mover", [&] { enginetest::setNodePosition(scene, a, Vec3(-2.5f, 0.6f, -2.0f)); } },
        { "a material edit (metalness 0 -> 1: the metal answer)", [&] {
              PbrParams p; p.metalness = 1.0f; p.roughness = 0.2f;
              scene->setPbrMaterial(matOf[b], p); } },
        { "a cull edit (the node's cull twin)", [&] { scene->setNodeFaceCull(c, FaceCull::TwoSided); } },
        { "an alpha-test edit (Cutout)", [&] {
              PbrParams p; p.alphaMode = PbrAlphaMode::Cutout; p.alphaCutoff = 0.5f;
              scene->setPbrMaterial(matOf[d], p); } },
        { "a cast-shadow flip", [&] { scene->setNodeCastShadow(f, false); } },
        { "a hidden parent", [&] { scene->setNodeParent(child, parent); scene->setNodeVisible(parent, false); } },
        { "a hidden parent shown again", [&] { scene->setNodeVisible(parent, true); } },
        { "a drag promoted (a gesture, a still moved under it)", [&] {
              enginetest::gesture(e, scene, { g });
              enginetest::setNodePosition(scene, g, Vec3(1.5f, 0.3f, 1.0f));
              enginetest::frames(e, 1);
              enginetest::setNodePosition(scene, g, Vec3(1.7f, 0.3f, 1.0f)); } },
        { "the drag demoted (the gesture ends)", [&] { enginetest::gesture(e, scene, {}); } },
        { "a mesh rebuild (attachMesh again)", [&] { scene->attachMesh(h, rebuilt, matOf[h]); } },
        { "a datablock swap (setNodeMaterial)", [&] { scene->setNodeMaterial(a, other); } },
        { "a planar reflector armed", [&] {
              PlanarReflectionParams pr; pr.budget = 1; pr.resolution = 256;
              scene->setPlanarReflections(pr);
              scene->setNodePlanarReflector(ground, true); } },
        { "a planar reflector disarmed", [&] { scene->setNodePlanarReflector(ground, false); } },
        { "a light-mask write", [&] { scene->setNodeLightMask(cubes[15], 0x2u); } },
        { "the Atom switch off", [&] { scene->setAtomDrawEnabled(false); } },
        { "the Atom switch on", [&] { scene->setAtomDrawEnabled(true); } },
        { "a render-queue refile (a Blend edit)", [&] {
              PbrParams p; p.alphaMode = PbrAlphaMode::Blend; p.alpha = 0.5f;
              scene->setPbrMaterial(matOf[cubes[16]], p); } },
        { "a skin attached", [&] {
              scene->attachSkinnedMesh(rig, skinMesh, matOf[cubes[18]], skel);
              enginetest::setNodePosition(scene, rig, Vec3(2.5f, 0.3f, 2.0f)); } },
        { "a mobility flip (setNodeMovable)", [&] { scene->setNodeMovable(cubes[20], true); } },
        { "a nodeless write (a mesh-less group moved, its cube under it)", [&] {
              enginetest::setNodePosition(scene, group, Vec3(0.0f, 0.5f, 0.0f)); } },
        { "a decal's rider moved (the GI walk's decal box, after the scene graph)", [&] {
              enginetest::setNodePosition(scene, decalRider, Vec3(2.5f, 0.6f, 1.0f)); } },
        { "a socketed camera (the view's camera on a moving cube: the plan's camera, after the scene graph)", [&] {
              view->setCameraNode(socket);
              enginetest::setNodePosition(scene, socket, Vec3(1.5f, 0.9f, 3.0f)); } },
    };
    // THE GEOMETRY SIGNATURE'S GUARD TOO (ENGINE-UPGRADE-U4 stage 3): the same writers, every frame a record that
    // carries the scene's GI (planGi reads the signature on it), and the arm gi.geomSigParity recomputing the whole
    // order-free sum after every incremental pass — 0 misses.
    const inspect::SceneEcho *echo = inspect::scene(scene);
    CHECK(echo != nullptr && e->setArm("gi.geomSigParity", 1.0), "the signature guard's arm");
    const auto giFrames = [&](int k) {
        for (int f = 0; f < k; ++f) {
            FrameInput in;
            FrameInputScene part;
            part.scene = scene;
            part.gi = gi;
            in.scenes.push_back(part);
            enginetest::frames(e, 1, in);
        }
    };
    giFrames(2);
    for (const Step &st : steps) {
        const GpuSubstrateStatus before = gpuSubstrateStatus(*scene);
        const GpuSceneStatus gb = scene->published().gpuScene;
        const inspect::SceneEcho::GiPlan pb = echo ? echo->giPlan() : inspect::SceneEcho::GiPlan();
        st.run();
        giFrames(4);
        const GpuSubstrateStatus after = gpuSubstrateStatus(*scene);
        const GpuSceneStatus ga = scene->published().gpuScene;
        const inspect::SceneEcho::GiPlan pa = echo ? echo->giPlan() : inspect::SceneEcho::GiPlan();
        CHECK(after.walkMissed == before.walkMissed,
              "%s: the table holds every slot (%llu misses over %llu checks; last miss slot %u)", st.name,
              after.walkMissed - before.walkMissed, after.walkChecked - before.walkChecked,
              after.walkLastMissed);
        CHECK(ga.updates > gb.updates, "%s: the table took the writer's change (%llu commits)", st.name,
              (unsigned long long)(ga.updates - gb.updates));
        CHECK(pa.sigMisses == pb.sigMisses, "%s: the geometry signature is the whole sum (%llu misses over %llu "
              "passes, %llu of them full)", st.name, pa.sigMisses - pb.sigMisses, pa.sigChecks - pb.sigChecks,
              pa.sigFullPasses - pb.sigFullPasses);
    }
    {
        const inspect::SceneEcho::GiPlan p = echo ? echo->giPlan() : inspect::SceneEcho::GiPlan();
        CHECK(p.sigChecks > p.sigFullPasses, "the signature's guard checked incremental passes (%llu checks, %llu full)",
              p.sigChecks, p.sigFullPasses);
    }
    view->setCameraNode(0);
    e->setArm("gi.geomSigParity", 0.0);
    e->setArm("gpuscene.walkParity", 0.0);
    std::remove("gpu_walk_parity_decal.tga");   // the decal's image, written for this run (decision 16.5)

    // THE NAME LIST'S DEATH (decision 16.2): a record whose bits were all taken stays LISTED until the list compacts,
    // and its death must take it off the list. The exact sequence: a scene drawn a few frames (its GPU walk and GI
    // scan primed: their names are recorded from then on) and then UNDRAWN (nothing consumes those names any more),
    // no lamp (its caster scan never primes), C named for the geometry signature alone (a material swap), A and B
    // moving (named for every consumer: the list stays live, so it never compacts), the signature takes C's bit, then
    // C is removed — the list must hold no dead record.
    {
        Scene *u = e->createScene("parity-undrawn");
        View *uv = u ? e->createOffscreenView("parity-undrawn", 64, 64, Colour(0, 0, 0, 1), jahshaka::engine::OffscreenContract::Live) : nullptr;
        if (uv) {
            uv->setScene(u);
            enginetest::testCameraLookAt(uv, Vec3(0.0f, 3.0f, 8.0f), Vec3(0.0f, 0.0f, 0.0f));
        }
        const MeshId um = u ? u->createMesh(enginetest::unitCubeMesh()) : MeshId(0);
        const MaterialId m1 = u ? u->createPbrMaterial(PbrParams()) : MaterialId(0);
        PbrParams p2; p2.albedo = Colour(0.9f, 0.2f, 0.2f);
        const MaterialId m2 = u ? u->createPbrMaterial(p2) : MaterialId(0);
        NodeId abc[3] = {};
        for (int i = 0; u && i < 3; ++i) {
            abc[i] = u->createDocumentNode(reserveNodeId());
            u->attachMesh(abc[i], um, m1);
        }
        const inspect::SceneEcho *ue = u ? inspect::scene(u) : nullptr;
        CHECK(u && ue && abc[2], "the undrawn scene and its three cubes");
        if (u && ue) {
            GiParams ugi = gi;
            const auto uframe = [&](int moveStep) {
                FrameInput in;
                FrameInputScene part;
                part.scene = u;
                part.gi = ugi;
                for (int i = 0; moveStep >= 0 && i < 2; ++i) {
                    NodeRecord r;
                    r.id = abc[i];
                    r.fields = NodeRecord::Trs;
                    r.position = Vec3(float(i) * 2.0f, 0.1f * float(moveStep), 0.0f);
                    r.rotation = Quat(0.0f, 0.0f, 0.0f, 1.0f);
                    r.scale = Vec3(1.0f, 1.0f, 1.0f);
                    part.nodes.push_back(r);
                    part.changes.moved.push_back(r.id);
                }
                in.scenes.push_back(part);
                enginetest::frames(e, 1, in);
            };
            for (int f = 0; f < 3; ++f) uframe(f);   // drawn: the walk and the scan primed
            if (uv) uv->setScene(nullptr);          // ...then undrawn
            uframe(-1);
            u->setNodeMaterial(abc[2], m2);   // C: named for the signature alone
            uframe(1);                        // A and B move; the signature's plan takes C's bit
            uframe(2);
            const inspect::SceneEcho::GpuWalk g0 = ue->gpuWalk();
            u->removeNode(abc[2]);
            uframe(3);
            const inspect::SceneEcho::GpuWalk g1 = ue->gpuWalk();
            std::printf("   the undrawn scene's name list: %u listed (%u owed by a consumer) before C's death, %u after; "
                        "dangling %u\n", g0.namedListed, g0.namedLive, g1.namedListed, g1.namedDangling);
            CHECK(g0.namedListed >= 3u, "the sequence holds C listed beside the live A and B (%u listed)", g0.namedListed);
            CHECK(g1.namedDangling == 0u, "a death leaves no dead record on the name list (%u dangling)",
                  g1.namedDangling);
            CHECK(g0.namedNotFlagged == 0u && g0.namedDuplicates == 0u && g1.namedNotFlagged == 0u &&
                  g1.namedDuplicates == 0u, "every listed record carries its listed flag and is listed once (%u/%u "
                  "before, %u/%u after)", g0.namedNotFlagged, g0.namedDuplicates, g1.namedNotFlagged,
                  g1.namedDuplicates);
            if (uv) e->destroyView(uv);
            e->destroyScene(u);
        }
    }
    std::printf("engine.gpu_walk_parity: %s (%d failure(s))\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
