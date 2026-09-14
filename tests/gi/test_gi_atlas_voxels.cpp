// gi.atlas_keeps_voxels — A SHADOW-ATLAS REBUILD IS NOT A GI REBUILD
// (SPECS/PHOTON_SPEC.md gate G2, audit C finding 2).
//
// THE DEFECT. The shadow atlas is one texture whose layout lives in a shadow
// NODE DEFINITION, and Ogre refuses to replace a definition while anything
// instantiates it. The hybrid's reflection probes instantiate it — one
// workspace each, naming the probe shadow node — and so does the raster
// irradiance field. So every atlas change had to drop them first, and the way
// the engine did that was `teardownVct()` followed by `rebuildVct()`: the whole
// voxel arm destroyed and rebuilt, every probe re-PLACED and photographed twice
// synchronously, because a shadow atlas GREW.
//
// And it grows for reasons that say nothing whatever about the scene's
// geometry: the derived focused-map count steps at the 3rd, 5th and 9th casting
// lamp, the one-way per-map-clear flip fires once per session, and a Shadow
// Quality change fires it by hand. Under Photon's cascade chain that was N
// voxelisers from scratch in one frame — the very hitch the cascade scheduler
// exists to prevent, fired by shadow bookkeeping.
//
// THE RULE. Only the WORKSPACES hold the definition, so only the workspaces are
// dropped and re-created. The voxels, the cascade chain, the probes' own shapes
// and the placement's depth fit all survive; the probes' CONTENTS are staled
// instead, so the ordinary per-frame budget re-photographs them a few at a time
// rather than the placement capturing the whole grid inline.
//
// Section A pins it for the SINGLE-VOLUME arm (the behaviour every shipped
// scene has today), section B for the cascade chain.
//
// Its own binary like every GI suite: the voxel lighting and the probe grid bind
// process-wide to HlmsPbs.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
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

/// Renders `frames` one at a time and returns how many probe FACE SETS the
/// budget photographed across them. `probeCapturesLastFrame` is a per-frame
/// figure, so a single reading after a burst of frames reports whatever the LAST
/// of them happened to do — usually nothing, because the budget has already
/// caught up. Accumulated, it is the number this gate is about.
static int renderCounting(Engine *e, Scene *s, int frames)
{
    int captured = 0;
    for (int i = 0; i < frames; ++i) {
        e->renderOneFrame();
        captured += s->giStatus().probeCapturesLastFrame;
    }
    return captured;
}

static float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

static float meanLum(const Image &img)
{
    float sum = 0.0f; unsigned n = 0;
    for (unsigned y = kSize / 4u; y < kSize * 3u / 4u; ++y)
        for (unsigned x = kSize / 4u; x < kSize * 3u / 4u; ++x) { sum += lum(img.at(x, y)); ++n; }
    return n ? sum / float(n) : 0.0f;
}

/// A SEALED BOX. The hybrid measures enclosure before it will place probes, and
/// this suite is about what an atlas rebuild does to a grid, not about
/// placement — so the walls are real AND the bounds are pinned, the same
/// belt-and-braces tests/shadow's r3 case uses.
struct Room {
    NodeId floor = 0, ceiling = 0, wallN = 0, wallS = 0, wallE = 0, wallW = 0;
};

static Room buildRoom(Scene *s)
{
    Room r;
    const auto slab = [&](const Vec3 &pos, const Vec3 &scale, const Colour &c) {
        const NodeId n = enginetest::addTestCube(s, c, 0.0f, 0.85f);
        enginetest::setNodePosition(s, n, pos);
        enginetest::setNodeScale(s, n, scale);
        return n;
    };
    r.floor   = slab(Vec3(0.0f, -0.1f, 0.0f), Vec3(10.0f, 0.2f, 10.0f), Colour(0.8f, 0.8f, 0.8f));
    r.ceiling = slab(Vec3(0.0f,  5.1f, 0.0f), Vec3(10.0f, 0.2f, 10.0f), Colour(0.8f, 0.8f, 0.8f));
    r.wallN   = slab(Vec3(0.0f, 2.5f, -5.0f), Vec3(10.0f, 5.0f, 0.2f), Colour(0.9f, 0.2f, 0.2f));
    r.wallS   = slab(Vec3(0.0f, 2.5f,  5.0f), Vec3(10.0f, 5.0f, 0.2f), Colour(0.8f, 0.8f, 0.8f));
    r.wallE   = slab(Vec3( 5.0f, 2.5f, 0.0f), Vec3(0.2f, 5.0f, 10.0f), Colour(0.2f, 0.9f, 0.2f));
    r.wallW   = slab(Vec3(-5.0f, 2.5f, 0.0f), Vec3(0.2f, 5.0f, 10.0f), Colour(0.2f, 0.2f, 0.9f));
    return r;
}

