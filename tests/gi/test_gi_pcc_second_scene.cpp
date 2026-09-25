// gi.pcc_second_scene — A SECOND SCENE WHILE THE FIRST ONE HOLDS A PROBE GRID.
//
// THE RULE (PHOTON-SCENE-SWITCH-1): what the shader reads is the scene being
// drawn. HlmsPbs holds ONE VctLighting / IrradianceField / PCC pointer, and
// every PBS pass binds its OWN scene's (SceneGiBinding, the engine's per-pass
// bind) — so scene A's probe grid, voxels and field reach A's passes and never
// B's, and B's datablocks never have to give up their sky cube for A's grid.
//
// THE TWO DEFECTS THIS SUITE HAS SEEN, both of the old process-wide binding:
//   * SKY-FALLBACK-1: while A's grid was bound HlmsPbs set
//     `parallax_correct_cubemaps` for EVERY scene's pass, so B's datablock with
//     a manual cubemap generated `SampleEnvProbe` against a cube ARRAY — a
//     shader that does not compile, B's mirror black (r3 g3 b4);
//   * and after that was patched over, B's mirror at the origin read A's PROBES
//     and A's cones (0.106 / 0.149 / 0.106 at the lane's base, grey 0.34 under
//     PHOTON-VOXEL-3's round 8) — the "margin" this suite used to carry.
//
// WHAT IS ASSERTED: with scene A holding a grid, scene B's mirror shows B's OWN
// sky exactly as it did before A had one; after A's grid is torn down and after
// A is destroyed, likewise.
//
// The two-toned sky is the instrument, as everywhere in this family: the visible
// sky is BLUE and the reflection cubemap is GREEN, so a green mirror is "the sky
// reached this material" and a black one is "no shader".
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
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
          "...and A's passes bind it (texEnvProbeMap is a cube array in A's passes)");

    viewB->readPixels(img);
    const Colour withGrid = img.at(64, 64);
    show("scene B's mirror, A holds a grid", withGrid);
    CHECK(withGrid.r + withGrid.g + withGrid.b > 0.10f,
          "B's mirror is NOT BLACK while another scene holds a probe grid — the\n"
          "          manual-cubemap branch would generate a shader that cannot compile");
    CHECK(withGrid.g > withGrid.r + 0.15f && withGrid.g > withGrid.b + 0.15f,
          "...and it shows B's OWN sky cubemap, not A's probes or A's cones");
    CHECK(std::fabs(withGrid.r - beforeGrid.r) < 0.01f && std::fabs(withGrid.g - beforeGrid.g) < 0.01f &&
              std::fabs(withGrid.b - beforeGrid.b) < 0.01f,
          "...exactly as it read before A had a grid: A's arms never reach B's passes");

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
          "B's sky is still its own when A's grid is released");

    // ---- and the other way out: A is DESTROYED while its grid is bound -------
    // A's arms die with A (off every PBS-family host the last pass left them on
    // — forgetGiArms), and B's passes never bound them.
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
          "B's sky is still its own when the scene holding the grid is DESTROYED");

    engine->destroyScene(b);

    std::printf(failures ? "\nFAILURES: %d\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
