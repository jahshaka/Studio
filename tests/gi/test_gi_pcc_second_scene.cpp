// gi.pcc_second_scene — A SECOND SCENE WHILE THE FIRST ONE HOLDS A PROBE GRID.
//
// THE DEFECT THIS EXISTS FOR (lane SKY-FALLBACK-1, second read; it was live on
// main). `HlmsPbs` is a singleton: `setParallaxCorrectedCubemap` is a
// PROCESS-WIDE binding, and while any grid is bound HlmsPbs sets
// `parallax_correct_cubemaps` for EVERY scene's pass (OgreHlmsPbs.cpp:1820-1828)
// — which makes `texEnvProbeMap` a cube ARRAY in every scene's pixel shader.
//
// A datablock that also carries its own manual cubemap then takes the
// `canUseManualProbe` branch and generates `SampleEnvProbe( texEnvProbeMap, … )`
// against that array. There is no OGRE_SampleLevelF16 overload for a
// textureCubeArray, so the shader DOES NOT COMPILE and every object using that
// material draws nothing at all. The engine used to decide "may this material
// carry a manual cubemap" from the scene's OWN `mPcc`, which is the wrong
// question: a preview, a thumbnail or the avatar module's scene has no grid of
// its own, kept its sky cube, and went black the moment the editor scene built
// one. Measured at r3 g3 b4 with two RenderingAPIException lines in the log, and
// previously misread as "the sky cubemap is unbound while a grid exists".
//
// WHAT IS ASSERTED: with scene A holding a grid, scene B's mirror is not black,
// and it shows scene B's OWN sky — which reaches it through the pass-level sky
// slot (ogre-patch 0048), because the same pass property that made the env slot
// a probe array is what arms that slot. Then scene A's grid is torn down and
// scene B's sky comes back the ordinary way, through its datablocks.
//
// The two-toned sky is the instrument, as everywhere in this family: the visible
// sky is BLUE and the reflection cubemap is GREEN, so a green mirror is "the sky
// reached this material" and a black one is "no shader".
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static void render(Engine *e, int frames = 8) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }
static void show(const char *what, const Colour &c)
{
    std::printf("   %-34s r=%.4f g=%.4f b=%.4f\n", what, c.r, c.g, c.b);
}