/// One shadow-casting point lamp — the thing that makes the atlas grow.
static NodeId addCastingLamp(Scene *s, const Vec3 &pos)
{
    const NodeId n = s->createNode();
    LightDesc d;
    d.type = LightType::Point;
    d.colour = Colour(1.0f, 1.0f, 1.0f);
    d.intensity = 3.0f;
    d.range = 20.0f;
    d.castShadows = true;
    if (!s->setLight(n, d)) return 0;
    enginetest::setNodePosition(s, n, pos);
    return n;
}

static GiParams hybridGi(bool cascades)
{
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::High;       // probeShadows resolves true at High
    gi.probeShadows = GiToggle::On;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 2;                // a live budget: the stale set is spendable
    gi.boundsMin = Vec3(-5.5f, -0.5f, -5.5f);
    gi.boundsMax = Vec3( 5.5f,  5.5f,  5.5f);
    gi.cascades = cascades;
    return gi;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-atlas-voxels-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("atlas", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("atlas");
    view->setScene(scene);
    scene->setAmbient(Colour(0.15f, 0.15f, 0.18f), Colour(0.08f, 0.08f, 0.10f));
    buildRoom(scene);
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 4.0f), Vec3(0.0f, 1.5f, 0.0f)));
    addCastingLamp(scene, Vec3(0.0f, 4.0f, 0.0f));
    addCastingLamp(scene, Vec3(2.0f, 4.0f, 2.0f));
    render(e, 4);

    // =====================================================================
    // SECTION A — the SINGLE-VOLUME arm (what ships today)
    // =====================================================================
    std::printf("\n== A: a shadow-atlas rebuild under the single volume ==\n");
    CHECK(scene->setGlobalIllumination(hybridGi(false)), "the hybrid arm accepts");
    render(e, 12);
    {
        const GiStatus before = scene->giStatus();
        std::printf("   probes %d, probeShadows %d, vctBound %d, arm rebuilds %llu\n",
                    before.probeCount, int(before.probeShadows), int(before.vctBound),
                    before.rebuilds);
        CHECK(before.probeShadows && before.probeCount > 0,
              "the probe captures really are shadowed (the precondition for this gate)");
        const unsigned atlas0 = e->shadowStatus().atlasRebuilds;
        // A SHADOW QUALITY CHANGE: the explicit, deterministic trigger.
        e->setShadowResolution(e->shadowStatus().resolution == 1024u ? 2048u : 1024u);
        // THE WORKSPACES MUST STILL CAPTURE (round-2 F8). Keeping the grid is
        // only half the claim: every probe's cubemap went back to the pool with
        // its workspace, so a probe whose workspace came back wrong would be a
        // grid of BLACK probes — which reads as "the probes are fine" in every
        // other counter this suite has.
        const int capturedA = renderCounting(e, scene, 40);
        const GiStatus after = scene->giStatus();
        const unsigned atlas1 = e->shadowStatus().atlasRebuilds;
        std::printf("   after the rebuild: atlasRebuilds %u -> %u, arm rebuilds %llu -> %llu, "
                    "probes %d -> %d\n",
                    atlas0, atlas1, before.rebuilds, after.rebuilds, before.probeCount,
                    after.probeCount);
        CHECK(atlas1 == atlas0 + 1u, "the atlas really was rebuilt");
        CHECK(after.rebuilds == before.rebuilds,
              "THE VOXELS SURVIVED IT: not one from-scratch GI rebuild");
        CHECK(after.probeCount == before.probeCount && after.pccBound && after.vctBound,
              "...and the probe grid is still there, still bound, with the same probes");
        std::printf("   the budget photographed %d probe(s) over the 40 frames after it, "
                    "%d still stale\n", capturedA, scene->giStatus().staleProbes);
        CHECK(capturedA > 0,
              "THE RE-CREATED WORKSPACES CAPTURE: the budget photographs the probes again");
        Image img;
        CHECK(view->readPixels(img) && meanLum(img) > 0.01f,
              "...and the room still renders, lit");
    }

    // =====================================================================
    // SECTION B — the cascade chain, and a lamp count that GROWS the atlas
    // =====================================================================
    // The real-world trigger, and the one audit C names: the derived focused-map
    // count steps {2,4,8,16}, so the 3rd casting lamp in a scene rebuilds the
    // atlas. Under the chain that used to be N voxelisers from scratch in one
    // frame.
    std::printf("\n== B: a lamp added under the cascade chain ==\n");
    CHECK(scene->setGlobalIllumination(hybridGi(true)), "the cascade arm accepts");
    render(e, 16);
    {
        const GiStatus before = scene->giStatus();
        unsigned long long cascade0 = 0;
        for (const auto &c : before.cascades) cascade0 += c.rebuilds;
        std::printf("   %zu cascades, probes %d, arm rebuilds %llu, cascade rebuilds %llu\n",
                    before.cascades.size(), before.probeCount, before.rebuilds, cascade0);
        CHECK(!before.cascades.empty(), "the chain is up");
        CHECK(before.probeShadows, "...with shadowed probe captures beside it");
        const unsigned atlas0 = e->shadowStatus().atlasRebuilds;
        // Lamps 3, 4 and 5: the count steps at 3 and again at 5.
        int capturedB = 0;
        addCastingLamp(scene, Vec3(-2.0f, 4.0f, -2.0f));
        capturedB += renderCounting(e, scene, 16);
        addCastingLamp(scene, Vec3( 2.0f, 4.0f, -2.0f));
        capturedB += renderCounting(e, scene, 16);
        addCastingLamp(scene, Vec3(-2.0f, 4.0f,  2.0f));
        capturedB += renderCounting(e, scene, 32);
        const GiStatus after = scene->giStatus();
        unsigned long long cascade1 = 0;
        for (const auto &c : after.cascades) cascade1 += c.rebuilds;
        const unsigned atlas1 = e->shadowStatus().atlasRebuilds;
        const ShadowStatus sh = e->shadowStatus();
        std::printf("   after three lamps: atlasRebuilds %u -> %u (focused maps %u), "
                    "arm rebuilds %llu -> %llu, cascade rebuilds %llu -> %llu\n",
                    atlas0, atlas1, sh.focusedMaps, before.rebuilds, after.rebuilds,
                    cascade0, cascade1);
        CHECK(atlas1 > atlas0, "the atlas grew for the new lamps");
        CHECK(after.rebuilds == before.rebuilds,
              "ZERO whole-chain builds: an atlas growth does not re-voxelise the chain");
        CHECK(after.cascades.size() == before.cascades.size() && after.vctBound,
              "...the chain is the same chain, still bound");
        CHECK(after.probeCount == before.probeCount && after.pccBound,
              "...and the probe grid kept its probes and its binding");
        std::printf("   under the chain the budget photographed %d probe(s) across the "
                    "lamp additions, %d still stale\n", capturedB,
                    scene->giStatus().staleProbes);
        CHECK(capturedB > 0, "THE RE-CREATED WORKSPACES CAPTURE under the chain too");
        Image img;
        CHECK(view->readPixels(img) && meanLum(img) > 0.01f,
              "...and the room still renders, lit");
    }

    GiParams off; off.mode = GiMode::Off;
    CHECK(scene->setGlobalIllumination(off), "the arm comes down");
    render(e, 2);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
