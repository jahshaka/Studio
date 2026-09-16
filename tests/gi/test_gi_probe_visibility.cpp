// gi.probe_visibility — A SURFACE TAKES A PROBE'S PICTURE ONLY IF IT IS IN
// THAT PICTURE (owner, 2026-09-13, REFLECTION_PROBE_AUDIT Q2: "the engine
// should not know if there is a room? Isn't it the layout of objects in a scene
// that matters?").
//
// The rule, and both of its halves, in one file:
//   PHASE 1 (the leak)   a reflective box sealed off from a probe by a
//                        partition must NOT be painted with what that probe
//                        photographed on the other side of it.
//   PHASE 2 (the mirror) a reflective box the probe CAN see must keep its
//                        reflection — the failure mode a naive tightening of
//                        probe influence produces, and the one that parked this
//                        work for a day.
//
// Shipped by ogre-patch 0029 (the visibility test itself: march from the probe
// camera towards the shaded point, read the depth the probe recorded that way,
// and treat "it saw something nearer" as occlusion) standing on ogre-patch 0030
// (the depth it reads is the TRUE depth: the DepthCompressor multiplied its
// view->probe-local matrix on the wrong side for GLSL/Vulkan, so every
// off-centre probe's X and Y depths were encoded against the MIRRORED
// direction). 0029 without 0030 is exactly the parked failure: phase 1 green,
// phase 2 black.
//
// Measured on this suite's two scenes (RTX 4080S, Debug+ASan, 2026-09-13):
//     phase 1 metal box   before 0029: r 1.000 g 0.055   after: r 0.000
//     phase 2 metal box   with 0030:   r 1.000 g 0.055    without: r 0.000
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
    gi.testBoundsMin = Vec3(-4.6f, -0.6f, -4.6f);
    gi.testBoundsMax = Vec3( 4.6f,  5.6f,  4.6f);
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

    // THE LEAK IS CLOSED. The camera's half contains nothing red; the probe's
    // photograph does. Red on this metal would be a picture of a place this
    // surface cannot see — the owner's "why would the outside of a building
    // show the room inside?" in its smallest reproducible form. It read
    // r 1.000 g 0.055 before ogre-patch 0029 and reads r 0.000 after it.
    CHECK(on.r - on.g < 0.06f,
          "the metal box in the sealed half is NOT painted with the red wall it cannot see "
          "(the leak ogre-patch 0029 closes — it read r 1.000 g 0.055 before)");
    CHECK(on.r < 0.20f, "...and there is no faint bleed of it either");

    engine->destroyScene(s);

    // =====================================================================
    // THE OTHER HALF OF THE RULE, and the fence on the DEPTH ITSELF: a
    // surface the probe CAN see keeps its reflection.
    //
    // The visibility test is only as good as the distance the probe recorded,
    // and that distance was WRONG on the X and Y axes for every off-centre
    // probe until ogre-patch 0030: the DepthCompressor multiplied the
    // view->probe-local matrix on the wrong side for GLSL/Vulkan, so a texel's
    // fApproxDist was computed for the MIRRORED direction. Measured here, in
    // this room, with the probe cube read back texel by texel: probe 0 recorded
    // the mirror 2.256 m away at 1.058 m (and probe 1 put the same surface, 12.21 m
    // off, at 31 m -- its encoding saturated). A depth that short reads as "the probe cannot see
    // this" for everything past a metre, which is exactly how a legitimate
    // mirror in a sealed room goes black.
    //
    // THE SCENE: a LONG sealed room with a 2x1x1 grid, so both probes stand
    // well off the centre of their own parallax box (the case the bug needs),
    // and a roughness-0 metal box at the far -X end, 2.2 m from the probe that
    // owns that end. It faces the camera, so its reflection ray runs back +X
    // into the only saturated thing in the room -- the red wall. Nothing stands
    // between the probe and the box: the honest answer is "visible", and the
    // pixel says so.
    //     ogre-patch 0030 applied:      r 1.000, probe 0 reads 2.275 m (101%)
    //     ogre-patch 0030 reverted:     r 0.000, probe 0 reads 1.058 m (47%)
    Scene *s2 = engine->createScene("vis2");
    view->setScene(s2);
    s2->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    // Interior: x in [-10,10], y in [0,5], z in [-4,4].
    addSlab(s2, white, Vec3(0.0f, -0.2f, 0.0f), Vec3(20.8f, 0.4f, 8.8f));   // floor
    addSlab(s2, white, Vec3(0.0f,  5.2f, 0.0f), Vec3(20.8f, 0.4f, 8.8f));   // ceiling
    addSlab(s2, dark,  Vec3(0.0f,  2.5f, -4.2f), Vec3(20.8f, 5.0f, 0.4f));  // -Z wall
    addSlab(s2, dark,  Vec3(0.0f,  2.5f,  4.2f), Vec3(20.8f, 5.0f, 0.4f));  // +Z wall
    addSlab(s2, dark,  Vec3(-10.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));   // -X wall
    addSlab(s2, red,   Vec3( 10.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));   // +X: THE red one
    const NodeId mirror2 = s2->createNode();
    const MeshId mesh2 = s2->createMesh(enginetest::unitCubeMesh());
    const MaterialId mat2 = s2->createPbrMaterial(mirrorP);
    CHECK(mirror2 && mesh2 && mat2 && s2->attachMesh(mirror2, mesh2, mat2),
          "long room: the metal box attaches");
    s2->setNodeTransform(mirror2, Vec3(-8.0f, 2.0f, 0.0f), Quat(), Vec3(1.6f, 1.6f, 1.6f));
    // Travelling +X, so it lights the red wall's inner face head-on.
    CHECK(enginetest::addDirectionalLight(s2, Vec3(0.993f, -0.12f, 0.0f), 6.0f) != 0,
          "long room: directional light created");
    enginetest::testCameraLookAt(view, Vec3(-4.0f, 2.0f, 0.0f), Vec3(-8.0f, 2.0f, 0.0f));

    GiParams gi2;
    gi2.mode = GiMode::VctPccHybrid;
    gi2.quality = GiQuality::Medium;
    gi2.numBounces = 2;
    gi2.testBoundsMin = Vec3(-10.6f, -0.6f, -4.6f);
    gi2.testBoundsMax = Vec3( 10.6f,  5.6f,  4.6f);
    gi2.pccProbesX = 2; gi2.pccProbesY = 1; gi2.pccProbesZ = 1;   // both probes off-centre in X
    const bool ok2 = s2->setGlobalIllumination(gi2);
    if (!ok2) std::printf("   engine error: %s\n", engine->lastError().c_str());
    CHECK(ok2, "long room: setGlobalIllumination(VctPccHybrid) succeeds");
    render(engine.get(), 12);
    view->readPixels(img);
    const Colour lit = img.at(mirrorX, mirrorY);
    show("long room: metal box, VCT + probes", lit);
    const GiStatus st2 = s2->giStatus();
    std::printf("   probes=%d pccBound=%s clamped=%d\n", st2.probeCount,
                st2.pccBound ? "true" : "false", st2.probesClampedToRegion);
    CHECK(st2.probeCount == 2 && st2.pccBound, "long room: the 2x1x1 grid built and bound");
    CHECK(lit.r > 0.50f && lit.r - lit.g > 0.40f,
          "long room: the metal box the probe CAN see still reflects the red wall "
          "(it reads 0.000 on a tree whose probes record the wrong depth -- ogre-patch 0030)");
    engine->destroyScene(s2);

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
