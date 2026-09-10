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
//     hybrid        r=1.000 g=0.059 b=0.059      <- the red wall, via a probe
//     hybrid, bailed r=0.000 g=0.000 b=0.000     <- indistinguishable from VCT
//
// The hybrid reading was r=0.251 until ogre-patch 0017 landed
// (2026-09-07): upstream divided probe reflections by the NUMBER of
// overlapping probes, and this scene runs four of them. Case (e) below is the
// fence that keeps it fixed.
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
#include <string>

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
    // Built through the raw verbs rather than enginetest::addTestCube (which is
    // exactly this, byte for byte) only so the HDR case below can reach its
    // MaterialId — see there for why a 100% reflector cannot measure clipping.
    PbrParams mirrorParams;
    mirrorParams.albedo = Colour(1.0f, 1.0f, 1.0f);
    mirrorParams.metalness = 1.0f;
    mirrorParams.roughness = 0.0f;
    const NodeId    mirror     = s->createNode();
    const MeshId    mirrorMesh = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId mirrorMat = s->createPbrMaterial(mirrorParams);
    CHECK(mirror && mirrorMesh && mirrorMat && s->attachMesh(mirror, mirrorMesh, mirrorMat),
          "the mirror box attaches");
    s->setNodeTransform(mirror, Vec3(0.0f, 2.0f, 0.0f), Quat(), Vec3(1.6f, 1.6f, 1.6f));

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

    // ---- (d) THE HELPER CHANNEL (P1b) -------------------------------------
    // The editor grid and the light icons used to be baked into every probe
    // capture: they carried kVisibleBit like everything else, and the probe
    // workspace set no visibility_mask at all. kHelperBit inverts that — helpers
    // carry it INSTEAD of kVisibleBit and the probe face pass asks for
    // kVisibleBit only.
    //
    // The witness is a saturated GREEN unlit plate standing just in front of the
    // red wall, i.e. exactly in the direction this suite's mirror pixel samples,
    // and BEHIND the camera, so it cannot reach the frame any way but through a
    // probe. It is built the way the real grid is: an unlit, depth-tested
    // material on a node marked helper BEFORE the geometry attaches (the order
    // the mirror uses).
    //
    // Two readings, and the second is what makes the first mean something: with
    // the flag ON the mirror must still show the RED WALL, and with the flag OFF
    // the very same plate must take the mirror over. Without that second half a
    // green-free reflection would prove nothing — the plate might simply not be
    // in any probe's view.
    {
        const NodeId helper = s->createNode();
        const MeshId helperMesh = s->createMesh(enginetest::unitCubeMesh());
        const MaterialId helperMat = s->createUnlitMaterial(Colour(0.0f, 1.0f, 0.0f), true);
        s->setNodeHelper(helper, true);
        CHECK(helper && helperMesh && helperMat && s->attachMesh(helper, helperMesh, helperMat),
              "helper: the unlit witness plate attaches");
        CHECK(s->nodeHelper(helper), "helper: setNodeHelper reads back");
        s->setNodeTransform(helper, Vec3(0.0f, 2.5f, 3.85f), Quat(),
                            Vec3(7.0f, 4.0f, 0.1f));

        CHECK(s->setGlobalIllumination(hybrid), "helper: the hybrid rebuilds with the plate in");
        render(engine.get());
        view->readPixels(img);
        const Colour withHelper = img.at(mirrorX, mirrorY);
        show("mirror, helper plate (flagged)", withHelper);
        CHECK(withHelper.r > withHelper.g + 0.12f,
              "a helper-flagged object does NOT appear in the probe capture");
        CHECK(std::fabs(withHelper.r - hyMirror.r) < 0.06f,
              "...and the reflection is the same one it was before the plate existed");

        // The same plate, no longer a helper: now it MUST take the mirror over.
        s->setNodeHelper(helper, false);
        CHECK(!s->nodeHelper(helper), "helper: clearing the flag reads back");
        CHECK(s->setGlobalIllumination(hybrid), "helper: the hybrid rebuilds with the plate visible");
        render(engine.get());
        view->readPixels(img);
        const Colour unflagged = img.at(mirrorX, mirrorY);
        show("mirror, helper plate (unflagged)", unflagged);
        CHECK(unflagged.g > unflagged.r + 0.05f,
              "the SAME plate, unflagged, IS captured — so the flag is what removed it");

        // Put it back so nothing after this reads a green room.
        s->setNodeHelper(helper, true);
        CHECK(s->removeNode(helper), "helper: witness removed");
    }

    // ---- (e) THE OVERLAPPING-PROBE DIVISION (ogre-patch 0017) --------------
    // Upstream's hybrid piece divided the finished blend by the NUMBER of
    // probes covering the pixel, after pccEnvS had already been normalised by
    // the sum of their fades — so the same reflection got darker the more
    // probes saw it. This case is the regression fence: the SAME reflection,
    // captured by one probe and by four, must be the same brightness.
    //
    // Measured here on the day the patch landed, everything else held (the
    // staged media file the only difference), on the FULL-power reading this
    // suite takes a few lines above:
    //     without 0017   4 probes r 0.251
    //     with    0017   4 probes r 1.000   (clipped; the true value is ~1.004)
    // and on this block's dimmed pair:
    //     with    0017   1 probe r 0.271, 4 probes r 0.275   (1.4% apart)
    //
    // The light is dimmed for this block for one reason: at full power the
    // corrected 4-probe reading CLIPS at 1.000 and a clipped number cannot
    // measure a ratio. It is restored immediately afterwards.
    {
        LightDesc dim;
        dim.type = LightType::Directional;
        dim.colour = Colour(1.0f, 1.0f, 1.0f);
        dim.intensity = 1.2f / 3.14159265358979323846f;
        CHECK(s->setLight(lightNode, dim), "multi-probe: the light dims for an unclipped reading");

        GiParams one = hybrid;
        one.pccProbesX = one.pccProbesY = one.pccProbesZ = 1;
        CHECK(s->setGlobalIllumination(one), "multi-probe: the 1-probe grid builds");
        render(engine.get());
        view->readPixels(img);
        const Colour oneProbe = img.at(mirrorX, mirrorY);
        show("mirror, 1 probe", oneProbe);
        CHECK(s->giStatus().probeCount == 1, "multi-probe: exactly one probe exists");
        CHECK(oneProbe.r > 0.10f && oneProbe.r < 0.99f,
              "multi-probe: the 1-probe reading is a real, UNCLIPPED reflection");

        CHECK(s->setGlobalIllumination(hybrid), "multi-probe: the 4-probe grid builds");
        render(engine.get());
        view->readPixels(img);
        const Colour fourProbes = img.at(mirrorX, mirrorY);
        show("mirror, 4 probes", fourProbes);
        CHECK(s->giStatus().probeCount == 4, "multi-probe: exactly four probes exist");
        CHECK(fourProbes.r < 0.99f, "multi-probe: the 4-probe reading is unclipped too");

        // THE assertion. 10% of the larger reading: a probe reflection may
        // legitimately shift a little between placements (four probes sit at
        // the region's quarters, one sits at its centre, so the parallax
        // correction lands on slightly different wall texels), but it may not
        // shift by a FACTOR. Before 0017 this ratio was 0.25 — exactly 1/N.
        const float larger = std::fmax(oneProbe.r, fourProbes.r);
        const float delta = std::fabs(oneProbe.r - fourProbes.r);
        std::printf("   1-probe vs 4-probe: %.3f vs %.3f  (delta %.1f%% of the larger)\n",
                    oneProbe.r, fourProbes.r, 100.0f * delta / larger);
        CHECK(delta < 0.10f * larger,
              "FOUR probes reflect the wall as brightly as ONE (ogre-patch 0017: the hybrid "
              "no longer divides probe reflections by the probe count)");

        // Restore the light for everything after this block.
        LightDesc full = dim;
        full.intensity = 6.0f / 3.14159265358979323846f;
        CHECK(s->setLight(lightNode, full), "multi-probe: the light is restored");
    }

    // ---- (e2) THE CLEAR-COAT PERMUTATION COMPILES -------------------------
    // Patch 0017 rewrites the clear-coat lobe's blend as well as the main one,
    // and `clear_coat + vct_num_probes + hlms_enable_cubemaps_auto` is a shader
    // permutation nothing in this program otherwise builds — a permutation that
    // fails to compile is silent until somebody puts a lacquered object in a
    // hybrid-lit room. So: a clear-coat slab beside the mirror, and the witness
    // is that the pixel where it stands CHANGES. (Change, not brightness: it
    // proves the object was actually rasterized there, which a fixed threshold
    // against an unknown background would not.)
    {
        const unsigned coatX = 111, coatY = 64;   // beside the mirror, on the far wall
        render(engine.get());
        view->readPixels(img);
        const Colour before = img.at(coatX, coatY);

        const NodeId coated = s->createNode();
        const MeshId coatedMesh = s->createMesh(enginetest::unitCubeMesh());
        PbrParams coat;
        coat.albedo = Colour(0.5f, 0.5f, 0.5f);
        coat.metalness = 0.0f;
        coat.roughness = 0.5f;
        coat.clearCoat = 1.0f;
        coat.clearCoatRoughness = 0.1f;
        const MaterialId coatMat = s->createPbrMaterial(coat);
        CHECK(coated && coatedMesh && coatMat && s->attachMesh(coated, coatedMesh, coatMat),
              "clear coat: the lacquered slab attaches");
        s->setNodeTransform(coated, Vec3(1.15f, 2.0f, 0.0f), Quat(), Vec3(0.4f, 1.6f, 0.4f));
        CHECK(s->setGlobalIllumination(hybrid), "clear coat: the hybrid rebuilds around it");
        render(engine.get());
        view->readPixels(img);
        const Colour after = img.at(coatX, coatY);
        show("clear-coat pixel, before", before);
        show("clear-coat pixel, after", after);
        CHECK(std::fabs(after.r - before.r) + std::fabs(after.g - before.g) +
                  std::fabs(after.b - before.b) > 0.02f,
              "clear coat: the slab rendered — the clear-coat + VCT + probes shader "
              "permutation compiles (patch 0017 touches its blend too)");
        CHECK(engine->lastError().empty(),
              "clear coat: ...and the engine reported no error building it");
        CHECK(s->removeNode(coated), "clear coat: slab removed");
    }

    // ---- (f) HDR PROBE CAPTURES (P3a) -------------------------------------
    // An 8-bit probe target clamps at capture time, BEFORE the IBL convolution
    // that spreads a highlight across the mip chain — so anything brighter than
    // white arrives in the reflection as flat white and loses its colour with
    // its brightness. In a room the lamps and the windows ARE the reflection
    // content, which is why this is worth 2x the probe VRAM at High.
    //
    // The witness is an EMISSIVE plate where the helper plate stood: black
    // albedo (so it contributes no diffuse), emissive (6, 1.5, 1.5) — four
    // times as red as it is green, and every channel of it over 1.0. Clamped to
    // 8-bit sRGB both channels saturate and the plate reflects as WHITE; in
    // floating point the 4:1 ratio survives. So the assertion is about HUE, not
    // just brightness, which no exposure or tone-mapping difference can fake.
    //
    // probeHdr is pinned rather than reached through GiQuality::High on purpose:
    // High also quadruples the probe resolution and (P3b) turns shadows on, and
    // a test that moved three things at once would measure none of them.
    //
    // THE MIRROR IS DIMMED TO 15% FOR THIS BLOCK, and the first attempt failed
    // without it: a 100% reflector hands the over-bright plate straight to an
    // 8-bit FRAME BUFFER, so both readings came back (1,1,1) and the test
    // measured the output's clipping instead of the probe's. A 15% reflector
    // (dark chrome) puts the HDR reading at ~0.9/0.22 and the LDR one at a flat
    // 0.15 — the difference between them is then entirely the probe format's.
    {
        PbrParams dimMirror = mirrorParams;
        dimMirror.albedo = Colour(0.15f, 0.15f, 0.15f);
        CHECK(s->setPbrMaterial(mirrorMat, dimMirror),
              "hdr: the mirror dims to 15% so the FRAME does not clip before the probe can");

        const NodeId lamp = s->createNode();
        const MeshId lampMesh = s->createMesh(enginetest::unitCubeMesh());
        PbrParams lampMat;
        lampMat.albedo = Colour(0.0f, 0.0f, 0.0f);
        lampMat.metalness = 0.0f;
        lampMat.roughness = 1.0f;
        lampMat.emissive = Colour(6.0f, 1.5f, 1.5f);
        const MaterialId lampMatId = s->createPbrMaterial(lampMat);
        CHECK(lamp && lampMesh && lampMatId && s->attachMesh(lamp, lampMesh, lampMatId),
              "hdr: the over-bright emissive plate attaches");
        s->setNodeTransform(lamp, Vec3(0.0f, 2.0f, 3.85f), Quat(), Vec3(7.0f, 4.0f, 0.1f));

        GiParams ldr = hybrid;
        ldr.probeHdr = GiToggle::Off;
        CHECK(s->setGlobalIllumination(ldr), "hdr: the LDR (8-bit) capture builds");
        render(engine.get());
        view->readPixels(img);
        const Colour ldrPix = img.at(mirrorX, mirrorY);
        show("mirror, emissive plate, LDR probe", ldrPix);
        CHECK(!s->giStatus().probeHdr, "hdr: giStatus reports an LDR capture");

        GiParams hdr = hybrid;
        hdr.probeHdr = GiToggle::On;
        CHECK(s->setGlobalIllumination(hdr), "hdr: the RGBA16F capture builds");
        render(engine.get());
        view->readPixels(img);
        const Colour hdrPix = img.at(mirrorX, mirrorY);
        show("mirror, emissive plate, HDR probe", hdrPix);
        CHECK(s->giStatus().probeHdr, "hdr: giStatus reports an HDR capture");
        CHECK(s->giStatus().probeCount == 4 && s->giStatus().pccBound,
              "hdr: the probe grid still built and bound at RGBA16F "
              "(the IBL compute path accepts a float target)");

        // The LDR capture clipped: r and g both hit the ceiling, so the
        // reflection is achromatic. (It is white rather than dim BECAUSE it
        // clipped — brightness alone would not distinguish the two.)
        std::printf("   LDR r-g = %+.3f   HDR r-g = %+.3f\n",
                    ldrPix.r - ldrPix.g, hdrPix.r - hdrPix.g);
        CHECK(std::fabs(ldrPix.r - ldrPix.g) < 0.10f,
              "hdr: the 8-bit capture CLIPS an over-bright emitter to flat white "
              "(this is the defect being measured, not a passing grade)");
        CHECK((hdrPix.r - hdrPix.g) > (ldrPix.r - ldrPix.g) + 0.15f,
              "hdr: the float capture keeps the emitter's colour instead of clipping it");
        CHECK(hdrPix.r >= ldrPix.r - 0.02f,
              "hdr: ...and is no dimmer for it");

        CHECK(s->removeNode(lamp), "hdr: witness removed");
        CHECK(s->setPbrMaterial(mirrorMat, mirrorParams), "hdr: the mirror is restored to 100%");
    }

    // ---- (g) SHADOWED PROBE CAPTURES (P3b) --------------------------------
    // v1 probe captures ran without a shadow node, so reflections showed a
    // uniformly lit room. The shadowed workspace runs the scene's shadow node
    // with `recalculate` in every face pass (JahshakaPcc.compositor).
    //
    // This block re-aims the light and it has to: the suite's light travels
    // almost exactly along the mirror's reflection ray (+Z), so ANY occluder
    // that shadows what the mirror looks at also stands in front of it, and the
    // test would measure occlusion rather than shadowing. A light angled down
    // at 64 degrees separates the two — the occluder sits at y ~ 4.1 while the
    // reflection ray travels at y = 2.0 — and its shadow still lands across the
    // red wall at the height the mirror samples.
    {
        CHECK(s->removeNode(lightNode), "shadows: the near-horizontal light is removed");
        const NodeId steep = enginetest::addDirectionalLight(s, Vec3(0.0f, -0.9f, 0.436f), 6.0f);
        CHECK(steep != 0, "shadows: a steeply angled light replaces it");

        // The occluder: a wide, thin slab high above the reflection ray, in the
        // light's path to the part of the red wall the mirror samples.
        const NodeId blocker = addSlab(s, white, Vec3(0.0f, 4.1f, 3.0f), Vec3(4.0f, 0.4f, 1.0f));
        CHECK(blocker != 0, "shadows: the occluder is in place");

        GiParams unshadowed = hybrid;
        unshadowed.probeShadows = GiToggle::Off;
        CHECK(s->setGlobalIllumination(unshadowed), "shadows: the unshadowed capture builds");
        render(engine.get());
        view->readPixels(img);
        const Colour noShadow = img.at(mirrorX, mirrorY);
        show("mirror, unshadowed capture", noShadow);
        CHECK(!s->giStatus().probeShadows, "shadows: giStatus reports an unshadowed capture");
        CHECK(noShadow.r > 0.15f,
              "shadows: the unshadowed reflection still shows the lit red wall "
              "(the test is looking at something)");

        GiParams shadowed = hybrid;
        shadowed.probeShadows = GiToggle::On;
        CHECK(s->setGlobalIllumination(shadowed), "shadows: the shadowed capture builds");
        render(engine.get());
        view->readPixels(img);
        const Colour withShadow = img.at(mirrorX, mirrorY);
        show("mirror, shadowed capture", withShadow);
        CHECK(s->giStatus().probeShadows,
              "shadows: giStatus reports a shadowed capture (the shadowed workspace was found "
              "and the shadow node existed to recalculate)");
        CHECK(s->giStatus().probeCount == 4 && s->giStatus().pccBound,
              "shadows: the probe grid still built and bound through the shadowed workspace");
        std::printf("   unshadowed r = %.3f   shadowed r = %.3f\n", noShadow.r, withShadow.r);
        CHECK(withShadow.r < noShadow.r * 0.75f,
              "shadows: the occluder's shadow is IN the reflection — the shadowed capture is "
              "measurably darker where the unshadowed one is not");

        // Auto follows the quality dial, which is the shipping contract: the
        // suite runs at Medium, so Auto must resolve to OFF here and to ON at
        // High. Cheap to assert and it is the half users actually get.
        GiParams autoMed = hybrid;   // quality Medium, probeShadows Auto
        CHECK(s->setGlobalIllumination(autoMed), "shadows: auto at Medium builds");
        render(engine.get(), 1);
        CHECK(!s->giStatus().probeShadows && !s->giStatus().probeHdr,
              "shadows: Auto at Medium quality means unshadowed, LDR captures");
        GiParams autoHigh = hybrid;
        autoHigh.quality = GiQuality::High;
        CHECK(s->setGlobalIllumination(autoHigh), "shadows: auto at High builds");
        render(engine.get(), 1);
        CHECK(s->giStatus().probeShadows && s->giStatus().probeHdr,
              "shadows: Auto at High quality means shadowed, HDR captures");

        CHECK(s->removeNode(blocker), "shadows: occluder removed");
    }

    // ---- (h) A SKY IBL *AND* THE HYBRID -----------------------------------
    // The combination this lane found broken, that nothing tested, and that no
    // upstream patch can fix: the PBS pixel shader has ONE env-probe texture
    // slot, automatic PCC fills it with a probe cube ARRAY, and a datablock
    // carrying its own PBSM_REFLECTION cubemap (which every PBR datablock gets
    // the moment the sky IBL is armed) makes HlmsPbs suppress
    // `use_parallax_correct_cubemaps` while the pass keeps
    // `hlms_enable_cubemaps_auto`. Three compile failures follow, each revealed
    // by fixing the one before it — toProbeLocalSpace/localCorrect undeclared,
    // then vctSpecPosVS undeclared, then SampleEnvProbe having no
    // textureCubeArray overload — and the third one is the verdict: the manual
    // cubemap is UNSAMPLEABLE in that permutation, so the two are mutually
    // exclusive by construction. The fix is ours and lives in
    // OgreScene::reflectionTexForDatablocks (OgreSky.cpp): while auto PCC is
    // bound we do not bind the IBL cubemap at all. Nothing is lost — the probe
    // captures include the sky, so the probes ARE the environment.
    //
    // It was pre-existing (any user picking VCT+Probes on a scene with a sky got
    // objects that did not draw) and it surfaced here only because P6 makes the
    // Epic tier select the hybrid, which took scripting.e2e.sky_ibl_churn from
    // green to a SEGFAULT.
    //
    // The assertion is deliberately the ordinary one: the mirror still shows the
    // red wall. A PBS shader that does not compile does not draw, so a
    // red-dominant mirror pixel IS the proof that the permutation built. RED-
    // FIRST VERIFIED: with the fix reverted this case reads r=0.000 g=0.000.
    // The REFLECTION half of SkyDesc is what matters, not the sky half: a sky
    // alone does not touch datablocks, while the IBL reflection cubemap is
    // bound to every PBR datablock as PBSM_REFLECTION (OgreSky.cpp
    // applyReflectionToAll). A first attempt using a plain equirect sky passed
    // WITHOUT the patch and proved nothing — the defect needs the manual
    // reflection texture. (Hence a description whose mode stays NoSky and whose
    // `reflections` half carries the six faces.)
    {
        std::string facePaths[6];
        TextureId faces[6] = {};
        bool facesOk = true;
        for (int f = 0; f < 6; ++f) {
            facePaths[f] = "gi_pcc_mirror_iblface" + std::to_string(f) + ".ppm";
            FILE *fp = std::fopen(facePaths[f].c_str(), "wb");
            std::fprintf(fp, "P6 8 8 255\n");
            for (int i = 0; i < 8 * 8; ++i) {
                std::fputc(20, fp); std::fputc(30, fp); std::fputc(90, fp);
            }
            std::fclose(fp);
            faces[f] = s->loadTexture(facePaths[f], true);
            if (!faces[f]) facesOk = false;
        }
        CHECK(facesOk, "sky+hybrid: the six IBL face textures load");
        SkyDesc iblOnly;
        iblOnly.reflections = true;
        for (int f = 0; f < 6; ++f) iblOnly.reflectionFaces[f] = faces[f];
        CHECK(s->setSky(iblOnly),
              "sky+hybrid: the IBL reflection cubemap arms (PBSM_REFLECTION on every "
              "PBR datablock — the manual-probe half of the broken combination)");
        CHECK(s->setGlobalIllumination(hybrid), "sky+hybrid: the hybrid rebuilds under the IBL");
        render(engine.get(), 6);
        view->readPixels(img);
        const Colour skyMirror = img.at(mirrorX, mirrorY);
        show("mirror, sky IBL + hybrid", skyMirror);
        CHECK(skyMirror.r > skyMirror.g + 0.10f && skyMirror.r > skyMirror.b + 0.10f,
              "sky+hybrid: the mirror still reflects the red wall — the manual-reflection + "
              "auto-PCC combination renders (the env-probe slot is not fought over)");
        CHECK(engine->lastError().empty(),
              "sky+hybrid: ...and the engine reported no error doing it");
        SkyDesc noIbl;
        noIbl.reflections = true;      // stated, and six zeros = clear
        CHECK(s->setSky(noIbl), "sky+hybrid: the IBL reflection is cleared");
        for (int f = 0; f < 6; ++f) std::remove(facePaths[f].c_str());
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
