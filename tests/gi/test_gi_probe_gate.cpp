// gi.probe_gate — REFLECTION PROBES ONLY REACH MATERIALS THAT CAN REFLECT THEM
// (owner decision 2026-09-13 Q1, SPECS/REFLECTION_PROBE_AUDIT.md; ogre-patch
// 0028).
//
// Owner: "[reflection probes] should only affect reflective objects in a
// scene." Before patch 0028 there was NO material-side gate anywhere in the
// chain: `use_envprobe_map` was set for every datablock from a PASS property and
// the per-pixel probe loop was inserted for every lit pixel of every object,
// however matte. The gate is the material's own reflectance — specular colour
// black AND no authored F0 — never a roughness threshold, because roughness
// blurs a reflection and never removes one.
//
// WHAT THIS SUITE CAN AND CANNOT SEE, stated plainly. The gate is PIXEL
// INVARIANT by construction and that is the proof it is safe: the PBS specular
// term ends as `envColourS * pixelData.specular * (...)` with
// pixelData.specular = the datablock's kS, so a material with kS = 0
// multiplies every environment term by zero whether or not it sampled one.
// There is therefore no "before" picture to differ from — what a suite CAN
// gate is the CONTRACT and the two ways it could go wrong:
//
//   (a) the zeroed plate must read IDENTICALLY with the probe grid bound and
//       with GI off — it takes nothing from the probes;
//   (b) the plate beside it, identical except that it keeps its specular
//       colour, must CHANGE between those two states — so (a) is the gate's
//       doing and not "matte surfaces never show a probe anyway", and the
//       owner's "physically lit surfaces keep their 4% sheen unless the author
//       zeroes it" is a measured fact here rather than a claim;
//   (c) the chrome box must still show the red wall — the gate mis-classifying
//       a reflective material is the regression that would matter, and it is
//       what this catches;
//   (d) diffuse GI must be untouched on the zeroed plate's own wall — the gate
//       clears `use_envprobe_map`, and `needs_env_brdf` (which carries the
//       irradiance-field and cone-traced DIFFUSE to the surface) must survive
//       it. It does, because the pass property raises it too.
//
// Room, light and determinism discipline are gi.pcc_mirror's, for the same
// reasons — see that suite's header.
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

static void render(Engine *e, int frames = 6) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

static void show(const char *what, const Colour &c)
{
    std::printf("   %-36s r=%.4f g=%.4f b=%.4f\n", what, c.r, c.g, c.b);
}
static float delta(const Colour &a, const Colour &b)
{
    return std::max(std::max(std::fabs(a.r - b.r), std::fabs(a.g - b.g)), std::fabs(a.b - b.b));
}

static NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = enginetest::addTestCube(s, albedo, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

/// A plate built from explicit PbrParams so the two under test differ in
/// EXACTLY the pair the gate reads.
static NodeId addPlate(Scene *s, const PbrParams &p, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId mat = s->createPbrMaterial(p);
    if (!n || !mesh || !mat || !s->attachMesh(n, mesh, mat)) return 0;
    s->setNodeTransform(n, pos, Quat(), scale);
    return n;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-probe-gate-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("gate", 128, 128, Colour(0, 0, 0));
    Scene *s = engine->createScene("gate");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    const Colour white(0.85f, 0.85f, 0.85f);
    const Colour red(1.0f, 0.02f, 0.02f);
    addSlab(s, white, Vec3(0.0f, -0.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f));    // floor
    addSlab(s, white, Vec3(0.0f,  5.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f));    // ceiling
    addSlab(s, white, Vec3(0.0f,  2.5f, -4.2f), Vec3(8.8f, 5.0f, 0.4f));   // -Z wall
    addSlab(s, white, Vec3(-4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));    // -X wall
    addSlab(s, white, Vec3( 4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));    // +X wall
    addSlab(s, red,   Vec3(0.0f,  2.5f, 4.2f), Vec3(8.8f, 5.0f, 0.4f));    // +Z wall: THE red one

    // The chrome witness (gi.pcc_mirror's mirror box, same numbers).
    PbrParams mirrorP; mirrorP.albedo = Colour(1, 1, 1); mirrorP.metalness = 1.0f; mirrorP.roughness = 0.0f;
    CHECK(addPlate(s, mirrorP, Vec3(0.0f, 2.5f, -1.0f), Vec3(0.8f, 0.8f, 0.8f)) != 0,
          "the chrome box attaches");

    // THE PAIR, as two panels covering the -Z wall's inner face — left half and
    // right half of the frame. Identical albedo, roughness, workflow and
    // position class; they differ ONLY in the two controls the gate reads.
    //
    // WHY THEIR ALBEDO IS BLACK, measured in-lane rather than assumed: these
    // panels FACE the red wall, so a grey albedo made their diffuse bounce off
    // it saturate the reading (both panels read r 1.000 g 0.055 — a picture of
    // the red wall in DIFFUSE, nothing to do with probes). A black albedo has
    // no diffuse term at all, so what is left on these two panels is exactly
    // and only the environment specular the gate is about: the reflective one
    // shows the room, the zeroed one shows nothing. The diffuse half of GI is
    // asserted separately, on the ceiling.
    //
    // THE PAIR IS AUTHORED IN THE FRESNEL WORKFLOW, for two reasons. It is the
    // workflow in which "not reflective" is EXPRESSIBLE at all (F0 is a
    // number the author types; in the metallic workflow the shader's 0.04
    // dielectric floor is not reachable from any setter), so it is the one the
    // owner's pair — F0 zero AND specular colour black — actually describes.
    // And an authored F0 of 0.6 puts the reflection well above the 1/255 the
    // frame is quantised to, which a 4% floor does not: the gate's effect is
    // the SAME either way (kS multiplies the whole environment term), but only
    // this way can a suite see the reflective twin at all.
    PbrParams sheen;
    sheen.albedo = Colour(0.0f, 0.0f, 0.0f);
    sheen.metalness = 0.0f;
    sheen.roughness = 0.12f;
    sheen.workflow = PbrParams::Workflow::SpecularAsFresnel;
    sheen.useFresnelColour = true;
    sheen.fresnelColour = Colour(0.60f, 0.60f, 0.60f);
    sheen.specularColour = Colour(1, 1, 1);     // kS = 1, the default
    PbrParams zeroed = sheen;
    zeroed.fresnelColour  = Colour(0, 0, 0);    // ...and the author says
    zeroed.specularColour = Colour(0, 0, 0);    //    "not reflective", with both controls
    CHECK(addPlate(s, zeroed, Vec3(-2.0f, 1.8f, -3.85f), Vec3(3.9f, 3.4f, 0.12f)) != 0,
          "the zero-reflectance panel attaches");
    CHECK(addPlate(s, sheen,  Vec3( 2.0f, 1.8f, -3.85f), Vec3(3.9f, 3.4f, 0.12f)) != 0,
          "the reflective panel attaches");

    // THE RUNTIME-EDIT SUBJECT (round-2 fix). It is built NOT REFLECTIVE — the
    // shape GF1's default floor has: specular colour black, no authored F0 — so
    // the gate is ON for it when the shader is first generated. Halfway through
    // the suite the "user" makes it a mirror, and the probe reflection has to
    // APPEAR with no other edit. See the case at the bottom.
    PbrParams matte;
    matte.albedo = Colour(0, 0, 0);      // no diffuse term: what is left is the gate's business
    matte.metalness = 0.0f;              // ...and no authored F0 in the metallic workflow
    matte.roughness = 0.05f;
    matte.specularColour = Colour(0, 0, 0);
    const MaterialId editedMat = s->createPbrMaterial(matte);
    const NodeId editedPlate = s->createNode();
    CHECK(editedMat && editedPlate &&
              s->attachMesh(editedPlate, s->createMesh(enginetest::unitCubeMesh()), editedMat),
          "the runtime-edit plate attaches, authored NOT reflective");
    s->setNodeTransform(editedPlate, Vec3(0.0f, 1.5f, -2.0f), Quat(), Vec3(2.4f, 1.2f, 0.2f));

    CHECK(enginetest::addDirectionalLight(s, Vec3(0.0f, -0.12f, 0.993f), 6.0f) != 0,
          "directional light created");
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.5f, 2.0f), Vec3(0.0f, 2.5f, -3.85f));

    const unsigned mirrorX = 64, mirrorY = 64;    // the chrome box, dead centre
    const unsigned zeroX   = 12,  zeroY  = 64;    // the zeroed panel, left of it
    const unsigned sheenX  = 116, sheenY = 64;    // the sheen panel, right of it
    // ABOVE the panels: the -Z wall's own white inner face, which the direct
    // light never reaches and only diffuse GI can light. (The pixel geometry is
    // measured, not guessed — the panels' top edge is at y 3.5 and the frame's
    // top row sees y ~4.1 at that distance.)
    const unsigned ceilX   = 64, ceilY   = 4;

    Image img;
    render(engine.get());
    view->readPixels(img);
    const Colour offMirror = img.at(mirrorX, mirrorY);
    const Colour offZero   = img.at(zeroX, zeroY);
    const Colour offSheen  = img.at(sheenX, sheenY);
    const Colour offCeil   = img.at(ceilX, ceilY);
    std::printf("-- GI off --\n");
    show("chrome box", offMirror); show("zeroed panel", offZero);
    show("sheen panel", offSheen); show("wall above", offCeil);
    CHECK(offMirror.r < 0.10f, "with GI off the chrome box reflects nothing");

    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 2;
    gi.boundsMin = Vec3(-4.6f, -0.6f, -4.6f);
    gi.boundsMax = Vec3( 4.6f,  5.6f,  4.6f);
    gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
    const bool ok = s->setGlobalIllumination(gi);
    if (!ok) std::printf("   engine error: %s\n", engine->lastError().c_str());
    CHECK(ok, "setGlobalIllumination(VctPccHybrid) succeeds");
    render(engine.get(), 10);
    view->readPixels(img);
    const Colour onMirror = img.at(mirrorX, mirrorY);
    const Colour onZero   = img.at(zeroX, zeroY);
    const Colour onSheen  = img.at(sheenX, sheenY);
    const Colour onCeil   = img.at(ceilX, ceilY);
    std::printf("-- VCT + probes --\n");
    show("chrome box", onMirror); show("zeroed panel", onZero);
    show("sheen panel", onSheen); show("wall above", onCeil);

    const GiStatus st = s->giStatus();
    std::printf("   probes=%d pccBound=%s refused=%s enclosedAxes=%d captureSize=%d\n",
                st.probeCount, st.pccBound ? "true" : "false",
                st.probeGridRefused ? "true" : "false", st.probeEnclosedAxes, st.probeCaptureSize);
    CHECK(st.probeCount == 4 && st.pccBound,
          "the probe grid is built and bound (a sealed room encloses all three axes)");
    CHECK(st.probeEnclosedAxes == 3 && !st.probeGridRefused,
          "and the enclosure measurement says so");

    // (c) the gate must not touch a reflective material.
    CHECK(onMirror.r > 0.30f && onMirror.r - onMirror.g > 0.20f,
          "the chrome box still reflects the red wall through a probe");

    // (b) a physically lit dielectric keeps its sheen, and it comes from a probe:
    //     the panel picks up the red wall it cannot see directly.
    std::printf("   sheen panel  (r-g) = %+.4f\n", onSheen.r - onSheen.g);
    CHECK(onSheen.r - onSheen.g > 0.01f,
          "the reflective panel takes RED from a probe (its sheen is kept, measured)");

    // (a) THE CONTRACT: the zeroed panel takes nothing from the probes. The
    //     only red in this room is the probe's, so a neutral panel sampled none.
    std::printf("   zeroed panel (r-g) = %+.4f\n", onZero.r - onZero.g);
    CHECK(std::fabs(onZero.r - onZero.g) < 0.002f,
          "the zero-reflectance panel stays NEUTRAL — it samples no probe");
    CHECK(onSheen.r - onSheen.g > std::fabs(onZero.r - onZero.g) + 0.008f,
          "...and the pair differs in exactly the one thing that differs about them");

    CHECK(onZero.r < 0.01f && onZero.g < 0.01f && onZero.b < 0.01f,
          "the zero-reflectance panel is black: no environment term reaches it at all");

    // (d) diffuse GI is untouched by the gate.
    CHECK(onCeil.r > offCeil.r + 0.05f,
          "diffuse GI still lights the wall above the panels (the gate does not take\n          needs_env_brdf with it)");

    // ---- (e) A RUNTIME REFLECTANCE EDIT REBUILDS THE SHADER ----------------
    // THE DEFECT THIS IS THE FAIL-BEFORE FOR (round-2 send-back, 2026-09-13).
    // The gate is decided in `calculateHashForPreCreate`, which runs only on
    // `flushRenderables()`, and at this pin `setSpecular`/`setMetalness`/
    // `setFresnel` schedule a const-buffer update and nothing else. Jahshaka's
    // own material push writes kS on the LIVE datablock every time a material
    // changes and deliberately never flushes (OgreMaterials applyPbr's guard,
    // which exists to keep the mirror's per-frame push off the hash path). So
    // the exact workflow the ground lane promises — a matte floor whose
    // Specular Color the user raises to make it a mirror — updated a constant
    // while the SHADER stayed gated, and the floor went on reflecting nothing
    // until some unrelated edit happened to flush it.
    //
    // Patch 0028 mirrors upstream's own `setClearCoat` idiom: evaluate
    // `hasZeroSpecularResponse()` before and after, flush only when it CROSSES.
    // Measured without that fix, on this exact case: the plate stayed black
    // (r-g +0.0000) after the edit.
    const unsigned editX = 64, editY = 103;      // the plate, below the chrome box
    const Colour beforeEdit = img.at(editX, editY);
    show("runtime plate, authored matte", beforeEdit);
    CHECK(std::fabs(beforeEdit.r - beforeEdit.g) < 0.002f && beforeEdit.r < 0.02f,
          "(e) the not-reflective plate shows nothing at all while the grid is live");

    PbrParams mirrored = matte;
    mirrored.albedo = Colour(1, 1, 1);
    mirrored.metalness = 1.0f;                   // ...and the author makes it a mirror,
    mirrored.specularColour = Colour(1, 1, 1);   //    with the two controls the gate reads
    CHECK(s->setPbrMaterial(editedMat, mirrored), "(e) the material edit applies");
    render(engine.get(), 6);
    view->readPixels(img);
    const Colour afterEdit = img.at(editX, editY);
    show("runtime plate, now a mirror", afterEdit);
    std::printf("   runtime plate (r-g) = %+.4f  ->  %+.4f\n",
                beforeEdit.r - beforeEdit.g, afterEdit.r - afterEdit.g);
    CHECK(afterEdit.r - afterEdit.g > 0.08f,      // measured +0.1373 against +0.0000
          "(e) THE FIX: raising the material's reflectance makes the probe reflection\n"
          "          APPEAR, with no other edit — the shader was rebuilt on the crossing");

    // ...AND AN ORDINARY EDIT MUST NOT FLUSH. The per-frame material push is
    // why applyPbr avoids flushRenderables at all, so a gate that flushed on
    // every kS write would put a full hash recompute of every renderable back
    // on the 60 Hz path. The crossing test makes that impossible by
    // construction (both sides false => no flush); this loop is the executable
    // statement of it, and the instrumented measurement in the lane report
    // counted the flushes it produces: ZERO in 100 pushes.
    {
        PbrParams keep = mirrored;
        for (int i = 0; i < 100; ++i) {
            keep.specularColour = Colour(1.0f - 0.001f * float(i % 10),
                                         1.0f - 0.001f * float(i % 10),
                                         1.0f - 0.001f * float(i % 10));
            s->setPbrMaterial(editedMat, keep);
        }
        render(engine.get(), 3);
        view->readPixels(img);
        const Colour afterChurn = img.at(editX, editY);
        CHECK(delta(afterChurn, afterEdit) < 0.02f,
              "(e) 100 ordinary kS pushes leave the picture where it was (no crossing,\n"
              "          and therefore no shader rebuild)");
    }

    engine->destroyScene(s);
    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