static NodeId addBox(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = enginetest::addTestCube(s, albedo, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

static void bindTwoTonedSky(Scene *s)
{
    SkyDesc sky;
    const unsigned char bluePx[4] = { 12, 30, 255, 255 };
    sky.mode = SkyMode::Equirectangular;
    sky.equirect = s->createTexture(1, 1, bluePx, true);
    const unsigned char greenPx[4] = { 20, 255, 40, 255 };
    sky.reflections = true;
    for (int f = 0; f < 6; ++f) sky.reflectionFaces[f] = s->createTexture(1, 1, greenPx, true);
    s->setSky(sky);
}

static NodeId addMirror(Scene *s, const Vec3 &pos, float size = 1.8f)
{
    PbrParams mirrorP; mirrorP.albedo = Colour(1, 1, 1);
    mirrorP.metalness = 1.0f; mirrorP.roughness = 0.0f;
    const NodeId mirror = s->createNode();
    s->attachMesh(mirror, s->createMesh(enginetest::unitCubeMesh()),
                  s->createPbrMaterial(mirrorP));
    s->setNodeTransform(mirror, pos, Quat(), Vec3(size, size, size));
    return mirror;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-pcc-second-scene-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *viewA = engine->createOffscreenView("a", 128, 128, Colour(0, 0, 0));
    View *viewB = engine->createOffscreenView("b", 128, 128, Colour(0, 0, 0));

    // ---- scene A: a room, which keeps a probe grid --------------------------
    Scene *a = engine->createScene("editor");
    viewA->setScene(a);
    a->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    bindTwoTonedSky(a);
    addBox(a, Colour(0.45f, 0.45f, 0.45f), Vec3(0.0f, -0.1f, 0.0f), Vec3(100.0f, 0.2f, 100.0f));
    {
        const Colour white(0.85f, 0.85f, 0.85f);
        const float span = 5.0f, height = 4.0f, thick = 0.2f;
        const float y = height * 0.5f, outer = span * 2.0f + thick * 2.0f;
        addBox(a, white, Vec3(-span, y, 0.0f), Vec3(thick, height, outer));
        addBox(a, white, Vec3( span, y, 0.0f), Vec3(thick, height, outer));
        addBox(a, white, Vec3(0.0f, y, -span), Vec3(outer, height, thick));
        addBox(a, white, Vec3(0.0f, y,  span), Vec3(outer, height, thick));
    }
    addMirror(a, Vec3(0.0f, 1.4f, 0.0f));
    enginetest::addDirectionalLight(a, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
    enginetest::testCameraLookAt(viewA, Vec3(0.0f, 2.6f, 5.0f), Vec3(0.0f, 1.4f, 0.0f));

    // ---- scene B: a sky and a mirror, and NO GI of its own -------------------
    // The shape of every second scene in the app: a preview, a thumbnail, the
    // avatar module's stage. It never arms GI (CLAUDE.md's rule), it has a sky,
    // and its materials want to reflect it.
    Scene *b = engine->createScene("preview");
    viewB->setScene(b);
    b->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.05f, 0.05f, 0.05f));
    bindTwoTonedSky(b);
    addMirror(b, Vec3(0.0f, 0.0f, 0.0f), 2.0f);
    enginetest::addDirectionalLight(b, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
    enginetest::testCameraLookAt(viewB, Vec3(0.0f, 1.6f, 4.0f), Vec3(0.0f, 0.0f, 0.0f));

    render(engine.get(), 10);

    Image img;
    viewB->readPixels(img);
    const Colour beforeGrid = img.at(64, 64);
    show("scene B's mirror, no grid yet", beforeGrid);
    CHECK(beforeGrid.g > beforeGrid.r + 0.15f && beforeGrid.g > beforeGrid.b + 0.15f,
          "B reflects its own GREEN sky cubemap before any grid exists");

    // ---- and now scene A builds its grid ------------------------------------
    {
        GiParams gi;
        gi.mode = GiMode::VctPccHybrid;
        gi.quality = GiQuality::Medium;
        gi.numBounces = 1;
        gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
        gi.updateBudget = 1;
        CHECK(a->setGlobalIllumination(gi), "scene A builds the hybrid over its room");
    }
    render(engine.get(), 20);
    const GiStatus stA = a->giStatus();
    std::printf("   scene A: probes=%d pccBound=%d\n", stA.probeCount, stA.pccBound ? 1 : 0);
    CHECK(stA.probeCount > 0 && stA.pccBound,
          "...and it is BOUND, which makes texEnvProbeMap a cube array in EVERY pass");

    viewB->readPixels(img);
    const Colour withGrid = img.at(64, 64);
    show("scene B's mirror, A holds a grid", withGrid);
    CHECK(withGrid.r + withGrid.g + withGrid.b > 0.10f,
          "B's mirror is NOT BLACK while another scene holds a probe grid — the\n"
          "          manual-cubemap branch would generate a shader that cannot compile");
    // THE MARGIN (PHOTON-M3, patch 0080): B's mirror reads 0.12/0.33/0.12 on the
    // 8-bit voxel store and 0.15/0.22/0.15 on the float one — what A's probes
    // leak into B through the PROCESS-WIDE PCC binding (the accepted v1: the
    // last scene to enable owns it) got brighter with A's un-clipped bounce.
    // The subject here is "B's own sky reaches B's mirror", i.e. green dominates,
    // not the size of the leak; 0.05 keeps the subject and admits the leak.
    CHECK(withGrid.g > withGrid.r + 0.05f && withGrid.g > withGrid.b + 0.05f,
          "...and it still shows B's OWN sky, which reaches it through the pass-level\n"
          "          sky slot (ogre-patch 0048) instead of through its datablock");

    // ---- scene A gives the grid back ----------------------------------------
    {
        GiParams off;
        off.mode = GiMode::Off;
        CHECK(a->setGlobalIllumination(off), "scene A switches GI off again");
    }
    render(engine.get(), 10);
    viewB->readPixels(img);
    const Colour afterGrid = img.at(64, 64);
    show("scene B's mirror, grid gone", afterGrid);
    CHECK(afterGrid.g > afterGrid.r + 0.15f && afterGrid.g > afterGrid.b + 0.15f,
          "B's sky comes back the ordinary way when the grid is released — the binding\n"
          "          follows the singleton for every scene, not only the one that changed");

    // ---- and the other way out: A is DESTROYED while its grid is bound -------
    // A different branch of the same fix, and it needs its own case because it
    // is the only one that cannot walk the scenes where it stands: teardownVct
    // runs inside OgreScene::destroy(), where the vector the walk iterates is
    // the one this scene is about to be erased from. So destroy() flags
    // `mReleasedPccOnDestroy` and OgreEngine::destroyScene does the walk AFTER
    // the erase. Without that hand-off B's mirror stays bound to nothing.
    {
        GiParams gi;
        gi.mode = GiMode::VctPccHybrid;
        gi.quality = GiQuality::Medium;
        gi.numBounces = 1;
        gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
        gi.updateBudget = 1;
        CHECK(a->setGlobalIllumination(gi), "scene A builds its grid one more time");
    }
    render(engine.get(), 20);
    CHECK(a->giStatus().pccBound, "...and it is bound again");
    engine->destroyScene(a);
    a = nullptr;
    render(engine.get(), 10);
    viewB->readPixels(img);
    const Colour afterDestroy = img.at(64, 64);
    show("scene B's mirror, A destroyed", afterDestroy);
    CHECK(afterDestroy.g > afterDestroy.r + 0.15f && afterDestroy.g > afterDestroy.b + 0.15f,
          "B's sky comes back when the scene holding the grid is DESTROYED — the walk\n"
          "          happens after the erase, never from inside the dying scene");

    engine->destroyScene(b);

    std::printf(failures ? "\nFAILURES: %d\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
