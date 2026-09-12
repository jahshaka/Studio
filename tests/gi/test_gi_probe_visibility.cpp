// gi.probe_visibility — CHARACTERISATION: a surface takes a probe's picture
// even when it is not IN that picture (the owner's Q2 leak, measured).
//
// STATUS, stated first because it is the point of this file: the FIX exists and
// is DEFERRED. `irisgl/thirdparty/ogre-patches/
// 0029-pcc-probe-visibility-from-captured-depth.patch.DEFERRED` implements the
// owner's rule — a shading point takes a probe's contribution only if the probe
// could SEE it, tested against the depth the probe itself captured — and this
// scene proves it works: with the patch applied the mirror below reads
// r 0.000 instead of r 1.000. It is NOT in the stack because it also crushed a
// LEGITIMATE reflection in gi.budget (a mirror in a sealed 2x1x2 room went to
// r 0.004, with the probe reporting a captured depth of ~10% of the distance to
// the shading point in that direction — root cause not established). Shipping
// "the mirror goes black in a sealed room" to fix "the roof shows the room" is
// the wrong trade, so the patch waits for that diagnosis.
//
// This suite therefore asserts WHAT THE RENDERER DOES TODAY, loudly, so that
// (a) the defect is executable rather than anecdotal, and (b) whoever lands the
// fix has a scene that flips in one obvious place. Flip the two assertions at
// the bottom when the patch goes into the stack.
//
// THE SYNTHETIC (never the Showroom — not open-to-open deterministic, MESH_BAKE
// facts). A sealed room split by a full-height partition:
//
//        -Z                                        +Z
//   +---------------------------+-+-------------------+
//   | R E D   wall              | |     C -->  M      |
//   |              P            |W|                   |
//   +---------------------------+-+-------------------+
//     P = the single probe        partition at z = +1
//     C = the camera, looking +Z at M, a roughness-0 metal box
//
// The camera looks AWAY from the red wall at a mirror, so the mirror's
// reflection ray runs back along -Z, straight through the partition and into
// the red half. The ray's DIRECTION is one the probe photographed in full red,
// and the surface reflecting it is one the probe cannot see at all.
// Geometrically nothing the camera can see is red; through an unguarded probe
// lookup, the mirror is.
//
// The probe grid is 1x1x1, so the single probe stands at the region centre — in
// the RED half — and its cube's -Z face IS the red wall. Its parallax box is
// forced out to the whole region by the snap tolerances
// (`probeSnapSidesMin/Max`, GiParams), which is what the shipped rooms do to
// themselves anyway: the shrink-fit reads ONE averaged depth per cube face and
// overshoots as soon as anything stands between a probe and a wall (measured on
// the Grand Showroom: boxes reaching +-23.68 in a room spanning +-12.25). So
// the cheap box test (`getProbeFade > 0`) admits the metal box in the sealed
// half, and nothing else in this pin asks whether the probe could see it.
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
    std::printf("   %-34s r=%.4f g=%.4f b=%.4f   (r-g)=%+.4f\n", what, c.r, c.g, c.b, c.r - c.g);
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
    cfg.logFile = "test-gi-probe-visibility-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("vis", 128, 128, Colour(0, 0, 0));
    Scene *s = engine->createScene("vis");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    const Colour white(0.85f, 0.85f, 0.85f);
    const Colour dark(0.06f, 0.06f, 0.06f);
    const Colour red(1.0f, 0.02f, 0.02f);
    // Shell: interior x in [-4,4], y in [0,5], z in [-4,4].
    addSlab(s, white, Vec3(0.0f, -0.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f));   // floor
    addSlab(s, white, Vec3(0.0f,  5.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f));   // ceiling
    addSlab(s, red,   Vec3(0.0f,  2.5f, -4.2f), Vec3(8.8f, 5.0f, 0.4f));  // -Z wall: THE red one
    addSlab(s, dark,  Vec3(0.0f,  2.5f,  4.2f), Vec3(8.8f, 5.0f, 0.4f));  // +Z wall (behind camera)
    addSlab(s, white, Vec3(-4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));   // -X wall
    addSlab(s, white, Vec3( 4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));   // +X wall
    // THE PARTITION: full height, full width, at z = +1. Dark, so any red in
    // the camera's half can only have arrived through a probe.
    addSlab(s, dark, Vec3(0.0f, 2.5f, 1.0f), Vec3(8.0f, 5.0f, 0.5f));

    // The witness: a roughness-0 metal box in the SEALED half.
    PbrParams mirrorP; mirrorP.albedo = Colour(1, 1, 1); mirrorP.metalness = 1.0f; mirrorP.roughness = 0.0f;
    const NodeId mirror = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId mat = s->createPbrMaterial(mirrorP);
    CHECK(mirror && mesh && mat && s->attachMesh(mirror, mesh, mat), "the metal box attaches");
    s->setNodeTransform(mirror, Vec3(0.0f, 2.2f, 3.1f), Quat(), Vec3(1.3f, 1.3f, 1.3f));

    // A directional light aimed at the RED wall's inner face, as in
    // gi.pcc_mirror: it lights the red wall nearly head-on and nothing in the
    // sealed half. Shadows off, so the partition does not block the direct pass
    // — which makes the test HARDER, not easier: the red wall is fully lit in
    // the probe's capture.
    CHECK(enginetest::addDirectionalLight(s, Vec3(0.0f, -0.12f, -0.993f), 6.0f) != 0,
          "directional light created");
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.2f, 1.6f), Vec3(0.0f, 2.2f, 3.1f));
    const unsigned mirrorX = 64, mirrorY = 64;

    Image img;
    render(engine.get());
    view->readPixels(img);
    const Colour off = img.at(mirrorX, mirrorY);
    show("metal box, GI off", off);
    CHECK(off.r < 0.10f && off.g < 0.10f, "with GI off the metal reflects nothing");

    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 2;
    gi.boundsMin = Vec3(-4.6f, -0.6f, -4.6f);
    gi.boundsMax = Vec3( 4.6f,  5.6f,  4.6f);
    gi.pccProbesX = 1; gi.pccProbesY = 1; gi.pccProbesZ = 1;   // ONE probe, in the red half
    // Force the fitted parallax box back out to the whole region, so the cheap
    // box test cannot be what excludes the sealed half (see the header).
    gi.probeSnapDeviation = 4.0f;
    gi.probeSnapSidesMin  = 4.0f;
    gi.probeSnapSidesMax  = 4.0f;
    const bool ok = s->setGlobalIllumination(gi);
    if (!ok) std::printf("   engine error: %s\n", engine->lastError().c_str());
    CHECK(ok, "setGlobalIllumination(VctPccHybrid) succeeds");
    render(engine.get(), 12);
    view->readPixels(img);
    const Colour on = img.at(mirrorX, mirrorY);
    show("metal box, VCT + probes", on);

    const GiStatus st = s->giStatus();
    std::printf("   probes=%d pccBound=%s shape %.2f..%.2f (z)  region %.2f..%.2f (z)\n",
                st.probeCount, st.pccBound ? "true" : "false",
                st.probeShapeMin.z, st.probeShapeMax.z,
                st.probeRegionMin.z, st.probeRegionMax.z);
    CHECK(st.probeCount == 1 && st.pccBound, "exactly one probe, bound");
    // The box test really does admit the sealed half: the probe's parallax
    // shape reaches past the partition. If this ever stops being true the
    // suite is no longer testing what it says it tests.
    CHECK(st.probeShapeMax.z > 3.8f,
          "the probe's parallax box reaches past the partition (so getProbeFade admits the box)");

    // THE CHARACTERISATION. The camera's half contains nothing red; the probe's
    // photograph does. The red on this metal is a picture of a place this
    // surface cannot see — the owner's "why would the outside of a building
    // show the room inside?" in its smallest reproducible form.
    //
    // WHEN PATCH 0029 LANDS these two flip to `< 0.06f` and `< 0.20f`; the
    // measured post-patch reading is r 0.000 g 0.000 b 0.000.
    CHECK(on.r - on.g > 0.50f,
          "TODAY: the metal box in the sealed half is painted with the red wall it cannot see "
          "(the leak; ogre-patch 0029.DEFERRED removes it — see this file's header)");
    CHECK(on.r > 0.50f, "TODAY: and it is bright, not a faint bleed");

    engine->destroyScene(s);
    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
