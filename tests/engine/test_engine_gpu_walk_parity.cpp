// engine.gpu_walk_parity — THE NAMED WALK'S GUARD (ENGINE-UPGRADE-2B stage 2).
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
// its node (its flags word or its transform), or the step tested nothing.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/GpuSubstrate.h"
#include "../support/enginetesthelpers.h"
#include "../support/gesture.h"

#include <cstdio>
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

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-engine-gpu-walk-parity-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("parity", 96, 96, Colour(0.05f, 0.06f, 0.08f, 1.0f));
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
    gi.numBounces = 1;
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
    };
    for (const Step &st : steps) {
        const GpuSubstrateStatus before = gpuSubstrateStatus(*scene);
        const GpuSceneStatus gb = scene->gpuSceneStatus();
        st.run();
        enginetest::frames(e, 4);
        const GpuSubstrateStatus after = gpuSubstrateStatus(*scene);
        const GpuSceneStatus ga = scene->gpuSceneStatus();
        CHECK(after.walkMissed == before.walkMissed,
              "%s: the table holds every slot (%llu misses over %llu checks; last miss slot %u)", st.name,
              after.walkMissed - before.walkMissed, after.walkChecked - before.walkChecked,
              after.walkLastMissed);
        CHECK(ga.updates > gb.updates, "%s: the table took the writer's change (%llu commits)", st.name,
              (unsigned long long)(ga.updates - gb.updates));
    }
    e->setArm("gpuscene.walkParity", 0.0);
    std::printf("engine.gpu_walk_parity: %s (%d failure(s))\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
