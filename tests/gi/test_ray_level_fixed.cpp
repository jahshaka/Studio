// gi.ray_level_fixed — ONE RAY LEVEL PER MESH (PHOTON-7 stage 6, law L1: light is a world function).
//
// THE LAW: every ray (the gather's, the sun contact's, the cards' sun, the reflections') traces each mesh's NEAR copy
// over LEVEL 0 - the authored triangles - wherever the camera stands; the far copy over the coarsest. The traced
// geometry is then a function of the world alone, so (a) a camera WALK builds no bottom-level structure and re-points
// no near copy (0 builds, every near level unchanged), and (b) the near copies after the walk are the near copies a
// camera that TELEPORTED to the walk's end finds (the same level per slot) - and both are level 0.
// WHAT IT GUARDS AGAINST (the base had no door to read a near copy's level; this row's door is new with the change): the
// 2x distance band the ray rule re-fitted per view - chained meshes cross it as the eye walks toward them (Bistro
// Exterior at the owner's pose: 1,384 re-fits at boot, +2 over a 4 m walk, spikes/photon-7/stage6/c3/BISTRO_BLAS.txt),
// and a walked level could differ from a teleport's where the band's hysteresis kept a level a fresh evaluation would
// not.
// THE FIXTURE: an offscreen view with the ray tier live (the viewport's post chain), twelve two-level cubes on a line
// at 2 .. 200 m (level 1's measured bound 2 cm, so any rule over a footprint picks it somewhere on the line) and a
// floor; the eye walks 4 m in 48 frames toward them. Frames: 1 + 24 settle + 48 walk + 4 + the teleport's 24 + 4 = 105.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/EngineInspect.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
    } while (0)

static const int kCubes = 12;

struct Arm {
    Scene *scene = nullptr;
    View *view = nullptr;
    std::vector<int> levels;
    unsigned long long builds = 0;
};

static bool makeArm(Engine *e, const char *name, Arm &a)
{
    a.view = e->createOffscreenView(name, 160, 90, Colour(0.05f, 0.06f, 0.08f, 1.0f));
    a.scene = e->createScene(name, 0u);
    if (!a.view || !a.scene) return false;
    a.view->setScene(a.scene);
    a.scene->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.3f, 0.3f, 0.3f));
    enginetest::addDirectionalLight(a.scene, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;   // the ray tier live on the view (the editor screenshot's door)
    a.view->setPostFx(fx);
    MeshData chained = enginetest::unitCubeMesh();
    chained.lodIndices.push_back(chained.indices);
    chained.lodBounds.push_back(0.02f);
    const MeshId mesh = a.scene->createMesh(chained);
    PbrParams p;
    p.albedo = Colour(0.6f, 0.6f, 0.6f);
    p.roughness = 0.5f;
    const MaterialId mat = a.scene->createPbrMaterial(p);
    for (int i = 0; i < kCubes; ++i) {
        const NodeId n = a.scene->createDocumentNode(reserveNodeId());
        a.scene->attachMesh(n, mesh, mat);
        // 2 .. 200 m from the walk's start, geometric: every distance band of the old rule holds one
        const float z = -2.0f * std::pow(100.0f, float(i) / float(kCubes - 1));
        enginetest::setNodePosition(a.scene, n, Vec3(0.6f * float(i % 3) - 0.6f, 0.5f, z));
    }
    const NodeId floor = enginetest::addTestCube(a.scene, Colour(0.4f, 0.4f, 0.4f), 0.0f, 0.9f);
    enginetest::setNodePosition(a.scene, floor, Vec3(0.0f, -0.05f, -100.0f));
    enginetest::setNodeScale(a.scene, floor, Vec3(20.0f, 0.1f, 220.0f));
    return true;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-ray-level-fixed-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const Vec3 start(0.0f, 1.6f, 4.0f), end(0.0f, 1.6f, 0.0f), target(0.0f, 0.5f, -200.0f);

    // (a) THE WALK
    Arm walk;
    if (!makeArm(e, "walk", walk)) { std::printf("FAIL: the walk's view/scene: %s\n", e->lastError().c_str()); return 1; }
    // (the device exists once a view does)
    if (!(e->rayQueryAvailable() && e->rayTracing())) {
        std::printf("SKIP: no ray-query device here: nothing traces (the law is the ray tier's)\n");
        return 77;
    }
    enginetest::testCameraLookAt(walk.view, start, target);
    enginetest::frames(e, 24);
    std::vector<int> before;
    CHECK(inspect::scene(walk.scene)->rayNearLevels(before), "the near copies read back before the walk (%zu slots)",
          before.size());
    const unsigned long long builds0 = walk.scene->published().rayQuery.blasBuilds;
    for (int f = 1; f <= 48; ++f) {
        const float t = float(f) / 48.0f;
        enginetest::testCameraLookAt(walk.view, Vec3(start.x, start.y, start.z + (end.z - start.z) * t), target);
        enginetest::frames(e, 1);
    }
    enginetest::frames(e, 4);
    walk.builds = walk.scene->published().rayQuery.blasBuilds - builds0;
    CHECK(inspect::scene(walk.scene)->rayNearLevels(walk.levels), "the near copies read back after the walk");
    std::printf("   the walk (4 m in 48 frames): %llu bottom-level structures built, %llu ever\n", walk.builds,
                walk.scene->published().rayQuery.blasBuilds);
    CHECK(walk.builds == 0u, "a 4 m walk builds no bottom-level structure (%llu)", walk.builds);
    int traced = 0, level0 = 0, changed = 0;
    for (size_t s = 0; s < walk.levels.size(); ++s) {
        if (walk.levels[s] < 0) continue;
        ++traced;
        if (walk.levels[s] == 0) ++level0;
        if (s < before.size() && before[s] != walk.levels[s]) ++changed;
    }
    std::printf("   after the walk: %d traced slots, %d near copies at level 0, %d re-pointed by the walk\n", traced,
                level0, changed);
    CHECK(traced >= kCubes + 1, "every cube and the floor is traced (%d)", traced);
    CHECK(level0 == traced, "every near copy is level 0 (%d of %d)", level0, traced);
    CHECK(changed == 0, "the walk re-pointed no near copy (%d)", changed);

    // (b) THE TELEPORT: a fresh scene of the same content, the camera at the walk's end from its first frame
    Arm tele;
    walk.view->setScene(nullptr);
    if (!makeArm(e, "teleport", tele)) { std::printf("FAIL: the teleport's view/scene: %s\n", e->lastError().c_str()); return 1; }
    enginetest::testCameraLookAt(tele.view, end, target);
    enginetest::frames(e, 28);
    CHECK(inspect::scene(tele.scene)->rayNearLevels(tele.levels), "the teleport's near copies read back");
    int differ = 0, compared = 0;
    for (size_t s = 0; s < tele.levels.size() && s < walk.levels.size(); ++s) {
        if (tele.levels[s] < 0 && walk.levels[s] < 0) continue;
        ++compared;
        if (tele.levels[s] != walk.levels[s]) ++differ;
    }
    CHECK(tele.levels.size() == walk.levels.size() && compared == traced,
          "the teleport traces the walk's slots (%zu / %zu slots, %d compared)", tele.levels.size(),
          walk.levels.size(), compared);
    CHECK(differ == 0, "walk vs teleport: every slot's near copy is the same level (%d differ)", differ);

    std::printf("%s\n", failures ? "gi.ray_level_fixed: FAIL" : "gi.ray_level_fixed: PASS");
    return failures ? 1 : 0;
}
