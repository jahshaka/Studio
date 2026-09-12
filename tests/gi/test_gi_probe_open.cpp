// gi.probe_open — AN OPEN SCENE BUILDS NO PROBE GRID, AND THE SKY IS ITS
// REFLECTION (owner decision 2026-09-13 Q3, SPECS/REFLECTION_PROBE_AUDIT.md).
//
// Owner: "a user starts in the editor in a new project with an open scene and
// then builds by adding assets and objects... I would think the sky is your
// first reflection asset." Exactly so, and the engine can MEASURE it rather
// than assume: `computeProbeRegion` already pulls the probe region in to the
// nearest enclosing slab on each of the six faces, so "is this space enclosed"
// is a reading it already takes. Below two axes enclosed on BOTH faces there is
// nothing to photograph but the sky, and 18-32 cube captures of the sky cost
// 128-512 MB of probe array, a probe shadow atlas per probe and the per-pixel
// probe loop, to reproduce — with a visible grid seam — what the sky cubemap
// already holds perfectly (the 2026-09-11 lighting audit's finding #4).
//
// ONE scene, two states, in this order (the process-wide HlmsPbs binding rule:
// a second scene in one process would fight for it):
//   1. OPEN: a ground plane and three objects, no walls. The hybrid must
//      refuse the grid, say so, and leave the sky cubemap bound — a mirror
//      sphere must reflect the SKY, not a probe's photograph of it.
//   2. ENCLOSED: four walls and a ceiling are added and GI is re-solved. The
//      grid must appear.
//
// FAIL-BEFORE (measured in-lane on the base binary): state 1 built 2 probes,
// reported probeGridRefused false, and unbound the sky cubemap from every
// datablock (`reflectionTexForDatablocks` returns null while a grid exists), so
// the mirror showed a probe capture instead of the sky.
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

static void render(Engine *e, int frames = 8) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }
static void show(const char *what, const Colour &c)
{
    std::printf("   %-34s r=%.4f g=%.4f b=%.4f\n", what, c.r, c.g, c.b);
}

static NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = enginetest::addTestCube(s, albedo, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-probe-open-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("open", 128, 128, Colour(0, 0, 0));
    Scene *s = engine->createScene("open");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // THE SKY, IN TWO SEPARATELY COLOURED HALVES so "the sky IBL is bound" is a
    // HUE assertion and not a brightness one:
    //   * the visible sky is a BLUE equirect image — what a probe capture would
    //     photograph;
    //   * the IBL reflection cubemap is GREEN — what a datablock samples when
    //     the engine binds the sky cubemap to it.
    // They are the same sky to an author and two different textures to the
    // renderer, which is exactly the discriminator this suite needs: while a
    // probe grid exists the engine UNBINDS the reflection cubemap from every
    // datablock (`reflectionTexForDatablocks`, OgreSky.cpp — the env-probe slot
    // has one occupant), so a mirror shows BLUE through a probe and GREEN
    // through the sky IBL.
    SkyDesc sky;
    {
        const unsigned char bluePx[4] = { 12, 30, 255, 255 };
        const TextureId skyTex = s->createTexture(1, 1, bluePx, true);
        CHECK(skyTex != 0, "a blue equirect sky texture");
        sky.mode = SkyMode::Equirectangular;
        sky.equirect = skyTex;
        const unsigned char greenPx[4] = { 20, 255, 40, 255 };
        sky.reflections = true;
        for (int f = 0; f < 6; ++f) sky.reflectionFaces[f] = s->createTexture(1, 1, greenPx, true);
        CHECK(sky.reflectionFaces[0] != 0, "six green IBL reflection faces");
        CHECK(s->setSky(sky), "the sky binds");
    }

    // THE OPEN SCENE: a ground plane and three objects. No walls, no ceiling.
    // 16 m square, so the walls added in phase 2 can stand OUTSIDE it: the
    // enclosure test asks for a slab whose OUTER face is the content hull's own
    // face (computeProbeRegion's condition 3, the Mirror Room fix), and a
    // ground plane wider than its own walls would disqualify them.
    addSlab(s, Colour(0.5f, 0.5f, 0.5f), Vec3(0.0f, -0.25f, 0.0f), Vec3(16.0f, 0.5f, 16.0f));
    addSlab(s, Colour(0.8f, 0.3f, 0.2f), Vec3(-3.0f, 0.8f, -2.0f), Vec3(1.6f, 1.6f, 1.6f));
    addSlab(s, Colour(0.2f, 0.8f, 0.3f), Vec3( 3.2f, 1.2f,  1.5f), Vec3(1.2f, 2.4f, 1.2f));

    PbrParams mirrorP; mirrorP.albedo = Colour(1, 1, 1); mirrorP.metalness = 1.0f; mirrorP.roughness = 0.0f;
    const NodeId mirror = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId mat = s->createPbrMaterial(mirrorP);
    CHECK(mirror && mesh && mat && s->attachMesh(mirror, mesh, mat), "the mirror box attaches");
    s->setNodeTransform(mirror, Vec3(0.0f, 1.4f, 0.0f), Quat(), Vec3(1.8f, 1.8f, 1.8f));

    CHECK(enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f) != 0,
          "directional light created");
    // Looking slightly DOWN at the box's +Z face, so its reflection vector
    // points up past the camera into the sky.
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.6f, 5.0f), Vec3(0.0f, 1.4f, 0.0f));
    const unsigned mx = 64, my = 70;

    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
    CHECK(s->setGlobalIllumination(gi), "setGlobalIllumination(VctPccHybrid) succeeds on the open scene");
    render(engine.get(), 10);

    {
        const GiStatus st = s->giStatus();
        std::printf("-- open scene --\n   probes=%d pccBound=%s refused=%s enclosedAxes=%d vctBound=%s\n",
                    st.probeCount, st.pccBound ? "true" : "false",
                    st.probeGridRefused ? "true" : "false", st.probeEnclosedAxes,
                    st.vctBound ? "true" : "false");
        CHECK(st.mode == GiMode::VctPccHybrid, "the mode is still the hybrid");
        CHECK(st.probeEnclosedAxes < 2,
              "the renderer MEASURES the scene as open (fewer than two enclosed axes)");
        CHECK(st.probeGridRefused, "so it declines the probe grid, and says so");
        CHECK(st.probeCount == 0 && !st.pccBound, "no probes exist and none are bound");
        CHECK(st.probeCaptureSize == 0, "and there is no capture size to report");
        CHECK(st.vctBound, "the voxel half is untouched — cone tracing still carries the bounce");
    }
    Image img;
    view->readPixels(img);
    const Colour openMirror = img.at(mx, my);
    show("mirror box, open scene", openMirror);
    CHECK(openMirror.g > 0.25f && openMirror.g > openMirror.b + 0.15f,
          "the mirror reflects the GREEN IBL cubemap — the sky is the open scene's reflection "
          "source, and it is BOUND (a probe capture would have shown the blue sky instead)");

    // ---- 2. ENCLOSE IT ----------------------------------------------------
    // Four walls and a ceiling around the same content. `computeProbeRegion`
    // now finds a slab on both faces of all three axes, so the grid is built.
    const Colour white(0.85f, 0.85f, 0.85f);
    addSlab(s, white, Vec3(0.0f,  6.2f,  0.0f), Vec3(16.4f, 0.4f, 16.4f));  // ceiling
    addSlab(s, white, Vec3(0.0f,  3.0f, -8.2f), Vec3(16.4f, 6.0f, 0.4f));
    addSlab(s, white, Vec3(0.0f,  3.0f,  8.2f), Vec3(16.4f, 6.0f, 0.4f));
    addSlab(s, white, Vec3(-8.2f, 3.0f,  0.0f), Vec3(0.4f, 6.0f, 16.4f));
    addSlab(s, white, Vec3( 8.2f, 3.0f,  0.0f), Vec3(0.4f, 6.0f, 16.4f));
    s->refreshGlobalIllumination();
    render(engine.get(), 14);
    {
        const GiStatus st = s->giStatus();
        std::printf("-- enclosed --\n   probes=%d pccBound=%s refused=%s enclosedAxes=%d captureSize=%d\n",
                    st.probeCount, st.pccBound ? "true" : "false",
                    st.probeGridRefused ? "true" : "false", st.probeEnclosedAxes,
                    st.probeCaptureSize);
        CHECK(st.probeEnclosedAxes >= 2, "walls and a ceiling measure as enclosure");
        CHECK(!st.probeGridRefused, "so the grid is no longer refused");
        CHECK(st.probeCount == 4 && st.pccBound, "and four probes are built and bound");
        // ITEM 4's default: Medium and High both capture at 256 px now.
        CHECK(st.probeCaptureSize == 256, "the resolved capture size is 256 px");
    }

    engine->destroyScene(s);
    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
