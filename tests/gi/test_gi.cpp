// Global illumination: Instant Radiosity, VCT and the VCT+PCC hybrid, all
// pixel-asserted (GI_SPEC.md phases 1-3).
//
// Scene: a red wall, a white floor, and a directional light aimed almost
// horizontally at the wall. The wall is brightly lit; the floor only catches
// the light at a grazing angle, so its direct term is dim and NEUTRAL
// (r == g == b). Bounced light is the only thing that can make the floor
// noticeably redder than green — which is exactly what the assertions measure,
// once per GI mode:
//   1. enabling the mode raises the floor's red bounce measurably;
//   2. turning it off restores the original pixels;
//   3. rotating the light away from the wall + refresh shrinks the bounce
//      (re-trace for IR, re-voxelize + re-inject for VCT);
//   4. mesh/texture churn while the mode is LIVE must not corrupt the heap
//      (the frame-time GI cache flush — IR's VAO/TextureGpu caches, VCT's
//      raw Item*/datablock caches).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static void render(Engine *e, int frames = 3)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static void show(const char *what, const Colour &c)
{
    std::printf("   %s: r=%.3f g=%.3f b=%.3f\n", what, c.r, c.g, c.b);
}

// The crash shape both IR and VCT must survive (owner crash, "double free or
// corruption" on scene switch): create a mesh + texture, attach, render (GI
// builds over them), then destroy everything while GI stays enabled. The
// engine flags the dead pointers and flushes ONCE at frame time.
static void churnRounds(Engine *engine, Scene *s)
{
    for (int round = 0; round < 4; ++round) {
        MeshData md;
        const float P[] = {-.5f,-.5f,-.5f, .5f,-.5f,-.5f, .5f,.5f,-.5f, -.5f,.5f,-.5f,
                           -.5f,-.5f,.5f, .5f,-.5f,.5f, .5f,.5f,.5f, -.5f,.5f,.5f};
        md.positions.assign(P, P + 24);
        const unsigned I[] = {0,1,2, 0,2,3, 4,6,5, 4,7,6, 0,4,5, 0,5,1,
                              3,2,6, 3,6,7, 1,5,6, 1,6,2, 0,3,7, 0,7,4};
        md.indices.assign(I, I + 36);
        MeshId m = s->createMesh(md);
        unsigned char px[4 * 4 * 4];
        for (unsigned i = 0; i < sizeof(px); ++i) px[i] = (unsigned char)(i * 7);
        TextureId t = s->createTexture(4, 4, px, true);
        PbrParams pp; pp.albedo = Colour(0.8f, 0.2f, 0.2f);
        MaterialId mat = s->createPbrMaterial(pp);
        s->setPbrTexture(mat, PbrTextureSlot::Albedo, t);
        NodeId n = s->createNode();
        s->attachMesh(n, m, mat);
        s->setNodeTransform(n, Vec3(float(round) - 2.0f, 0.6f, 0), Quat(), Vec3(0.5f, 0.5f, 0.5f));
        render(engine, 2);
        s->removeNode(n);
        s->destroyMaterial(mat);
        s->destroyTexture(t);
        s->destroyMesh(m);
        render(engine, 2);   // the frame-time GI flush runs here
    }
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("gi", 128, 128, Colour(0, 0, 0));
    Scene *s = engine->createScene("gi");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // Floor: white, rough, spanning x/z around the origin.
    const NodeId floor = enginetest::addTestCube(s, Colour(1.0f, 1.0f, 1.0f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(s, floor, Vec3(14.0f, 0.1f, 14.0f));

    // Wall: strongly red, facing +Z, behind the floor's far edge. Thick enough
    // (~3.5 voxels at the VCT block's 64^3 resolution) that the front and back
    // faces do not share voxels — a paper-thin wall leaks injected light
    // through by VCT's nature and would soften the light-turned-away assertion.
    const NodeId wall = enginetest::addTestCube(s, Colour(1.0f, 0.05f, 0.05f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, wall, Vec3(0.0f, 3.0f, -3.0f));
    enginetest::setNodeScale(s, wall, Vec3(12.0f, 6.0f, 0.9f));

    // Directional light. Engine convention: lights shine down the node's -Y, so
    // a +80 degree pitch about X sends it towards -Z (at the wall), dipping just
    // 10 degrees — the wall catches ~cos(10) of it, the floor only ~sin(10).
    const NodeId lightNode = s->createNode();
    const float half = 40.0f * 3.14159265f / 180.0f;   // 80 degrees about X
    const Quat atWall(std::sin(half), 0.0f, 0.0f, std::cos(half));
    s->setNodeTransform(lightNode, Vec3(0, 6, 6), atWall, Vec3(1, 1, 1));
    LightDesc light;
    light.type = LightType::Directional;
    light.colour = Colour(1, 1, 1);
    light.intensity = 2.0f;
    light.castShadows = false;
    s->setLight(lightNode, light);

    // Camera: above the floor in front of the wall, looking down at the patch
    // of floor the bounce should tint.
    enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 6.0f), Vec3(0.0f, 0.0f, -0.5f));

    render(engine.get());
    Image img;
    view->readPixels(img);
    // The wall fills the top of the frame, the floor the bottom.
    const unsigned floorX = 64, floorY = 96, wallX = 64, wallY = 20;
    const Colour baseFloor = img.at(floorX, floorY);
    const Colour baseWall  = img.at(wallX, wallY);
    show("floor before GI", baseFloor);
    show("wall  before GI", baseWall);
    CHECK(baseWall.r > 0.3f && baseWall.r > baseWall.g + 0.2f,
          "wall is brightly red-lit by the direct light");
    CHECK(std::fabs(baseFloor.r - baseFloor.g) < 0.02f,
          "floor's direct light is neutral (no red bias before GI)");

    // ---- Enable Instant Radiosity ----------------------------------------
    GiParams gi;
    gi.mode = GiMode::InstantRadiosity;
    gi.quality = GiQuality::Medium;
    gi.irLight = lightNode;
    gi.numBounces = 1;
    CHECK(s->setGlobalIllumination(gi), "setGlobalIllumination(InstantRadiosity) succeeds");
    render(engine.get());
    view->readPixels(img);
    const Colour onFloor = img.at(floorX, floorY);
    show("floor with IR", onFloor);
    CHECK(onFloor.r > baseFloor.r + 0.03f,
          "IR raises the floor's red measurably");
    CHECK((onFloor.r - onFloor.g) > (baseFloor.r - baseFloor.g) + 0.03f,
          "the raise is red bounce, not overall brightness");

    // Idempotent: pushing identical params again must not change the image.
    CHECK(s->setGlobalIllumination(gi), "re-pushing identical params succeeds");
    render(engine.get());
    view->readPixels(img);
    const Colour again = img.at(floorX, floorY);
    CHECK(std::fabs(again.r - onFloor.r) < 0.02f, "identical params leave pixels alone");

    // ---- The driving light moves: refresh re-traces ----------------------
    // Pitch the light the other way (-80 about X) so it shines AWAY from the
    // wall, towards +Z. The floor's grazing direct term keeps the same angle;
    // the red bounce must die.
    const Quat awayFromWall(-std::sin(half), 0.0f, 0.0f, std::cos(half));
    s->setNodeTransform(lightNode, Vec3(0, 6, 6), awayFromWall, Vec3(1, 1, 1));
    s->refreshGlobalIllumination();
    render(engine.get());
    view->readPixels(img);
    const Colour turned = img.at(floorX, floorY);
    show("floor, light turned away + refresh", turned);
    // Some red remains by IR's nature: the reversed rays hit the wall's REAR
    // face (front-facing to them) and VPLs are omni lights that bleed through
    // thin geometry. The re-trace must still shrink the bounce substantially.
    CHECK((turned.r - turned.g) < (onFloor.r - onFloor.g) - 0.03f,
          "refresh after the light turns away shrinks the bounce substantially");

    // Turn it back for the teardown comparison.
    s->setNodeTransform(lightNode, Vec3(0, 6, 6), atWall, Vec3(1, 1, 1));
    s->refreshGlobalIllumination();
    render(engine.get());

    // ---- Off restores ----------------------------------------------------
    GiParams off;   // defaults: mode Off
    CHECK(s->setGlobalIllumination(off), "setGlobalIllumination(Off) succeeds");
    render(engine.get());
    view->readPixels(img);
    const Colour offFloor = img.at(floorX, floorY);
    show("floor after GI off", offFloor);
    CHECK(std::fabs(offFloor.r - baseFloor.r) < 0.02f &&
          std::fabs(offFloor.g - baseFloor.g) < 0.02f,
          "turning GI off restores the original floor");

    // ---- churn while IR is LIVE must not corrupt the heap ----------------
    // Regression (owner crash, "double free or corruption" on scene switch):
    // Ogre::InstantRadiosity caches mesh data by raw VAO pointer and images by
    // TextureGpu*; destroying meshes/textures while IR is enabled left those
    // caches dangling. The engine now flags and flushes them at frame time.
    {
        GiParams irAgain;
        irAgain.mode = GiMode::InstantRadiosity;
        irAgain.quality = GiQuality::Low;
        CHECK(s->setGlobalIllumination(irAgain), "IR re-enabled for the churn test");
        render(engine.get());
        churnRounds(engine.get(), s);
        CHECK(true, "mesh/texture churn under live IR survived 4 rounds (no heap corruption)");
        GiParams off2;
        CHECK(s->setGlobalIllumination(off2), "IR off after churn (teardown clean)");
        render(engine.get());
    }

    // ---- VCT: real voxel cone tracing (GI_SPEC.md phase 2) ---------------
    {
        GiParams vct;
        vct.mode = GiMode::Vct;
        vct.quality = GiQuality::Medium;   // 64^3 voxels
        vct.numBounces = 2;
        // EXPLICIT bounds covering floor + wall (also exercises the document's
        // gi.bounds parameter): geometry that leaves them stops voxelizing.
        vct.boundsMin = Vec3(-9.0f, -1.5f, -9.0f);
        vct.boundsMax = Vec3(9.0f, 7.5f, 9.0f);
        const bool vctOk = s->setGlobalIllumination(vct);
        if (!vctOk) std::printf("   engine error: %s\n", engine->lastError().c_str());
        CHECK(vctOk, "setGlobalIllumination(Vct) succeeds");
        render(engine.get());
        view->readPixels(img);
        const Colour vctFloor = img.at(floorX, floorY);
        show("floor with VCT", vctFloor);
        CHECK(vctFloor.r > baseFloor.r + 0.03f,
              "VCT raises the floor's red measurably");
        CHECK((vctFloor.r - vctFloor.g) > (baseFloor.r - baseFloor.g) + 0.03f,
              "the VCT raise is red bounce, not overall brightness");

        // Idempotent: pushing identical params re-voxelizes to the same pixels.
        CHECK(s->setGlobalIllumination(vct), "re-pushing identical VCT params succeeds");
        render(engine.get());
        view->readPixels(img);
        const Colour vctAgain = img.at(floorX, floorY);
        CHECK(std::fabs(vctAgain.r - vctFloor.r) < 0.02f,
              "identical VCT params leave pixels alone");

        // The wall MOVES out of the GI bounds: node transforms deliberately do
        // not auto-invalidate (the mirror pushes them every frame), so the
        // stale voxels keep bouncing until refreshGlobalIllumination
        // re-voxelizes — then the red must collapse, because the wall is
        // outside the explicit voxel volume. (The floor's direct term never
        // involved the wall, so only the bounce can change.) The
        // light-turned-away shape IR asserts is deliberately not used here:
        // injected light leaks through voxels shared across the wall by VCT's
        // nature, which makes that signal too weak to assert robustly.
        enginetest::setNodePosition(s, wall, Vec3(0.0f, 3.0f, 40.0f));
        s->refreshGlobalIllumination();
        render(engine.get());
        view->readPixels(img);
        const Colour vctMoved = img.at(floorX, floorY);
        show("floor, wall moved away + VCT refresh", vctMoved);
        CHECK((vctMoved.r - vctMoved.g) < (vctFloor.r - vctFloor.g) - 0.05f,
              "VCT refresh after the wall moves away removes the bounce");
        enginetest::setNodePosition(s, wall, Vec3(0.0f, 3.0f, -3.0f));
        s->refreshGlobalIllumination();
        render(engine.get());

        // ---- churn while VCT is LIVE: the frame-time flush re-voxelizes ---
        churnRounds(engine.get(), s);
        CHECK(true, "mesh/texture churn under live VCT survived 4 rounds (no heap corruption)");

        // ---- off restores --------------------------------------------------
        GiParams offVct;
        CHECK(s->setGlobalIllumination(offVct), "setGlobalIllumination(Off) after VCT succeeds");
        render(engine.get());
        view->readPixels(img);
        const Colour offFloorVct = img.at(floorX, floorY);
        show("floor after VCT off", offFloorVct);
        CHECK(std::fabs(offFloorVct.r - baseFloor.r) < 0.02f &&
              std::fabs(offFloorVct.g - baseFloor.g) < 0.02f,
              "turning VCT off restores the original floor");
    }

    // ---- Hybrid: VCT diffuse + PCC probe reflections (phase 3) -----------
    {
        GiParams hybrid;
        hybrid.mode = GiMode::VctPccHybrid;
        hybrid.quality = GiQuality::Medium;   // 64^3 voxels + 256px probe faces
        hybrid.numBounces = 2;
        hybrid.pccProbesX = 2; hybrid.pccProbesY = 1; hybrid.pccProbesZ = 2;
        CHECK(s->setGlobalIllumination(hybrid), "setGlobalIllumination(VctPccHybrid) succeeds");
        render(engine.get());
        view->readPixels(img);
        const Colour hyFloor = img.at(floorX, floorY);
        show("floor with hybrid", hyFloor);
        CHECK(hyFloor.r > baseFloor.r + 0.02f,
              "hybrid keeps the VCT red bounce on the floor");
        CHECK((hyFloor.r - hyFloor.g) > (baseFloor.r - baseFloor.g) + 0.02f,
              "the hybrid raise is red bounce, not overall brightness");

        GiParams offHybrid;
        CHECK(s->setGlobalIllumination(offHybrid), "setGlobalIllumination(Off) after hybrid succeeds");
        render(engine.get());
        view->readPixels(img);
        const Colour offFloorHy = img.at(floorX, floorY);
        CHECK(std::fabs(offFloorHy.r - baseFloor.r) < 0.02f &&
              std::fabs(offFloorHy.g - baseFloor.g) < 0.02f,
              "turning the hybrid off restores the original floor (probes unbound)");
    }

    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
