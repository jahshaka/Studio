// gi.material_swap — A MATERIAL SWAP RE-VOXELISES THE ITEM'S BOX, NOT THE WORLD
// (MATERIAL-SWAP-GI-1; ledger §804-§805; SPECS/briefs/MATERIAL-SWAP-GI-1.md).
//
// A material-pointer change used to reach the engine as detach + create
// (attachMesh), whose GI invalidation carries no box: under the cascade chain
// every cascade re-voxelised, one per frame — twice per object a hover preview
// crossed. Scene::setNodeMaterial swaps the datablock on the live Item and
// invalidates the item's own box, so only the cascades that box reaches owe a
// rebuild. The claims, as counters and pixels:
//   A. a swap on an item OUTSIDE the near cascades moves only the cascades that
//      contain it; the near ones do not rebuild at all;
//   B. the old path (attachMesh on the same item) rebuilds every cascade — the
//      control that proves A discriminates;
//   C. the picture takes the new material (a probe on the item changes colour);
//   D. a swap to the material already worn is a no-op (no rebuild);
//   E. a family crossing (Lit -> Unlit) and a node without a mesh are refused
//      with lastError, so the mirror falls through to the re-attach;
//   F. destroying a material no item wears rebuilds nothing (the reclaim after
//      a swap used to cost the whole chain).
// Needs a display (Vulkan). RUN_SERIAL: the chain holds four volumes.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

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

static const unsigned kSize = 128;

static void render(Engine *e, int frames = 1)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

// Renders until no cascade is pending and the rebuild counters have held still
// for eight frames (a wall clock measures nothing; the counters do).
static std::vector<unsigned long long> settle(Engine *e, Scene *s)
{
    std::vector<unsigned long long> last;
    int still = 0;
    for (int f = 0; f < 400 && still < 8; ++f) {
        render(e, 1);
        const GiStatus st = s->giStatus();
        std::vector<unsigned long long> now;
        bool pending = false;
        for (const auto &c : st.cascades) { now.push_back(c.rebuilds); pending |= c.pending != 0; }
        if (!pending && now == last) ++still; else still = 0;
        last = now;
    }
    return last;
}

