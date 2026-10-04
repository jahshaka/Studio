// gi.cone_skip — THE PIXEL'S VOXEL CONES RUN ONLY WHERE THEIR ANSWER IS READ (SPEED-GPU,
// perf audit 2026-10-03 PH-2 and PH-1 / S1).
//
// WHAT CHANGED. Two estimators of the lit pixel were computed everywhere and then
// multiplied by zero over most of the screen:
//   PH-2  the four-cone DIFFUSE under a cascade chain with an irradiance field is
//         blended in by the field's fallback weight (1 - confidence), and that weight
//         is exactly 0 inside the field. The weight is now known before the march
//         (JahIfd_piece_ps.any's post hook marches the cones) and the cones run only
//         where it is above zero.
//   PH-1  the SPECULAR cone feeds the environment term the fork's SSR composite
//         replaces by the screen march's / the ray tier's reflection weighted by its
//         confidence w; at w = 1 it is replaced whole. The cone now reads w first and
//         marches only where w < 1 (PhotonVct_piece_ps.any).
//
// WHAT THIS SUITE ASSERTS: each skip is EXACT. Every arm is a measurement switch
// (engine.arm: photon.diffuseConeSkip / photon.specularConeSkip; 0 = march everywhere,
// the cost before the lane), so one process renders the same still frame with the skip
// on and off and compares the HDR readback FLOAT FOR FLOAT. Each comparison is guarded
// by a repeat of the first arm (a frame that is not still would prove nothing), and by
// a negative control that shows the subject is on screen (the field's ring for PH-2,
// the traced reflection for PH-1).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstring>
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
        char buf_[512];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kW = 256, kH = 144;

static void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

/// How many float components differ (bit for bit) between two readbacks.
static size_t floatsDiffering(const ImageF &a, const ImageF &b)
{
    if (a.rgba.empty() || b.rgba.empty()) return size_t(-1);   // nothing read is no proof
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size()) return size_t(-1);
    size_t n = 0;
    for (size_t i = 0; i < a.rgba.size(); ++i)
        if (std::memcmp(&a.rgba[i], &b.rgba[i], sizeof(float)) != 0) ++n;
    return n;
}

static float worstAbs(const ImageF &a, const ImageF &b)
{
    float w = 0.0f;
    for (size_t i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i) {
        const float d = std::fabs(a.rgba[i] - b.rgba[i]);
        if (d > w) w = d;
    }
    return w;
}

/// The arm, then enough frames for every history the frame keeps to come back to the
/// same still state, then the readback.
static bool shot(Engine *e, View *v, const char *arm, double value, int settle, ImageF &out)
{
    if (!e->setArm(arm, value)) return false;
    render(e, settle);
    return v->readPixelsHdr(out);
}

