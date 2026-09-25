// gi.reenable — GI TURNED OFF AND BACK ON AT RUNTIME, in every arm.
//
// The document can push any GiParams at any time (`world.gi`, the World
// panel's dropdowns, a script, a scene open), and the engine answers every push
// by tearing the arm down and building the one that was asked for. Nothing in
// the tree used to drive that transition: every GI suite pushes its mode once.
// The two defects this suite exists for were both found by driving it (lane
// PATCHES-1 / GI-REENABLE-1, ledger §401's side observation):
//
//   1. THE CASCADE CHAIN BUILT WITH MULTIPLE BOUNCES FAILED TO COMPILE ITS
//      BOUNCE SHADER, every time, on a fresh enable and on a re-enable:
//      "'ogre_t6' : unrecognized layout identifier" from
//      NNLightVctBounceInject_cs, which left the whole VCT arm unbound — no GI
//      in the picture, and one line in the engine log to say so. The cause is
//      in the pin (ogre-patch 0057): `runBounce` declares one light-voxel
//      texture per cascade from `hlms_num_vct_cascades`, while the job's
//      texture UNIT COUNT is only ever derived inside
//      `setAllowMultipleBounces`/`resetTexturesFromBuildRelative` — so chaining
//      the cascades AFTER enabling bounces (an order the pin's header does not
//      forbid, and the order this engine uses) left the two disagreeing.
//
//   2. A re-enable must leave the picture it had before. Asserted as pixels,
//      not as a flag: the mode round trip off -> on has to restore the bounce.
//
// Every case ends with `takeLastError()` empty, because that sink is where the
// backend's swallowed exceptions land: a GI arm that throws its way out of a
// build reports a wrong picture and nothing else without it.
//
// Its own binary like every GI suite.
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
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        if (cond) std::printf("ok: " fmt "\n", __VA_ARGS__);                    \
        else { std::printf("FAIL: " fmt "\n", __VA_ARGS__); ++failures; }       \
    } while (0)

static const unsigned kSize = 128;

static void render(Engine *e, int frames = 1)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

/// The shadowed side of the floor: the band the bounce lights and the direct
/// light only grazes, which is what "the GI is back" has to mean.
static float bounceLum(const Image &img)
{
    float sum = 0.0f; unsigned n = 0;
    for (unsigned y = kSize * 5u / 8u; y < kSize * 7u / 8u; ++y)
        for (unsigned x = kSize / 4u; x < kSize * 3u / 4u; ++x) { sum += lum(img.at(x, y)); ++n; }
    return n ? sum / float(n) : 0.0f;
}

/// Drains the error sink and reports what it held. Empty is the assertion.
static void checkNoEngineError(Engine *e, const char *tag)
{
    const std::string err = e->takeLastError();
    CHECK_MSG(err.empty(), "%s: the engine error sink must be empty (got '%s')", tag,
              err.empty() ? "" : err.c_str());
}

static GiParams offGi()
{
    GiParams gi;
    gi.mode = GiMode::Off;
    return gi;
}