static std::string counts(const std::vector<unsigned long long> &v)
{
    std::string s = "{";
    for (size_t i = 0; i < v.size(); ++i) { if (i) s += ","; s += std::to_string(v[i]); }
    return s + "}";
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-material-swap-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("mswap", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("mswap");
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.3f, 0.3f, 0.3f));

    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.55f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(200.0f, 0.1f, 200.0f));

    PbrParams red;   red.albedo = Colour(0.85f, 0.15f, 0.15f); red.metalness = 0.0f; red.roughness = 0.8f;
    PbrParams green; green.albedo = Colour(0.15f, 0.85f, 0.15f); green.metalness = 0.0f; green.roughness = 0.8f;
    const MaterialId matRed = scene->createPbrMaterial(red);
    const MaterialId matGreen = scene->createPbrMaterial(green);
    const MaterialId matUnlit = scene->createUnlitMaterial(Colour(1, 1, 0, 1), true);
    CHECK(matRed && matGreen && matUnlit, "two lit materials and an unlit one exist");

    const MeshId cube = scene->createMesh(enginetest::unitCubeMesh());
    const NodeId nearBox = scene->createNode();
    CHECK(scene->attachMesh(nearBox, cube, matRed), "the NEAR box (red) is attached at the origin");
    enginetest::setNodePosition(scene, nearBox, Vec3(0.0f, 0.5f, 0.0f));
    const NodeId farBox = scene->createNode();
    CHECK(scene->attachMesh(farBox, cube, matRed), "the FAR box (red) is attached");
    enginetest::setNodeScale(scene, farBox, Vec3(2.0f, 2.0f, 2.0f));

    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 6.0f), Vec3(0.0f, 0.5f, 0.0f));
    render(e, 2);

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 1;
    gi.cascades = true;
    CHECK(scene->setGlobalIllumination(gi), "the cascade chain built");
    render(e, 8);
    GiStatus st = scene->giStatus();
    CHECK(st.cascades.size() >= 3, ("the chain has at least three cascades (" +
                                    std::to_string(st.cascades.size()) + ")").c_str());
    if (st.cascades.size() < 3) return 1;

    // Put the far box OUTSIDE the two nearest cascades and inside the outer
    // ones: x = the second cascade's half-size plus a margin, along the axis
    // the camera looks across. Its box (a 2 m cube) must not reach the near
    // cascades' boxes either.
    const float farX = st.cascades[1].halfSize + st.cascades[1].centre.x + 6.0f;
    enginetest::setNodePosition(scene, farBox, Vec3(farX, 1.0f, 0.0f));
    std::printf("    cascades: ");
    for (const auto &c : st.cascades)
        std::printf("[half %.1f centre %.1f,%.1f,%.1f] ", c.halfSize, c.centre.x, c.centre.y, c.centre.z);
    std::printf("\n    the far box at x = %.1f\n", farX);
    std::vector<unsigned long long> base = settle(e, scene);
    st = scene->giStatus();
    std::vector<bool> reaches;
    for (const auto &c : st.cascades) {
        const float dx = std::fabs(farX - c.centre.x);
        reaches.push_back(dx - 1.5f < c.halfSize);   // the 2 m cube's own half plus slack
    }
    CHECK(!reaches[0] && !reaches[1] && reaches.back(),
          "the far box lies outside cascades 0 and 1 and inside the outermost one");
    std::printf("    baseline rebuilds %s\n", counts(base).c_str());

    // ---- A: the swap moves only the cascades that reach the box ----
    CHECK(scene->setNodeMaterial(farBox, matGreen), "setNodeMaterial(far, green) succeeds");
    std::vector<unsigned long long> after = settle(e, scene);
    std::printf("    after the swap   %s\n", counts(after).c_str());
    bool nearStill = true, outerMoved = false;
    for (size_t i = 0; i < after.size(); ++i) {
        const unsigned long long d = after[i] - base[i];
        if (!reaches[i] && d != 0) nearStill = false;
        if (reaches[i] && d >= 1) outerMoved = true;
    }
    CHECK(nearStill, "A: the cascades that do not contain the box did NOT rebuild");
    CHECK(outerMoved, "A: ...and a cascade that contains it did");

    // ---- D: the material already worn is a no-op ----
    base = after;
    CHECK(scene->setNodeMaterial(farBox, matGreen), "setNodeMaterial with the material already worn answers true");
    after = settle(e, scene);
    CHECK(after == base, "D: ...and rebuilds nothing");

    // ---- B: the old path, as the control ----
    base = after;
    CHECK(scene->attachMesh(farBox, cube, matRed), "the control: attachMesh (detach + create) on the far box");
    after = settle(e, scene);
    std::printf("    after re-attach  %s\n", counts(after).c_str());
    bool nearMovedOnReattach = false;
    for (size_t i = 0; i < after.size(); ++i)
        if (!reaches[i] && after[i] - base[i] >= 1) nearMovedOnReattach = true;
    CHECK(nearMovedOnReattach,
          "B: the re-attach rebuilds cascades that do not even contain the box — the world, "
          "which is what the swap no longer costs");

    // ---- C: the picture takes the new material ----
    Image img;
    render(e, 2);
    view->readPixels(img);
    const Colour before = img.at(kSize / 2, kSize / 2 + 8);
    CHECK(scene->setNodeMaterial(nearBox, matGreen), "setNodeMaterial(near, green) succeeds");
    settle(e, scene);
    view->readPixels(img);
    const Colour now = img.at(kSize / 2, kSize / 2 + 8);
    std::printf("    the near box's probe: %.2f %.2f %.2f -> %.2f %.2f %.2f\n",
                before.r, before.g, before.b, now.r, now.g, now.b);
    CHECK(before.r > before.g && now.g > now.r,
          "C: the probe on the near box went from red to green — the Item wears the new datablock");

    // ---- F: a material nobody wears dies for free ----
    //
    // The mirror reclaims a node's previous material the frame after a swap;
    // destroyMaterial used to invalidate the GI caches with no box for it, so
    // a hover paid the whole chain twice for two datablocks that were not in
    // the volume. A material no item wears changes no voxel.
    {
        PbrParams blue; blue.albedo = Colour(0.15f, 0.15f, 0.85f); blue.metalness = 0.0f; blue.roughness = 0.8f;
        const MaterialId matSpare = scene->createPbrMaterial(blue);
        base = settle(e, scene);
        CHECK(matSpare && scene->destroyMaterial(matSpare), "a material worn by nothing is created and destroyed");
        after = settle(e, scene);
        std::printf("    after destroying an unworn material %s\n", counts(after).c_str());
        CHECK(after == base, "F: destroying a material no item wears rebuilds nothing");
    }

    // ---- E: refusals ----
    CHECK(!scene->setNodeMaterial(nearBox, matUnlit) &&
              e->lastError().find("family") != std::string::npos,
          ("E: a Lit -> Unlit swap is refused with a reason (" + e->lastError() + ")").c_str());
    const NodeId empty = scene->createNode();
    CHECK(!scene->setNodeMaterial(empty, matGreen), "E: a node with no mesh is refused");
    CHECK(!scene->setNodeMaterial(nearBox, MaterialId(999999)), "E: an unknown material is refused");

    e->destroyView(view);
    e->destroyScene(scene);
    engine.reset();
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
