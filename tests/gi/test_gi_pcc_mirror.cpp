// The REAL PCC gate (REFLECTIONS_ADOPTION_SPEC.md §3, findings F12/F13/F14):
// the first test anywhere that looks at a parallax-corrected-cubemap probe
// REFLECTION and at whether the probe arm armed at all.
//
// Why it exists: until this suite, `buildPcc` could log "PCC probe workspace
// missing; hybrid renders as plain VCT" and return (OgreGi.cpp) with every
// caller — and every green suite — still believing the hybrid was live.
// gi.modes' hybrid case only ever asserted the VCT floor bounce, which
// survives that bail untouched. Two things close the hole:
//   1. Scene::giStatus() reports what GI ACHIEVED (probe count, whether this
//      scene's probe/VCT bindings are live on the process-wide HlmsPbs), so
//      the silent bail is an assertion failure here;
//   2. a mirror pixel that must actually contain the red wall's hue.
//
// The scene: a CLOSED room (floor + ceiling + four walls) so the probes can
// shrink-fit to it — PccPerPixelGridPlacement::buildEnd reads the probe depth
// back through the DepthCompressor pass and refits each probe's shape to the
// geometry around it, which only works when the probe is enclosed. The +Z wall
// (the one BEHIND the camera) is saturated red; everything else is white. A
// roughness-0 metallic box sits in the middle and the camera looks straight at
// its +Z face, so the centre pixel's reflection vector points back past the
// camera into the red wall — the classic "the mirror shows the wall behind
// you". With the hybrid on, that pixel is red. With plain VCT it is not (see
// the light comment in main() for the measured reason — a directional light
// inside a sealed room injects nothing into the voxel volume, so cone-traced
// reflections here have nothing to carry). With GI off it is black, because
// nothing else in this scene reflects anything.
//
// F12, measured rather than argued (2026-09-07, RTX 4080S, Debug+ASan): with
// the probe workspace definition renamed in the staged compositor so buildPcc
// takes its bail, **gi.modes stayed 27/27 green and exited 0** while this suite
// went red on seven checks — the three giStatus ones and the four mirror-pixel
// ones. Reference readings for the centre mirror pixel, bit-identical across
// three consecutive runs:
//     GI off        r=0.000 g=0.000 b=0.000
//     plain VCT     r=0.000 g=0.000 b=0.000
//     hybrid        r=0.251 g=0.016 b=0.016      <- the red wall, via a probe
//     hybrid, bailed r=0.000 g=0.000 b=0.000     <- indistinguishable from VCT
//
// Determinism discipline (spec §3, and the MESH_BAKE "Showroom is not
// deterministic" facts): offscreen view (so MSAA stays 1x), no SSAO, no planar
// reflections, no autoRefresh churn between the reads, no geometry moves while
// a mode is live, EXPLICIT GI bounds (so probe placement does not depend on the
// auto-bounds heuristic), and hue RANGES rather than exact colours — probe
// shrink-fit reads GPU depth, so the exact reflected texel is driver-dependent
// even though the wall it lands on is not.
//
// macOS (F13) is explicitly NEXT MAC SESSION, not this lane: the PCC bind
// texture is a TextureFlags::ManualTexture (upstream
// OgreParallaxCorrectedCubemapAuto.cpp), the exact class of the MoltenVK
// sampling defect in DOCS/MACOS_BUILD.md §6.1. It is GPU-FILLED (a copy, not a
// CPU upload), so it may well be fine — running THIS suite on the Mac is the
// measurement. A red-hue failure there is that defect, not this test.
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

static void render(Engine *e, int frames = 4)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static void show(const char *what, const Colour &c)
{
    std::printf("   %-34s r=%.3f g=%.3f b=%.3f   (r-g)=%+.3f\n",
                what, c.r, c.g, c.b, c.r - c.g);
}

static const char *modeName(GiMode m)
{
    switch (m) {
    case GiMode::Off:              return "off";
    case GiMode::InstantRadiosity: return "instant_radiosity";
    case GiMode::Vct:              return "vct";
    case GiMode::VctPccHybrid:     return "vct_pcc_hybrid";
    }
    return "?";
}

static void showStatus(const char *what, const GiStatus &st)
{
    std::printf("   %-34s mode=%s probes=%d pccBound=%s vctBound=%s\n",
                what, modeName(st.mode), st.probeCount,
                st.pccBound ? "true" : "false", st.vctBound ? "true" : "false");
}