/// A floor that runs far past cascade 0's field (the ring the cones answer), walls and
/// columns standing on it inside the field, a sun.
static void buildWorld(Scene *s, float floorMetal, float floorRough, bool sunShadows = true)
{
    s->setAmbient(Colour(0.25f, 0.27f, 0.30f), Colour(0.20f, 0.18f, 0.15f));
    const NodeId floor = enginetest::addTestCube(s, Colour(0.70f, 0.68f, 0.64f), floorMetal, floorRough);
    enginetest::setNodeScale(s, floor, Vec3(80.0f, 0.2f, 80.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    const NodeId wall = enginetest::addTestCube(s, Colour(0.80f, 0.20f, 0.15f), 0.0f, 0.8f);
    enginetest::setNodeScale(s, wall, Vec3(6.0f, 3.0f, 0.3f));
    enginetest::setNodePosition(s, wall, Vec3(-1.0f, 1.5f, 4.0f));
    for (int i = 0; i < 6; ++i) {
        const NodeId col = enginetest::addTestCube(s, Colour(0.2f, 0.5f, 0.8f), 0.0f, 0.6f);
        enginetest::setNodeScale(s, col, Vec3(0.6f, 2.5f, 0.6f));
        enginetest::setNodePosition(s, col, Vec3(i % 2 ? 2.5f : -2.5f, 1.25f, 2.0f + 5.0f * float(i)));
    }
    const NodeId sun = enginetest::addDirectionalLight(s, Vec3(-0.4f, -1.0f, 0.5f), 2.0f);
    if (!sunShadows) {
        // the traced sun contact samples a new pattern every frame: no shadow, no per-frame term
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.f, 1.f, 1.f);
        l.intensity = 2.0f / 3.14159265358979323846f;
        l.castShadows = false;
        s->setLight(sun, l);
    }
}

int main()
{
    std::printf("== gi.cone_skip: the pixel's voxel cones run only where their answer is read, "
                "and the picture is the same floats\n");
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-cone-skip-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    // ---- PH-2: the cone diffuse under a chain with a field (Low: no gather) ----------
    {
        std::printf("-- PH-2: Low, a chain with a field, no gather\n");
        View *view = e->createOffscreenView("skip-diffuse", kW, kH, Colour(0, 0, 0));
        Scene *s = e->createScene("skip-diffuse");
        view->setScene(s);
        buildWorld(s, 0.0f, 0.9f);
        e->setRayTracing(false);
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::Low;
        gi.numBounces = 1;
        gi.ddgi = GiToggle::On;
        gi.gather = GiToggle::Off;
        gi.updateBudget = 0;           // a PAUSED field: converged at the build, then still
        CHECK(s->setGlobalIllumination(gi), "the Low chain with its field builds");
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdrReadback = true;         // the scene radiance, float for float
        view->setPostFx(fx);
        // Low and looking down the floor: the near floor and the wall inside cascade 0's
        // field, the far floor in the ring outside it.
        enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, -1.5f), Vec3(0.0f, 0.4f, 30.0f));
        render(e, 90);
        const GiStatus st = s->giStatus();
        CHECK_MSG(st.ifdBound && st.cascades.size() > 1, "the field is bound under a chain (%zu cascades)", st.cascades.size());

        ImageF on, off, on2, noField;
        CHECK(shot(e, view, "photon.diffuseConeSkip", 1.0, 8, on), "skip ON rendered");
        CHECK(shot(e, view, "photon.diffuseConeSkip", 0.0, 8, off), "skip OFF (marched everywhere) rendered");
        CHECK(shot(e, view, "photon.diffuseConeSkip", 1.0, 8, on2), "skip ON again rendered");
        const size_t still = floatsDiffering(on, on2);
        CHECK_MSG(still == 0, "the frame is still: skip ON twice is the same floats (%zu differ)", still);
        const size_t d = floatsDiffering(on, off);
        CHECK_MSG(d == 0, "PH-2 IS EXACT: skip ON and OFF are the same floats (%zu of %zu differ, worst %.3g)",
                  d, on.rgba.size(), worstAbs(on, off));
        // the negative control: the field is on screen (taking it away moves the picture)
        GiParams nf = gi;
        nf.ddgi = GiToggle::Off;
        s->setGlobalIllumination(nf);
        render(e, 90);
        view->readPixelsHdr(noField);
        const size_t fieldPixels = floatsDiffering(on, noField);
        CHECK_MSG(fieldPixels > on.rgba.size() / 10,
                  "the control: the field lights this frame (%zu floats move without it)", fieldPixels);
        e->destroyView(view);
        e->destroyScene(s);
    }

    // ---- PH-1: the specular cone under the ray tier's reflection (Epic rays) ----------
    {
        std::printf("-- PH-1: High, rays, a near-mirror floor\n");
        e->setRayTracing(true);
        View *view = e->createOffscreenView("skip-specular", kW, kH, Colour(0, 0, 0));
        Scene *s = e->createScene("skip-specular");
        view->setScene(s);
        // A MIRROR (roughness 0): one ray is the whole lobe, so the trace's answer is the same
        // every frame and a still frame is a fixed point the arms can be compared at.
        buildWorld(s, 1.0f, 0.0f, false);
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.numBounces = 1;
        gi.ddgi = GiToggle::On;
        gi.gather = GiToggle::Off;     // the gather's history is the other per-frame estimator
        gi.updateBudget = 0;
        CHECK(s->setGlobalIllumination(gi), "the High chain builds");
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 2;                    // full-resolution rays
        fx.hdrReadback = true;
        view->setPostFx(fx);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, -1.5f), Vec3(0.0f, 0.4f, 30.0f));
        render(e, 120);
        const RayQueryStatus rq = s->rayQueryStatus();
        if (!rq.available) {
            std::printf("SKIP: no ray-query device; PH-1's ray half cannot run here\n");
        } else {
            CHECK(rq.reflect, "the view traces its reflections");
            ImageF on, off, reflOn, reflOff;
            CHECK(shot(e, view, "photon.specularConeSkip", 1.0, 30, on) && view->readReflectionHdr(reflOn),
                  "skip ON rendered, its reflection read");
            CHECK(shot(e, view, "photon.specularConeSkip", 0.0, 30, off) && view->readReflectionHdr(reflOff),
                  "skip OFF rendered, its reflection read");
            // THE TRACE'S HISTORY IS NOT A FIXED POINT EVERYWHERE: on this fixture ~1 % of the
            // answered pixels (the floor nearest the camera) move between frames of the SAME arm by an
            // RGBA16F rounding of the temporal mean, so a whole-frame comparison cannot prove the skip.
            // The proof is CONDITIONAL instead, and exact: where the reflection answered whole (w = 1)
            // the composite replaces the environment term by the reflection, so a pixel whose
            // reflection texel holds the SAME bits in the two frames must shade to the SAME floats
            // with the skip on and off. (The screen march alone never answers a pixel whole - its
            // weight carries a distance fade - so the ray tier is the skip's whole territory.)
            size_t answered = 0, sameInput = 0, moved = 0;
            for (size_t i = 0; i + 3 < on.rgba.size() && i + 3 < reflOn.rgba.size() && i + 3 < reflOff.rgba.size(); i += 4) {
                if (!( reflOn.rgba[i + 3] >= 1.0f && reflOff.rgba[i + 3] >= 1.0f)) continue;
                ++answered;
                if (std::memcmp(&reflOn.rgba[i], &reflOff.rgba[i], 4 * sizeof(float)) != 0) continue;
                ++sameInput;
                if (std::memcmp(&on.rgba[i], &off.rgba[i], 4 * sizeof(float)) != 0) ++moved;
            }
            CHECK_MSG(answered > (on.rgba.size() / 4u) / 10u && sameInput > answered * 9u / 10u,
                      "the trace answers %zu pixels whole, %zu with the same reflection in both frames",
                      answered, sameInput);
            CHECK_MSG(moved == 0, "PH-1 IS EXACT: of those %zu pixels, %zu shade differently with the skip ON and OFF",
                      sameInput, moved);
        }
        e->destroyView(view);
        e->destroyScene(s);
    }

    // ---- PH-1's exception: a CLEAR COAT under parallax-corrected probes ---------------
    // There the cone's hit position also weighs the coat's own probe term, which the SSR
    // composite does not replace, so the skip is compiled out for that material
    // (PhotonVct_piece_ps.any). A coated mirror floor and a plain glossy column under a
    // probe grid and the screen march: the arm on and off are the same floats.
    {
        std::printf("-- PH-1 exception: a clear-coated floor under a probe grid, the screen march\n");
        e->setRayTracing(false);
        View *view = e->createOffscreenView("skip-coat", kW, kH, Colour(0, 0, 0));
        Scene *s = e->createScene("skip-coat");
        view->setScene(s);
        // gi.leak_room's shell (the probes keep only what they see enclosed), a coated slab on
        // its floor and a mirror column, the camera inside.
        enginetest::leakroom::build(s, view, 0.3f);
        s->setAmbient(Colour(0.25f, 0.27f, 0.30f), Colour(0.20f, 0.18f, 0.15f));
        const NodeId floor = s->createNode();
        {
            PbrParams p;
            p.albedo = Colour(0.30f, 0.05f, 0.05f);
            p.roughness = 0.6f;
            p.clearCoat = 1.0f;
            p.clearCoatRoughness = 0.02f;
            const MaterialId mat = s->createPbrMaterial(p);
            const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
            CHECK(floor && mat && mesh && s->attachMesh(floor, mesh, mat), "the coated slab exists");
        }
        s->setNodeTransform(floor, Vec3(0.0f, 0.02f, 0.0f), Quat(), Vec3(9.0f, 0.04f, 9.0f));
        const NodeId col = enginetest::addTestCube(s, Colour(0.9f, 0.9f, 0.9f), 1.0f, 0.0f);
        s->setNodeTransform(col, Vec3(0.0f, 1.25f, 2.5f), Quat(), Vec3(1.0f, 2.5f, 1.0f));
        GiParams gi;
        gi.mode = GiMode::VctPccHybrid;
        gi.quality = GiQuality::Medium;
        gi.numBounces = 1;
        gi.updateBudget = 1;           // the probes capture, then the stale set is empty
        gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
        CHECK(s->setGlobalIllumination(gi), "the hybrid arm builds");
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;                    // the screen march (no rays bound)
        fx.hdrReadback = true;
        view->setPostFx(fx);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, -3.5f), Vec3(0.0f, 0.4f, 4.0f));
        render(e, 160);
        const GiStatus pst = s->giStatus();
        CHECK_MSG(pst.pccBound && pst.probeCount > 0,
                  "the probe grid is bound (%d probes, by rays %d, dropped %d, stale %d)", pst.probeCount,
                  int(pst.probeGridByRays), pst.probesDropped, pst.staleProbes);
        ImageF on, off, on2;
        CHECK(shot(e, view, "photon.specularConeSkip", 1.0, 30, on), "skip ON rendered");
        CHECK(shot(e, view, "photon.specularConeSkip", 0.0, 30, off), "skip OFF rendered");
        CHECK(shot(e, view, "photon.specularConeSkip", 1.0, 30, on2), "skip ON again rendered");
        const size_t still = floatsDiffering(on, on2);
        CHECK_MSG(still == 0, "the frame is still (%zu floats differ)", still);
        const size_t d = floatsDiffering(on, off);
        CHECK_MSG(d == 0, "THE COAT IS UNTOUCHED: skip ON and OFF are the same floats (%zu of %zu differ, worst %.3g)",
                  d, on.rgba.size(), worstAbs(on, off));
        e->destroyView(view);
        e->destroyScene(s);
    }

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