/// The hybrid as the shipped default project runs it, with MULTIPLE BOUNCES —
/// the setting the bounce-inject job exists for, and the one no other GI suite
/// turns on (they all run numBounces = 1, i.e. no bounce job at all, which is
/// why defect 1 above lived through every gate).
static GiParams hybridGi(bool cascades)
{
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 2;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;          // no probe sweeps; this suite is about the arms
    gi.cascades = cascades;
    return gi;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-reenable-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("reenable", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("reenable");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.02f, 0.02f, 0.02f), Colour(0.02f, 0.02f, 0.02f));

    // A floor, a bright red wall to bounce off, and a light that grazes the
    // floor: the classic GI fixture (gi.modes' shape, smaller).
    const NodeId floorN = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, floorN, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, floorN, Vec3(40.0f, 0.1f, 40.0f));
    const NodeId wall = enginetest::addTestCube(scene, Colour(0.9f, 0.05f, 0.05f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, wall, Vec3(0.0f, 2.0f, -4.0f));
    enginetest::setNodeScale(scene, wall, Vec3(8.0f, 4.0f, 0.2f));
    enginetest::addDirectionalLight(scene, Vec3(0.15f, -0.12f, -0.98f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.2f, 6.0f), Vec3(0.0f, 0.6f, -1.0f));

    render(e, 2);
    e->takeLastError();                       // the fixture's own build, whatever it said

    // =====================================================================
    // CASE 1 — the single-volume hybrid: on, off, ON AGAIN
    // =====================================================================
    CHECK(scene->setGlobalIllumination(hybridGi(false)), "case 1: the hybrid is accepted");
    render(e, 4);
    checkNoEngineError(e, "case 1 build");
    GiStatus st = scene->giStatus();
    CHECK(st.vctBound, "case 1: the voxel arm is bound");
    Image lit;
    if (!view->readPixels(lit)) { std::printf("FAIL: readPixels (lit)\n"); return 1; }
    const float litLum = bounceLum(lit);

    CHECK(scene->setGlobalIllumination(offGi()), "case 1: GI off is accepted");
    render(e, 4);
    checkNoEngineError(e, "case 1 off");
    st = scene->giStatus();
    CHECK(!st.vctBound, "case 1: nothing is bound with GI off");
    Image dark;
    if (!view->readPixels(dark)) { std::printf("FAIL: readPixels (off)\n"); return 1; }
    const float offLum = bounceLum(dark);
    CHECK_MSG(litLum > offLum + 0.002f,
              "case 1: the bounce is visible in the first place (%.4f lit vs %.4f off)",
              litLum, offLum);

    CHECK(scene->setGlobalIllumination(hybridGi(false)), "case 1: the hybrid is accepted again");
    render(e, 4);
    checkNoEngineError(e, "case 1 re-enable");
    st = scene->giStatus();
    CHECK(st.vctBound, "case 1: the voxel arm is bound again");
    Image relit;
    if (!view->readPixels(relit)) { std::printf("FAIL: readPixels (relit)\n"); return 1; }
    const float relitLum = bounceLum(relit);
    // THE PICTURE COMES BACK, and to the same place: a re-enable rebuilds the
    // same volume from the same content, so the band's mean must land within a
    // byte of where it was (the voxelisation is deterministic for a still
    // scene; this tolerance is a quantisation allowance, not a fudge).
    CHECK_MSG(std::fabs(relitLum - litLum) < 0.004f,
              "case 1: the re-enabled bounce is the same picture (%.4f vs %.4f)",
              relitLum, litLum);

    // =====================================================================
    // CASE 2 — THE CASCADE CHAIN WITH BOUNCES (ogre-patch 0057)
    // =====================================================================
    // A fresh enable first: before the patch this raised
    // "'ogre_t6' : unrecognized layout identifier" and left vctBound false,
    // because the chain was grown after bounces were switched on.
    CHECK(scene->setGlobalIllumination(offGi()), "case 2: GI off before the chain");
    render(e, 2);
    e->takeLastError();
    CHECK(scene->setGlobalIllumination(hybridGi(true)), "case 2: the cascade chain is accepted");
    render(e, 6);
    checkNoEngineError(e, "case 2 chain build");
    st = scene->giStatus();
    CHECK(st.vctBound, "case 2: the cascade chain is bound");
    CHECK_MSG(st.cascades.size() >= 2u, "case 2: the chain has cascades (%zu)",
              st.cascades.size());
    Image chain;
    if (!view->readPixels(chain)) { std::printf("FAIL: readPixels (chain)\n"); return 1; }
    const float chainLum = bounceLum(chain);
    CHECK_MSG(chainLum > offLum + 0.002f,
              "case 2: the chain lights the floor (%.4f vs %.4f off)", chainLum, offLum);

    // ...and the re-enable of the same chain, which is the flip the ledger's
    // observation named.
    CHECK(scene->setGlobalIllumination(offGi()), "case 2: the chain is torn down");
    render(e, 4);
    checkNoEngineError(e, "case 2 teardown");
    CHECK(scene->setGlobalIllumination(hybridGi(true)), "case 2: the chain is re-enabled");
    render(e, 6);
    checkNoEngineError(e, "case 2 chain re-enable");
    st = scene->giStatus();
    CHECK(st.vctBound, "case 2: the re-enabled chain is bound");
    Image rechain;
    if (!view->readPixels(rechain)) { std::printf("FAIL: readPixels (rechain)\n"); return 1; }
    CHECK_MSG(std::fabs(bounceLum(rechain) - chainLum) < 0.006f,
              "case 2: the re-enabled chain is the same picture (%.4f vs %.4f)",
              bounceLum(rechain), chainLum);

    // =====================================================================
    // CASE 3 — the whole sequence the ledger recorded, in one go
    // =====================================================================
    // off -> hybrid -> off -> cascades, with the error sink drained after each
    // step: the transition itself is what is under test, so no case here may
    // leave a recorded engine error behind.
    const GiParams seq[4] = { offGi(), hybridGi(false), offGi(), hybridGi(true) };
    const char *seqName[4] = { "off", "hybrid", "off again", "cascades" };
    for (int i = 0; i < 4; ++i) {
        CHECK_MSG(scene->setGlobalIllumination(seq[i]), "case 3: %s accepted", seqName[i]);
        render(e, 5);
        checkNoEngineError(e, seqName[i]);
    }
    st = scene->giStatus();
    CHECK(st.vctBound, "case 3: the arm ends up bound");

    scene->setGlobalIllumination(offGi());
    render(e, 2);
    std::printf("%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