// One wall/floor/ceiling slab of the closed room.
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
    cfg.logFile = "test-gi-pcc-mirror-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("pcc", 128, 128, Colour(0, 0, 0));
    Scene *s = engine->createScene("pcc");
    view->setScene(s);
    // Zero ambient: with no sky and no ambient, a roughness-0 metal reflects
    // NOTHING unless a GI system gives it something to reflect. That is what
    // makes the mirror pixel a clean probe-vs-no-probe signal.
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // ---- the closed room -------------------------------------------------
    // Interior: x in [-4,4], y in [0,5], z in [-4,4]; 0.4-thick shell.
    const Colour white(0.85f, 0.85f, 0.85f);
    const Colour red(1.0f, 0.02f, 0.02f);
    addSlab(s, white, Vec3(0.0f, -0.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f));   // floor
    addSlab(s, white, Vec3(0.0f,  5.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f));   // ceiling
    addSlab(s, white, Vec3(0.0f,  2.5f, -4.2f), Vec3(8.8f, 5.0f, 0.4f));  // -Z wall
    addSlab(s, white, Vec3(-4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));   // -X wall
    addSlab(s, white, Vec3( 4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));   // +X wall
    // THE red wall: +Z, i.e. behind the camera. It is the only saturated thing
    // in the room, so any red in the mirror came from it.
    addSlab(s, red, Vec3(0.0f, 2.5f, 4.2f), Vec3(8.8f, 5.0f, 0.4f));

    // ---- the mirror ------------------------------------------------------
    // Metalness 1 + roughness 0: a pure specular surface with no diffuse term
    // at all, so its pixels are ONLY what the reflection integral returns.
    const NodeId mirror = enginetest::addTestCube(s, Colour(1.0f, 1.0f, 1.0f), 1.0f, 0.0f);
    enginetest::setNodePosition(s, mirror, Vec3(0.0f, 2.0f, 0.0f));
    enginetest::setNodeScale(s, mirror, Vec3(1.6f, 1.6f, 1.6f));

    // ---- light -----------------------------------------------------------
    // A single directional light, aimed almost horizontally at the red wall: it
    // lights that wall's inner face nearly head-on (N.L ~ 0.99), the floor at a
    // grazing 0.12, and nothing else — the ceiling and the other three walls'
    // INNER faces all face away from it, so they are pure black until GI turns
    // on. Shadows are off, so the direct pass is not blocked by the shell.
    //
    // THE ONE THING TO KNOW ABOUT THIS SCENE (measured here, 2026-09-07; it is
    // what makes assertion (b) as clean as it is): a DIRECTIONAL light inside a
    // SEALED room injects nothing into the VCT voxel volume. Ogre's light
    // injection compute shader ray-marches from every voxel towards the light
    // through the voxel albedo volume and multiplies an alpha down as it
    // crosses geometry (Samples/Media/VCT/LightInjection_piece_cs.any — the
    // `alpha *= max(0, 1 - albedoAtIt.w * p_thinWallCounter)` loop, which for a
    // directional light only stops when it exits the volume). Every interior
    // voxel's march crosses the shell, so its injected radiance is ~0; only the
    // shell's OUTER faces, which march straight out of the volume, get lit.
    // Consequence in this room: the red wall is bright red in the RENDER (and
    // therefore in the probe capture, which is a render) while its voxels are
    // dark — so cone-traced reflections of it are black and probe reflections
    // of it are red. That is a genuinely maximal probe-vs-cone discriminator,
    // and it is also why the diffuse assertion below is a BRIGHTNESS assertion
    // and not a hue one: the achromatic light VCT does put into this room comes
    // from the shell's lit outer faces bleeding through ~3-voxel-thick slabs,
    // not from the red wall. The red-bounce HUE contract stays where it already
    // lives and works — gi.modes, on an open scene, which is in this lane's gate.
    const NodeId lightNode = enginetest::addDirectionalLight(s, Vec3(0.0f, -0.12f, 0.993f), 6.0f);
    CHECK(lightNode != 0, "directional light created");

    // ---- camera ----------------------------------------------------------
    // Straight at the mirror's +Z face from inside the room, between the box
    // and the red wall. The box overfills the middle of the 128x128 frame; the
    // bottom rows see the floor past the box's lower edge, and the left/right
    // columns see the far (-Z) wall past its sides.
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 3.6f), Vec3(0.0f, 2.0f, 0.0f));
    const unsigned mirrorX = 64, mirrorY = 64;   // the reflecting face
    const unsigned floorX  = 64, floorY  = 120;  // floor at z ~ -1.9
    // The far wall, past the box's left edge: an inner face the direct light
    // never reaches, so it is black until a GI system lights it. That makes it
    // the sharpest possible "the diffuse half is live" witness.
    const unsigned wallX = 10, wallY = 64;

    // ---- baseline: GI off ------------------------------------------------
    render(engine.get());
    Image img;
    view->readPixels(img);
    const Colour offMirror = img.at(mirrorX, mirrorY);
    const Colour offFloor  = img.at(floorX, floorY);
    const Colour offWall   = img.at(wallX, wallY);
    show("mirror,  GI off", offMirror);
    show("floor,   GI off", offFloor);
    show("farwall, GI off", offWall);
    CHECK(offMirror.r < 0.10f && offMirror.g < 0.10f && offMirror.b < 0.10f,
          "with GI off the roughness-0 metal reflects nothing (near black)");
    CHECK(std::fabs(offFloor.r - offFloor.g) < 0.03f,
          "floor's direct light is neutral before GI (no red bias)");
    CHECK(offFloor.r > 0.05f, "the floor IS directly lit (the test is looking at lit geometry)");
    CHECK(offWall.r < 0.05f && offWall.g < 0.05f,
          "the far wall's inner face gets NO direct light (black before GI)");

    {
        const GiStatus st = s->giStatus();
        showStatus("giStatus, GI off", st);
        CHECK(st.mode == GiMode::Off, "giStatus reports mode off");
        CHECK(st.probeCount == 0, "giStatus reports no probes with GI off");
        CHECK(!st.pccBound && !st.vctBound, "giStatus reports nothing bound with GI off");
    }

    // Explicit GI bounds around the room shell. Deliberate: the auto heuristic
    // is P1a's subject and would make this suite's probe placement move when
    // that phase lands. The probes still SHRINK-FIT from these bounds down to
    // the room via the depth readback — that is the mechanism under test.
    const Vec3 giMin(-4.6f, -0.6f, -4.6f), giMax(4.6f, 5.6f, 4.6f);

    // ---- plain VCT: cone-traced reflections only -------------------------
    GiParams vct;
    vct.mode = GiMode::Vct;
    vct.quality = GiQuality::Medium;      // 64^3 voxels
    vct.numBounces = 2;
    vct.boundsMin = giMin; vct.boundsMax = giMax;
    const bool vctOk = s->setGlobalIllumination(vct);
    if (!vctOk) std::printf("   engine error: %s\n", engine->lastError().c_str());
    CHECK(vctOk, "setGlobalIllumination(Vct) succeeds");
    render(engine.get());
    view->readPixels(img);
    const Colour vctMirror = img.at(mirrorX, mirrorY);
    const Colour vctFloor  = img.at(floorX, floorY);
    const Colour vctWall   = img.at(wallX, wallY);
    show("mirror,  plain VCT", vctMirror);
    show("floor,   plain VCT", vctFloor);
    show("farwall, plain VCT", vctWall);
    {
        const GiStatus st = s->giStatus();
        showStatus("giStatus, plain VCT", st);
        CHECK(st.mode == GiMode::Vct, "giStatus reports mode vct");
        CHECK(st.probeCount == 0, "plain VCT builds NO probes");
        CHECK(!st.pccBound, "plain VCT binds no parallax-corrected cubemap");
        CHECK(st.vctBound, "plain VCT binds this scene's VctLighting to HlmsPbs");
    }
    // (c) the diffuse regression tie. VCT's diffuse half must still light this
    // room: the far wall's inner face and the floor both gain measurably. The
    // HUE half of the contract (bounced light carries the emitter's colour)
    // stays in gi.modes, on the open scene where it is a clean signal — see the
    // light comment above for why a sealed room cannot carry it. If a later
    // phase of this program breaks diffuse GI while chasing reflections, these
    // two go red here as well as there.
    CHECK(vctWall.r > offWall.r + 0.15f,
          "VCT lights the far wall the direct light never reaches (diffuse GI is live)");
    CHECK(vctFloor.r > offFloor.r + 0.05f,
          "VCT raises the lit floor as well (bounce on top of the direct term)");

    // ---- the hybrid: PCC probes + VCT ------------------------------------
    GiParams hybrid = vct;
    hybrid.mode = GiMode::VctPccHybrid;
    // A small explicit grid: 2 x 1 x 2 = 4 probes. Small enough to stay quick
    // (each probe is 6 face renders + an IBL mip chain, twice — buildStart and
    // buildEnd), large enough that the grid product is a real assertion.
    hybrid.pccProbesX = 2; hybrid.pccProbesY = 1; hybrid.pccProbesZ = 2;
    const int expectedProbes = hybrid.pccProbesX * hybrid.pccProbesY * hybrid.pccProbesZ;
    const bool hybridOk = s->setGlobalIllumination(hybrid);
    if (!hybridOk) std::printf("   engine error: %s\n", engine->lastError().c_str());
    CHECK(hybridOk, "setGlobalIllumination(VctPccHybrid) succeeds");
    render(engine.get());
    view->readPixels(img);
    const Colour hyMirror = img.at(mirrorX, mirrorY);
    const Colour hyFloor  = img.at(floorX, floorY);
    const Colour hyWall   = img.at(wallX, wallY);
    show("mirror,  VCT+PCC hybrid", hyMirror);
    show("floor,   VCT+PCC hybrid", hyFloor);
    show("farwall, VCT+PCC hybrid", hyWall);

    // ---- BAIL HARDENING (F12) -------------------------------------------
    // These three are the assertions the silent bail cannot survive. Verified
    // red-first by renaming the workspace definition in the staged compositor:
    // buildPcc then logs "PCC probe workspace missing; hybrid renders as plain
    // VCT", and exactly these — plus the mirror-hue assertions below — go red
    // while every other suite stays green.
    {
        const GiStatus st = s->giStatus();
        showStatus("giStatus, hybrid", st);
        CHECK(st.mode == GiMode::VctPccHybrid, "giStatus reports mode vct_pcc_hybrid");
        CHECK(st.probeCount == expectedProbes,
              "the hybrid built exactly the requested probe grid (2 x 1 x 2 = 4)");
        CHECK(st.pccBound,
              "the probe grid is BOUND to HlmsPbs (the hybrid did not degrade to plain VCT)");
        CHECK(st.vctBound, "the hybrid keeps the VCT binding as well");
    }

    // ---- (a) the mirror pixel takes the wall's hue ------------------------
    CHECK(hyMirror.r > hyMirror.g + 0.12f && hyMirror.r > hyMirror.b + 0.12f,
          "hybrid: the mirror pixel is RED-dominant (it is showing the red wall)");
    CHECK(hyMirror.r > 0.15f,
          "hybrid: the mirror's red is a real reflection, not a rounding crumb");
    // ---- (b) plain VCT does not ------------------------------------------
    // In THIS scene the cone-traced answer is not merely softer, it is empty:
    // the red wall's voxels were never lit (the directional-injection note on
    // the light above). So the margin is the whole signal, and the assertion
    // that matters is a comparative one — a hybrid that quietly fell back to
    // plain VCT would produce vctMirror's pixel exactly.
    CHECK((hyMirror.r - hyMirror.g) > (vctMirror.r - vctMirror.g) + 0.08f,
          "the probe reflection is measurably redder than the cone-traced one");
    CHECK(hyMirror.r > vctMirror.r + 0.08f,
          "the probe reflection is measurably brighter than the cone-traced one");
    // The diffuse half survives the hybrid unchanged: the probes are ADDED to
    // VCT, they do not replace its diffuse contribution.
    CHECK(hyWall.r > offWall.r + 0.15f,
          "the hybrid keeps VCT's diffuse light on the far wall");

    // ---- idempotence: re-pushing identical params leaves the image alone --
    CHECK(s->setGlobalIllumination(hybrid), "re-pushing identical hybrid params succeeds");
    render(engine.get());
    view->readPixels(img);
    const Colour hyAgain = img.at(mirrorX, mirrorY);
    show("mirror, hybrid re-pushed", hyAgain);
    CHECK(std::fabs(hyAgain.r - hyMirror.r) < 0.06f &&
          std::fabs(hyAgain.g - hyMirror.g) < 0.06f,
          "a full probe rebuild reproduces the same reflection (determinism)");
    {
        const GiStatus st = s->giStatus();
        CHECK(st.probeCount == expectedProbes && st.pccBound,
              "the rebuilt probe grid is the same size and still bound");
    }

    // ---- off restores -----------------------------------------------------
    GiParams off;
    CHECK(s->setGlobalIllumination(off), "setGlobalIllumination(Off) succeeds");
    render(engine.get());
    view->readPixels(img);
    const Colour endMirror = img.at(mirrorX, mirrorY);
    show("mirror, GI off again", endMirror);
    CHECK(endMirror.r < offMirror.r + 0.05f && endMirror.r < 0.10f,
          "turning GI off unbinds the probes: the mirror goes black again");
    {
        const GiStatus st = s->giStatus();
        showStatus("giStatus, GI off again", st);
        CHECK(st.mode == GiMode::Off && st.probeCount == 0 && !st.pccBound && !st.vctBound,
              "giStatus reports everything torn down");
    }

    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
