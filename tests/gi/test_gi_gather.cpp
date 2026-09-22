// gi.gather — THE SCREEN-PROBE GATHER, phase 1
// (SPECS/SCREEN_PROBE_GATHER_SPEC.md section 7 phase 1; the brief
// SPECS/briefs/GATHER-1a.md).
//
// WHAT IT ASSERTS, and why it is these things and not smoothness: at phase 1
// there is no filter and no temporal accumulation, so the picture is BLOCKY at
// the probe stride and NOISY at 64 rays BY DESIGN. What can be asserted
// honestly is that the diffuse answer really comes from the rays, that the leak
// through a thin wall is measured against the irradiance field's own bars in
// the SAME process at the SAME pose, that the adaptive pass appends probes
// where geometry needs them and none where it does not, that a still frame is
// byte-deterministic, and that a machine without rays draws exactly what it
// drew before.
//
// THE MAGNITUDE is gi.gather_reference's question, not this suite's: a
// rectangular Lambertian emitter over a plane has a closed-form irradiance, and
// that is the first measurement of any of this renderer's diffuse-GI estimators
// against a reference.
//
// THREE ENTRIES, ONE BINARY (the gi.rt_reflect shape):
//   gi.gather          — the leak room, the picture, the adaptive pass,
//                        determinism, the tier-teardown case
//   gi.gather_cost     — the GPU milliseconds at 1080p on a dense scene
//   gi.gather_norays   — the fallback: the row does nothing and the picture is
//                        the one a no-ray machine already draws
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

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

static void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

/// The offscreen chain the gather needs. The prepass — the depth and normals
/// every probe's surface comes from — exists ONLY with the SSR row on at the
/// pin (OgreChain.cpp), so phase 0 measures under it; phase 1's
/// `ChainDesc::probeGather` is what frees the gather from that.
static void armChain(View *view, int ssrRow = 2)
{
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = ssrRow;
    view->setPostFx(fx);
}

/// THE ROW AND THE KNOBS. The row is `GiParams::gather` and travels with the
/// rest of the GI configuration (a GI push, because the gather changes the
/// CHAIN as well as the shader); the knobs are the test-and-tool door, and
/// every zero in them means "what the tier derives".
///
/// `gi` is passed by value on purpose: every arm of this suite states the whole
/// GI configuration it is measuring, so no arm can inherit a row from the one
/// before it.
///
/// THE FRAME INDEX IS FROZEN BY DEFAULT (PHOTON-GAFAR-1). The gather's sample
/// sequence is keyed on the frame index, so two LIVE frames are two draws of a
/// stochastic estimator: at 64 rays and no temporal filter they differ on about
/// half the pixels (the naked-noise print below). Every A/B of this suite
/// differences two pictures, and a difference of two live frames measures that
/// noise and nothing else — the far-term A/B did exactly that and its "53 %"
/// was the suite's own 54 % noise (spikes/measure-1bd/FINDINGS.md). So every
/// arm holds the frame term, and the ONE arm that must be live (the check that
/// the frame index really is the sequence's input) says so explicitly.
static void armGather(Scene *s, GiParams gi, bool on, unsigned stride = 16u,
                      unsigned octRes = 8u, bool freeze = true, bool farOff = false,
                      int adaptiveCap = -1, bool jitterOff = false)
{
    gi.gather = on ? GiToggle::On : GiToggle::Off;
    s->setGlobalIllumination(gi);
    GatherTuning t;
    t.probeStride = stride;
    t.octRes = octRes;
    t.freezeFrameIndex = freeze;
    t.farTermOff = farOff;
    t.adaptiveCap = adaptiveCap;
    t.jitterOff = jitterOff;
    s->setGatherTuning(t);
}

/// What the engine reports back about the last gathered frame.
static GatherStatus gatherStatus(Scene *s) { return s->giStatus().gather; }

static void writePpm(const Image &img, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4)
        std::fwrite(&img.rgba[i], 1, 3, f);
    std::fclose(f);
}

/// How two pictures differ, in the terms the sheet is written in.
struct Delta { unsigned moved = 0u; unsigned total = 0u; double meanMoved = 0.0; unsigned worst = 0u; };
static Delta deltaOf(const Image &a, const Image &b)
{
    Delta d;
    if (a.width != b.width || a.height != b.height) return d;
    d.total = a.width * a.height;
    double sum = 0.0;
    for (size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        unsigned worst = 0u;
        for (int c = 0; c < 3; ++c) {
            const unsigned dc = unsigned(std::abs(int(a.rgba[i + c]) - int(b.rgba[i + c])));
            if (dc > worst) worst = dc;
        }
        if (worst) { ++d.moved; sum += worst; if (worst > d.worst) d.worst = worst; }
    }
    d.meanMoved = d.moved ? sum / d.moved : 0.0;
    return d;
}

static bool identical(const Image &a, const Image &b)
{
    return a.width == b.width && a.height == b.height && a.rgba == b.rgba;
}

// ---------------------------------------------------------------------------
// THE LEAK ROOM IS gi.leak_room's OWN FIXTURE, shared through
// tests/support/enginetesthelpers.h (fix round, B1) rather than copied: this
// suite's `field` arm is compared against gi.leak_room's asserted numbers, and
// that comparison is only worth anything while the rooms are the same room.
using enginetest::leakroom::addSlab;
using enginetest::leakroom::meanRG;

// ---------------------------------------------------------------------------
static int costMain(Engine *e);
static int noRaysMain(Engine *e);

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = std::getenv("JAHSHAKA_NO_RAY_QUERY") ? "test-gi-gather-norays-ogre.log"
                  : std::getenv("JAH_GATHER_COST")     ? "test-gi-gather-cost-ogre.log"
                                                       : "test-gi-gather-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    if (std::getenv("JAH_GATHER_COST")) return costMain(e);
    if (std::getenv("JAHSHAKA_NO_RAY_QUERY")) return noRaysMain(e);

    const unsigned kSize = 256u;
    View *view = e->createOffscreenView("gather", kSize, kSize, Colour(0, 0, 0));
    if (!view) { std::printf("FAIL: view: %s\n", e->lastError().c_str()); return 1; }
    // Offscreen views ship with shadows OFF; the leak measurement is nothing
    // without them (the outside lamp would light the room through the wall).
    view->setShadows(true);
    armChain(view);

    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather is about the tier "
                    "and skips cleanly\n");
        return 0;
    }

    const char *dumpDir = std::getenv("JAH_GATHER_DUMP");

    // =====================================================================
    // 1. THE LEAK ROOM, four wall thicknesses, three arms each.
    //
    // A sealed 10 m room with a GREEN lamp inside and a RED one outside; with
    // shadows on the direct term through the wall is zero, so every red pixel
    // inside arrived through global illumination and that number IS the leak
    // (gi.leak_room's construction, and its bars are quoted beside ours).
    //
    // THE ARMS, all three in this process at this pose:
    //   field   — the irradiance field on cascade 0 (what ships today)
    //   cones   — the cascade cone diffuse with no field (ddgi off)
    //   gather  — GATHER-0's probe rays, with the cones compiled out
    const float thicknesses[4] = { 0.5f, 0.2f, 0.1f, 0.05f };
    // PHOTON-S2's numbers, quoted for provenance and NOT asserted against: they
    // were taken at six metres with no post chain, before PHOTON-M2 corrected
    // the field's units. gi.leak_room's own asserted bars (this pose, chain
    // arm) are 0.068 / 0.071 / 0.093 / 0.325 and are the honest comparison.
    const float s2[4] = { 0.041f, 0.046f, 0.054f, 0.083f };
    const float leakRoomBars[4] = { 0.068f, 0.071f, 0.093f, 0.325f };

    struct Row { float field = 0, cones = 0, gather = 0, greenGather = 0; };
    Row rows[4];
    for (int a = 0; a < 4; ++a) {
        const float T = thicknesses[a];
        Scene *s = e->createScene("leak" + std::to_string(a));
        view->setScene(s);
        enginetest::leakroom::Room room = enginetest::leakroom::build(s, view, T);

        // THE ACCOUNTING RULE. A pixel gets exactly ONE diffuse-GI term, and
        // since ogre-patch 0086 that is true of the FIELD too: the listener
        // compiles the cone diffuse out under the gather's pass property
        // (`vct_disable_diffuse`) and the patch's cage gate stands the field's
        // eight-probe cage down on every pixel a probe answered for. The three
        // arms are still measured with the field OFF in the gather's arm, for
        // a different and simpler reason: it is what makes the three arms
        // comparable, each being ONE estimator and nothing else.
        const auto leakOf = [&](GiToggle ddgi, bool gather, const char *what) {
            GiParams gi;
            gi.mode = GiMode::Vct;
            gi.quality = GiQuality::High;
            gi.ddgi = ddgi;
            gi.updateBudget = 1;
            gi.numBounces = 1;
            gi.cascades = true;
            armGather(s, gi, gather);
            enginetest::leakroom::setOutsideIntensity(s, room, 25.0f);
            s->refreshGlobalIllumination();
            render(e, 16);
            Image img; view->readPixels(img);
            float onR = 0, onG = 0; meanRG(img, onR, onG);
            if (dumpDir && a == 0)
                writePpm(img, std::string(dumpDir) + "/g0-leakroom-" + what + ".ppm");
            enginetest::leakroom::setOutsideIntensity(s, room, 0.0f);
            s->refreshGlobalIllumination();
            render(e, 16);
            view->readPixels(img);
            float offR = 0, offG = 0; meanRG(img, offR, offG);
            armGather(s, gi, false);
            std::printf("   %-7s wall %.2f m: LEAK %.4f (lamp on r %.4f, off r %.4f), green %.4f\n",
                        what, double(T), double(onR - offR), double(onR), double(offR),
                        double(onG));
            return std::make_tuple(onR - offR, onG);
        };
        const auto f = leakOf(GiToggle::On, false, "field");
        const auto c = leakOf(GiToggle::Off, false, "cones");
        const auto g = leakOf(GiToggle::Off, true, "gather");
        rows[a].field = std::get<0>(f);
        rows[a].cones = std::get<0>(c);
        rows[a].gather = std::get<0>(g);
        rows[a].greenGather = std::get<1>(g);
        e->destroyScene(s);
    }

    std::printf("\n THE LEAK, by wall thickness (red inside a sealed room, all three arms in one "
                "process at one pose)\n");
    std::printf(" wall(m)     field      cones     GATHER   leak_room bar   PHOTON-S2 (6 m, "
                "pre-M2)\n");
    for (int a = 0; a < 4; ++a)
        std::printf("  %5.2f   %9.4f  %9.4f  %9.4f   %12.4f   %12.4f\n", double(thicknesses[a]),
                    double(rows[a].field), double(rows[a].cones), double(rows[a].gather),
                    double(leakRoomBars[a]), double(s2[a]));
    std::printf("\n");
    // THE CLAIM PHASE 0 IS ASKED TO TEST: does a RAY gather leak less through a
    // thin wall than the cone/field estimate does? The physics says yes — a ray
    // is stopped by a triangle, a cone is stopped by a voxel, and at 0.05 m the
    // wall is sub-voxel at every cascade cell size. This is the assertion, and
    // it is stated as a comparison rather than a bar because a bar on a
    // measurement nobody has taken before is a number invented, not measured.
    for (int a = 0; a < 4; ++a)
        CHECK_MSG(rows[a].gather <= rows[a].field + 0.002f,
                  "the ray gather leaks no more than the field through the %.2f m wall "
                  "(%.4f vs %.4f)", double(thicknesses[a]), double(rows[a].gather),
                  double(rows[a].field));
    CHECK_MSG(rows[0].greenGather > 0.02f,
              "the room is lit by its own lamp under the gather (green %.4f) — a black room "
              "leaks nothing and proves nothing", double(rows[0].greenGather));

    // =====================================================================
    // 2. THE BOUNCE: A RED PANEL'S COLOUR ON A WHITE FLOOR, four arms at one
    // pose in one process.
    //
    // The panel is EMISSIVE, deliberately: its radiance owes nothing to a
    // light's direction or to a shadow map, so what the floor shows is the
    // diffuse-GI estimator and nothing else. The four arms are
    //   off     — GiMode::Off: no diffuse GI at all. The floor of the sheet.
    //   cones   — the cascade cone diffuse (ddgi off)
    //   field   — the irradiance field on cascade 0 (what ships today)
    //   gather  — GATHER-0's probe rays, with the cones compiled out
    {
        Scene *s = e->createScene("bounce");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addSlab(s, Colour(0.85f, 0.85f, 0.85f), Vec3(0, -0.25f, 0), Vec3(16, 0.5f, 16));
        {
            const NodeId panel = s->createNode();
            PbrParams p;
            p.albedo = Colour(0.05f, 0.05f, 0.05f);
            p.emissive = Colour(6.0f, 0.0f, 0.0f);
            p.roughness = 0.9f;
            const MaterialId m = s->createPbrMaterial(p);
            const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
            CHECK(panel && m && mesh && s->attachMesh(panel, mesh, m),
                  "the emissive red panel exists");
            s->setNodeTransform(panel, Vec3(0.0f, 2.0f, -4.0f), Quat(), Vec3(10, 4, 0.5f));
        }
        enginetest::addDirectionalLight(s, Vec3(0.0f, -0.55f, 0.83f), 0.6f);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.6f, 4.5f), Vec3(0.0f, 0.2f, -2.0f));

        const auto arm = [&](GiMode mode, GiToggle ddgi, bool gather, const char *what,
                             Image &out) {
            GiParams gi;
            gi.mode = mode;
            gi.quality = GiQuality::High;
            gi.ddgi = ddgi;
            gi.numBounces = 1;
            gi.cascades = true;
            armGather(s, gi, gather);
            s->refreshGlobalIllumination();
            render(e, 40);
            view->readPixels(out);
            // THE FLOOR'S RED, in the lower-middle block where the panel's
            // bounce lands and the panel itself is not in shot.
            double sr = 0.0, sg = 0.0; int n = 0;
            for (unsigned y = out.height * 11u / 16u; y < out.height * 15u / 16u; ++y)
                for (unsigned x = out.width * 6u / 16u; x < out.width * 10u / 16u; ++x) {
                    const Colour c = out.at(x, y); sr += c.r; sg += c.g; ++n;
                }
            const float red = float(sr / n), green = float(sg / n);
            std::printf("   %-7s floor red %.4f  green %.4f  (red excess over green %.4f)\n",
                        what, double(red), double(green), double(red - green));
            if (dumpDir)
                writePpm(out, std::string(dumpDir) + "/g1a-bounce-" + what + ".ppm");
            return red - green;
        };
        Image offImg, conesImg, fieldImg, gatherImg;
        const float offRed = arm(GiMode::Off, GiToggle::Off, false, "off", offImg);
        const float conesRed = arm(GiMode::Vct, GiToggle::Off, false, "cones", conesImg);
        const float fieldRed = arm(GiMode::Vct, GiToggle::On, false, "field", fieldImg);
        const float gatherRed = arm(GiMode::Vct, GiToggle::Off, true, "gather", gatherImg);

        GiParams gatherGi;
        gatherGi.mode = GiMode::Vct; gatherGi.quality = GiQuality::High;
        gatherGi.ddgi = GiToggle::Off; gatherGi.numBounces = 1; gatherGi.cascades = true;
        armGather(s, gatherGi, true);
        render(e, 40);
        const GatherStatus stats = gatherStatus(s);
        std::printf("\n   gather: %u x %u probes (%u) + %u adaptive (cap %u) x %u rays = %llu "
                    "rays/frame over %ux%u, place %.4f ms, trace %.4f ms, integrate %.4f ms, "
                    "record %.4f ms CPU, VRAM %llu bytes\n",
                    stats.probesX, stats.probesY, stats.probes, stats.adaptive, stats.adaptiveCap,
                    stats.raysPerProbe, (unsigned long long)stats.raysPerFrame, stats.targetW,
                    stats.targetH, double(stats.placeMs), double(stats.traceMs),
                    double(stats.integrateMs), double(stats.cpuMs),
                    (unsigned long long)stats.atlasBytes);
        CHECK(stats.on && stats.running,
              "the scene reports the gather ON and a view RUNNING it (giStatus().gather)");
        CHECK_MSG(gatherRed > offRed + 0.01f,
                  "THE BOUNCE IS THE RAYS': the floor's red excess is %.4f under the gather "
                  "against %.4f with no diffuse GI at all", double(gatherRed), double(offRed));
        CHECK_MSG(stats.probes > 0u && stats.raysPerFrame > 0ull,
                  "the tier reports the trace it dispatched (%u probes, %llu rays)", stats.probes,
                  (unsigned long long)stats.raysPerFrame);
        CHECK_MSG(stats.placeMs > 0.0f && stats.traceMs > 0.0f && stats.integrateMs > 0.0f,
                  "all three stages are timed on the GPU (place %.4f, trace %.4f, integrate "
                  "%.4f ms)", double(stats.placeMs), double(stats.traceMs),
                  double(stats.integrateMs));
        const Delta gf = deltaOf(fieldImg, gatherImg), gc = deltaOf(conesImg, gatherImg),
                    go = deltaOf(offImg, gatherImg), fo = deltaOf(offImg, fieldImg);
        std::printf("   THE SHEET at one pose (256x256):\n");
        const auto sheet = [](const char *what, const Delta &d) {
            std::printf("     %-24s %6u of %u px (%5.1f%%) mean %5.2f/255 worst %3u\n", what,
                        d.moved, d.total, 100.0 * d.moved / std::max(1u, d.total), d.meanMoved,
                        d.worst);
        };
        sheet("gather vs no diffuse GI", go);
        sheet("field  vs no diffuse GI", fo);
        sheet("gather vs field", gf);
        sheet("gather vs cones", gc);
        std::printf("   floor red excess: off %.4f | cones %.4f | field %.4f | GATHER %.4f\n",
                    double(offRed), double(conesRed), double(fieldRed), double(gatherRed));

        // ---- THE INSTRUMENT'S FLOOR, before any A/B is trusted -----------
        // The same frozen arm, pushed twice exactly as an A/B pushes its two
        // arms: whatever this pair moves is what the instrument moves on its
        // own, and an A/B reading at or below it has measured nothing. The bar
        // is 1 % of the frame; the frozen estimator is a pure function of the
        // scene, so the honest reading is zero.
        armGather(s, gatherGi, true);
        render(e, 40);
        Image floorA; view->readPixels(floorA);
        armGather(s, gatherGi, true);
        render(e, 40);
        Image floorB; view->readPixels(floorB);
        const Delta floorD = deltaOf(floorA, floorB);
        std::printf("   THE A/B INSTRUMENT'S FLOOR (one frozen arm pushed twice): %u of %u px "
                    "(%.2f%%) mean %.2f/255 worst %u\n", floorD.moved, floorD.total,
                    100.0 * floorD.moved / std::max(1u, floorD.total), floorD.meanMoved,
                    floorD.worst);
        CHECK_MSG(floorD.moved * 100u <= floorD.total,
                  "THE A/B INSTRUMENT IS QUIET: one frozen arm pushed twice moves %u of %u px "
                  "(bar 1 %%) — every A/B below differences two FROZEN arms", floorD.moved,
                  floorD.total);

        // ---- the far term's A/B (spec section 10 item 6), BOTH ARMS FROZEN ---
        // THE MEASURED TRUTH (MEASURE-1b, spikes/measure-1bd/FINDINGS.md): the
        // far term never fires. The ray's length is the outer cascade's full
        // DIAGONAL, and a ray that long leaving a point inside a box ends
        // outside it except on a set of measure zero — so the end point it
        // asks the cascades about lies in no volume, `ok` is false and the sky
        // answers whether the term is on or off. The earlier "53 % of pixels"
        // was this pair taken with the frame index LIVE: two draws of the
        // estimator, i.e. its own noise. This assertion states the finding; if
        // it ever reds, the ray length or the term changed and the decision at
        // OgreScreenProbeGather.cpp's `reach` has to be re-taken.
        armGather(s, gatherGi, true, 16u, 8u, true, true);
        render(e, 40);
        Image farOff; view->readPixels(farOff);
        armGather(s, gatherGi, true, 16u, 8u, true, false);
        render(e, 40);
        Image farOn; view->readPixels(farOn);
        const Delta fd = deltaOf(farOn, farOff);
        std::printf("   THE FAR TERM (the outer cascades' voxel at tMax, against the sky), "
                    "frozen: %u of %u px moved (%.2f%%), mean %.2f/255, worst %u\n", fd.moved,
                    fd.total, 100.0 * fd.moved / std::max(1u, fd.total), fd.meanMoved, fd.worst);
        CHECK_MSG(fd.moved == 0u,
                  "THE FAR TERM MOVES NO PIXEL at the shipped ray length (%u of %u px): the "
                  "diagonal-long ray ends outside every cascade, so the term's voxel read "
                  "is never ok and the sky answers either way", fd.moved, fd.total);
        if (dumpDir) writePpm(farOff, std::string(dumpDir) + "/g1a-bounce-gather-farOff.ppm");

        // ---- DETERMINISM (spec section 5) --------------------------------
        // With the sample sequence's frame term HELD, the whole estimator is a
        // pure function of the scene and the pixel: no clock, no atomic whose
        // order is the scheduler's, no subgroup identity. Three consecutive
        // frames of a still scene must be byte-identical.
        armGather(s, gatherGi, true, 16u, 8u, true);
        render(e, 24);
        Image a1, a2, a3;
        view->readPixels(a1);
        render(e, 1); view->readPixels(a2);
        render(e, 1); view->readPixels(a3);
        CHECK(identical(a1, a2) && identical(a2, a3),
              "DETERMINISM: with the frame term held, three consecutive frames of a still "
              "scene are byte-identical (no clock, no ordered atomic in the estimator)");
        // ...and with it LIVE the estimate moves, which is what says the frame
        // index really is the sequence's input and nothing else is. (THE ONE
        // LIVE ARM of this suite, and it is not an A/B: its print is the noise
        // every live A/B would have measured instead of its subject.)
        armGather(s, gatherGi, true, 16u, 8u, false);
        render(e, 8);
        Image b1, b2;
        view->readPixels(b1);
        render(e, 1); view->readPixels(b2);
        const Delta noise = deltaOf(b1, b2);
        std::printf("   THE NAKED GATHER'S FRAME-TO-FRAME NOISE (what phases 2-3 buy out): "
                    "%u of %u px (%.1f%%) mean %.2f/255 worst %u\n", noise.moved, noise.total,
                    100.0 * noise.moved / std::max(1u, noise.total), noise.meanMoved, noise.worst);
        CHECK(!identical(b1, b2),
              "...and with the frame term live the estimate moves frame to frame (it is what "
              "makes the temporal mean an integral)");

        // ---- D1: THE TIER CLOSES WHILE THE SPIKE IS ARMED ----------------
        // `Engine::setRayTracing(false)` tears the whole tier down —
        // `RayQueryTier::close()` destroys every gather texture — and it takes
        // `updateRayQuery`'s early return with it, so the frame-head clear of
        // the shader registration never runs again. Without the clear inside
        // `close()` the next colour pass binds a DESTROYED TextureGpu, which
        // is a validation error at best and a lost device at worst. Armed,
        // then torn down, then drawn: the frames after must be ordinary.
        armGather(s, gatherGi, true);
        render(e, 8);
        e->setRayTracing(false);
        render(e, 8);
        Image afterTeardown;
        view->readPixels(afterTeardown);
        CHECK(afterTeardown.width == kSize && afterTeardown.height == kSize,
              "D1: the tier torn down while the gather is on still draws a frame (the shader's "
              "registration dies with the textures it names)");
        // ...AND THE REGISTRATION IS ACTUALLY GONE, which is the half an image
        // size cannot see (the lead's read): the frame would draw either way,
        // once with a live binding and once with a freed TextureGpu behind it.
        // `running` false is the Component reporting that it holds nothing for
        // this scene any more.
        {
            const GatherStatus torn = gatherStatus(s);
            CHECK_MSG(!torn.running,
                      "D1: ...and the gather holds nothing for the scene after the teardown "
                      "(running %d, probes %u)", int(torn.running), torn.probes);
        }
        e->setRayTracing(true);
        render(e, 4);
        armGather(s, gatherGi, false);
        e->destroyScene(s);
    }

    // =====================================================================
    // 3. THE ADAPTIVE PASS: probes appear where a cell's pixels do not lie in
    //    its probe's plane, and NOWHERE else.
    //
    // Two fixtures in one scene, measured one after the other at two poses: a
    // flat floor filling the frame (one plane, every cell's pixels in it) and a
    // RAILING — a row of thin uprights crossing every cell of the shot, which
    // is the case Lumen's adaptive placement exists for and the case a uniform
    // grid interpolates straight through.
    //
    // The bar is a COMPARISON and not a count: what the pass must do is find
    // more cells on the railing than on the plane, and find none at all on the
    // plane. An absolute count would be a number invented rather than measured.
    {
        Scene *s = e->createScene("adaptive");
        view->setScene(s);
        s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.02f, 0.02f, 0.03f));
        addSlab(s, Colour(0.8f, 0.8f, 0.8f), Vec3(0, -0.25f, 0), Vec3(40, 0.5f, 40));
        enginetest::addDirectionalLight(s, Vec3(-0.2f, -1.0f, -0.35f), 2.0f);
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::Off;
        gi.numBounces = 1;
        gi.cascades = true;

        // THE PLANE: the camera low over the floor, nothing else in shot.
        enginetest::testCameraLookAt(view, Vec3(0.0f, 1.2f, 6.0f), Vec3(0.0f, 0.0f, 0.0f));
        armGather(s, gi, true);
        render(e, 24);
        const GatherStatus flat = gatherStatus(s);

        // THE RAILING: twenty thin uprights a metre apart, across the shot.
        const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
        PbrParams railP;
        railP.albedo = Colour(0.7f, 0.7f, 0.72f);
        railP.roughness = 0.8f;
        const MaterialId railMat = s->createPbrMaterial(railP);
        for (int i = 0; i < 20; ++i) {
            const NodeId n = s->createNode();
            if (!n || !s->attachMesh(n, cube, railMat)) { std::printf("FAIL: rail\n"); ++failures; break; }
            s->setNodeTransform(n, Vec3(-4.75f + 0.5f * float(i), 0.6f, 0.0f), Quat(),
                                Vec3(0.06f, 1.2f, 0.06f));
        }
        s->refreshGlobalIllumination();
        render(e, 24);
        const GatherStatus railing = gatherStatus(s);
        std::printf("   ADAPTIVE PROBES: a flat floor %u of a %u-probe grid (cap %u); the same "
                    "shot with a railing in it %u\n", flat.adaptive, flat.probes, flat.adaptiveCap,
                    railing.adaptive);
        CHECK_MSG(flat.adaptive == 0u,
                  "a cell of a FLAT FLOOR wants no second probe (%u appended)", flat.adaptive);
        CHECK_MSG(railing.adaptive > 0u && railing.adaptive > flat.adaptive,
                  "...and a railing crossing the cells does (%u appended against %u)",
                  railing.adaptive, flat.adaptive);
        CHECK_MSG(railing.adaptive <= railing.adaptiveCap,
                  "the adaptive pass stays inside its per-frame cap (%u of %u)", railing.adaptive,
                  railing.adaptiveCap);

        // ...and with the cap at zero the picture is still drawn (the arm that
        // prices the adaptive pass at phase 2, and the guard that a capped-out
        // frame is an ordinary frame).
        //
        // ASSERTED ON THE RAW COUNTER, not on `adaptive`: that one is
        // `min(requested, cap)`, so `adaptive <= cap` and `adaptive == 0` under
        // a cap of zero are arithmetic identities and would pass with the whole
        // placement job deleted (the lead's read). `adaptiveRequested` is what
        // the job actually asked for, and the cap is what it is doing something
        // TO.
        armGather(s, gi, true, 16u, 8u, true, false, 0);
        render(e, 16);
        const GatherStatus capped = gatherStatus(s);
        Image cappedImg; view->readPixels(cappedImg);
        CHECK_MSG(capped.adaptive == 0u && cappedImg.width == kSize,
                  "the adaptive cap at zero is a uniform grid and an ordinary frame (%u appended)",
                  capped.adaptive);
        CHECK_MSG(capped.adaptiveRequested > 0u && capped.adaptive == 0u,
                  "...and the placement job still ASKED for its probes under that cap (%u "
                  "requested, %u appended) — the cap is a budget, not an off switch, and the "
                  "raw counter is what says so",
                  capped.adaptiveRequested, capped.adaptive);
        CHECK_MSG(railing.adaptiveRequested >= railing.adaptive,
                  "the railing's raw request (%u) is what the cap clamped to %u",
                  railing.adaptiveRequested, railing.adaptive);

        // ---- MUST 6: A GATHER TOGGLE IS NOT A GI REBUILD -------------------
        // The row changes a chain shape and a listener registration and NOTHING
        // about the GI configuration, so it rides `setGiTuning` and not the
        // configuration push. It was in `GiParams::operator==` for a round,
        // which tore the whole cascade chain down and built it again to turn a
        // compute dispatch on — and made every A/B arm of every suite above
        // compare ACROSS a GI rebuild.
        // PUSHED THE WAY A HOST PUSHES IT: `setGlobalIllumination` has no
        // early-out by contract — it is the CONFIGURATION door and it rebuilds
        // — and the mirror only calls it when the configuration really moved.
        // A gather toggle moves no configuration, so it rides `setGiTuning`,
        // and this case asserts that route costs no rebuild. (Before the fix
        // round the row was in `GiParams::operator==`, so the mirror itself
        // would have taken the rebuilding door.)
        armGather(s, gi, false);
        render(e, 8);
        const GiStatus before = s->giStatus();
        GiParams toggled = gi;
        toggled.gather = GiToggle::On;
        s->setGiTuning(toggled);
        render(e, 8);
        const GatherStatus onStatus = gatherStatus(s);
        toggled.gather = GiToggle::Off;
        s->setGiTuning(toggled);
        render(e, 8);
        const GiStatus after = s->giStatus();
        CHECK_MSG(onStatus.running,
                  "the tuning push turned the gather ON without a configuration push (%u probes)",
                  onStatus.probes);
        CHECK_MSG(after.rebuilds == before.rebuilds &&
                      after.cascadeFullRebuilds == before.cascadeFullRebuilds,
                  "A GATHER TOGGLE RE-VOXELISES NOTHING: %llu/%llu rebuilds across an on-and-off "
                  "against %llu/%llu before it",
                  (unsigned long long)after.rebuilds,
                  (unsigned long long)after.cascadeFullRebuilds,
                  (unsigned long long)before.rebuilds,
                  (unsigned long long)before.cascadeFullRebuilds);

        // ---- MUST 4: NO PIXEL IS BLACK WHERE A PROBE DECLINED --------------
        // Turning the gather on compiles the cone diffuse out of the shader,
        // and every fallback term of this renderer lives inside the irradiance
        // field's block — so with the FIELD ABSENT (this arm, and every arm of
        // this suite) a pixel whose probe the plane test rejected used to get
        // NOTHING. A region MEAN cannot see it; a COUNT of black pixels can.
        armGather(s, gi, true);
        render(e, 24);
        Image lit; view->readPixels(lit);
        unsigned black = 0u, floorPixels = 0u;
        for (unsigned y = lit.height / 2u; y < lit.height; ++y)
            for (unsigned x = 0; x < lit.width; ++x) {
                const Colour c = lit.at(x, y);
                ++floorPixels;
                if (c.r < 0.004f && c.g < 0.004f && c.b < 0.004f) ++black;
            }
        std::printf("   BLACK PIXELS on the lit half of the gathered frame: %u of %u\n", black,
                    floorPixels);
        CHECK_MSG(black == 0u,
                  "NO PIXEL IS BLACK under the gather with no field bound (%u of %u) — the "
                  "pixels no probe answered for get the sky, not nothing", black, floorPixels);
        armGather(s, gi, false);
        e->destroyScene(s);
    }

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
/// THE FALLBACK ARM. `JAHSHAKA_NO_RAY_QUERY=1` makes vkCreateDevice never hear
/// of ray tracing, so there is no tier to arm — and the picture must be exactly
/// the one this machine already draws.
static int noRaysMain(Engine *e)
{
    View *view = e->createOffscreenView("gathernorays", 256u, 256u, Colour(0, 0, 0));
    Scene *s = e->createScene("gathernorays");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    view->setShadows(true);
    armChain(view);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.02f, 0.02f, 0.03f));
    addSlab(s, Colour(0.85f, 0.85f, 0.85f), Vec3(0, -0.25f, 0), Vec3(16, 0.5f, 16));
    addSlab(s, Colour(0.9f, 0.05f, 0.05f), Vec3(0, 2.0f, -4.0f), Vec3(10, 4, 0.5f));
    enginetest::addDirectionalLight(s, Vec3(0.0f, -0.55f, 0.83f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.6f, 4.5f), Vec3(0.0f, 0.2f, -2.0f));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.cascades = true;
    s->setGlobalIllumination(gi);
    render(e, 24);
    Image before; view->readPixels(before);

    armGather(s, gi, true);
    render(e, 24);
    const GatherStatus st = gatherStatus(s);
    CHECK(!st.on && !st.running,
          "the row resolves OFF on a machine with no ray query — the machine answers, not the "
          "project (giStatus().gather.on)");
    Image after; view->readPixels(after);
    CHECK(identical(before, after),
          "THE FALLBACK PICTURE IS UNTOUCHED: byte-identical with the row on and off");
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
/// THE COST, at 1080p, on a scene dense enough that the rays disagree with each
/// other. Not Showroom 2 — an engine suite cannot open a Studio sample — but
/// the same shape of question: how many GPU milliseconds do the two stages cost
/// at the resolution the spec's budget table is stated at?
static int costMain(Engine *e)
{
    View *view = e->createOffscreenView("gathercost", 1920u, 1080u, Colour(0, 0, 0));
    Scene *s = e->createScene("gathercost");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    view->setShadows(true);
    armChain(view);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_cost skips cleanly\n");
        return 0;
    }
    s->setAmbient(Colour(0.02f, 0.02f, 0.03f), Colour(0.01f, 0.01f, 0.02f));
    // A ROOM WITH THINGS IN IT: six walls so most rays hit, and 120 props so
    // the hits are at different distances and read different voxels.
    const MeshId cubeMesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams wallP;
    wallP.albedo = Colour(0.8f, 0.78f, 0.75f);
    wallP.roughness = 0.9f;
    const MaterialId wallMat = s->createPbrMaterial(wallP);
    struct Wall { Vec3 scale, pos; };
    const Wall walls[6] = {
        { Vec3(24.0f, 12.0f, 0.4f), Vec3(0.0f, 4.0f, 12.0f) },
        { Vec3(24.0f, 12.0f, 0.4f), Vec3(0.0f, 4.0f, -12.0f) },
        { Vec3(0.4f, 12.0f, 24.0f), Vec3(12.0f, 4.0f, 0.0f) },
        { Vec3(0.4f, 12.0f, 24.0f), Vec3(-12.0f, 4.0f, 0.0f) },
        { Vec3(24.0f, 0.4f, 24.0f), Vec3(0.0f, -1.0f, 0.0f) },
        { Vec3(24.0f, 0.4f, 24.0f), Vec3(0.0f, 10.0f, 0.0f) },
    };
    for (const Wall &w : walls) {
        const NodeId n = s->createNode();
        if (!n || !s->attachMesh(n, cubeMesh, wallMat)) { std::printf("FAIL: wall\n"); return 1; }
        enginetest::setNodeScale(s, n, w.scale);
        enginetest::setNodePosition(s, n, w.pos);
    }
    for (int i = 0; i < 120; ++i) {
        const NodeId n = s->createNode();
        PbrParams p;
        p.albedo = Colour(0.2f + 0.005f * float(i), 0.3f, 0.8f - 0.004f * float(i));
        p.emissive = Colour(0.0f, 0.0f, float(i % 5) * 0.4f);
        p.roughness = 0.6f;
        const MaterialId m = s->createPbrMaterial(p);
        if (!n || !m || !s->attachMesh(n, cubeMesh, m)) { std::printf("FAIL: prop\n"); return 1; }
        enginetest::setNodeScale(s, n, Vec3(1.2f, 0.8f + 0.02f * float(i % 9), 1.2f));
        enginetest::setNodePosition(s, n, Vec3(-10.0f + 1.7f * float(i % 13),
                                               0.2f + 0.6f * float(i % 7),
                                               -10.0f + 2.1f * float(i % 11)));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.numBounces = 1;
    gi.cascades = true;
    CHECK(s->setGlobalIllumination(gi), "the cascade chain builds over the room");
    // ONE VIEW GATHERS AT A TIME (GATHER-0's D3): `giStatus().gather` answers
    // for the first view of the scene that is running one, in a map keyed by
    // listener POINTER, so with two gathering views the milliseconds are the
    // allocator's choice. Every arm below leaves exactly one enabled.
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, -4.0f), Vec3(2.0f, 3.0f, 6.0f));

    // ONE GATHERING VIEW AT A TIME, and it is not tidiness (fix round, D3):
    // `gatherStatsInto` answers for the FIRST GatherView it finds for the
    // scene in an unordered_map keyed by listener POINTER, so with two views
    // of one scene both gathering, which view's milliseconds come back is
    // decided by the allocator's addresses. Every arm below therefore leaves
    // exactly one view enabled.
    const auto measure = [&](View *, unsigned stride, unsigned octRes, const char *what) {
        armGather(s, gi, true, stride, octRes);
        std::vector<float> place, trace, integrate;
        for (int i = 0; i < 90; ++i) {
            e->renderOneFrame();
            const GatherStatus q = gatherStatus(s);
            if (q.placeMs >= 0.0f) place.push_back(q.placeMs);
            if (q.traceMs >= 0.0f) trace.push_back(q.traceMs);
            if (q.integrateMs >= 0.0f) integrate.push_back(q.integrateMs);
        }
        const GatherStatus fin = gatherStatus(s);
        const auto median = [](std::vector<float> v) {
            if (v.empty()) return -1.0f;
            std::vector<float> tail(v.end() - std::min<size_t>(30u, v.size()), v.end());
            std::sort(tail.begin(), tail.end());
            return tail[tail.size() / 2];
        };
        const float pm = median(place), tm = median(trace), im = median(integrate);
        std::printf("   %-34s %u probes (+%u adaptive) x %u rays = %llu rays: PLACE %.4f, "
                    "TRACE %.4f, INTEGRATE %.4f ms, sum %.4f, CPU %.4f ms, VRAM %.2f MB\n",
                    what, fin.probes, fin.adaptive, fin.raysPerProbe,
                    (unsigned long long)fin.raysPerFrame, double(pm), double(tm), double(im),
                    double(pm + tm + im), double(fin.cpuMs),
                    double(fin.atlasBytes) / (1024.0 * 1024.0));
        CHECK_MSG(pm > 0.0f && tm > 0.0f && im > 0.0f, "%s: all three stages were timed", what);
        armGather(s, gi, false);
        render(e, 2);
        return pm + tm + im;
    };
    const float high = measure(view, 16u, 8u, "1080p, 16 px probes, 64 rays (High)");
    measure(view, 16u, 6u, "1080p, 16 px probes, 36 rays (Medium)");
    measure(view, 8u, 8u, "1080p, 8 px probes, 64 rays (Epic)");
    measure(view, 32u, 8u, "1080p, 32 px probes, 64 rays");

    // ---- THE VR EYE SIZE, as ONE mono target of the same pixel count ------
    // Two Quest Pro eyes are 10.26 Mpx (SCREEN_PROBE_GATHER_SPEC section 4's
    // VR column, from spikes/v1-rig). The gather's phase-0 shape declines a
    // STEREO target — the probe grid would have to be split at the eye seam,
    // which is phase 7's arm — so the cost is measured on a mono target of the
    // same area, which is exactly what the two dispatches would cost: the
    // trace is per PROBE and the integrate per PIXEL, and neither knows about
    // the seam. Stated as an equivalence, not as a VR measurement.
    View *vr = e->createOffscreenView("gathervr", 4320u, 2384u, Colour(0, 0, 0));
    if (vr) {
        // ...and the 1080p view stands DOWN first (see the note on `measure`):
        // with both enabled the "10.3 Mpx" rows could be the 1080p view's.
        view->setEnabled(false);
        vr->setScene(s);
        vr->setShadows(true);
        armChain(vr);
        enginetest::testCameraLookAt(vr, Vec3(0.0f, 3.0f, -4.0f), Vec3(2.0f, 3.0f, 6.0f));
        render(e, 8);
        measure(vr, 16u, 8u, "10.3 Mpx (two Quest Pro eyes), 16 px, 64 rays");
        measure(vr, 16u, 6u, "10.3 Mpx (two Quest Pro eyes), 16 px, 36 rays");
        // AND THE READING IS THE VR VIEW'S, asserted by its own size rather
        // than assumed — the whole point of GATHER-0's D3.
        armGather(s, gi, true, 16u, 8u);
        render(e, 8);
        const GatherStatus who = gatherStatus(s);
        CHECK_MSG(who.targetW == 4320u && who.targetH == 2384u,
                  "the VR rows above are the VR view's (%ux%u reported)", who.targetW,
                  who.targetH);
        armGather(s, gi, false);
        render(e, 2);
    }
    // THE NUMBER IS PRINTED, NOT GATED — and that is a gate-system decision,
    // not modesty (fix round, H2). A GPU-TIME assertion belongs to the
    // nightly tier and nowhere else: this rig's GPU idles at 210 of 3105 MHz
    // under Xvfb unless the clocks are LOCKED (the PHOTON-E2 fact), and a
    // fifteen-fold spread on 0.13 ms sits exactly on the spec's 2.0 ms
    // estimate. So this entry carries the `benchmark` label (out of the MERGE
    // and PUSH tiers by construction) and RUN_SERIAL, and the only assertion
    // left is a CATASTROPHE ceiling — two orders of magnitude of headroom —
    // so a real regression still reds while a contended run never does. The
    // measurement of record is the one taken by hand with the clocks locked;
    // it is in spikes/gather-0/FINDINGS.md.
    std::printf("\n   1080p High (trace + integrate) = %.4f ms, against SCREEN_PROBE_GATHER_SPEC "
                "section 4's 1.0-2.0 ms estimate for the WHOLE block.\n"
                "   NOT A BAR: lock the clocks (nvidia-smi --lock-gpu-clocks=2100,2550) before "
                "believing any GPU number from this rig.\n", double(high));
    CHECK_MSG(high > 0.0f && high < 20.0f,
              "1080p High (trace + integrate) is not catastrophically wrong: %.4f ms "
              "(a print, not the spec's bar — see the note)", double(high));
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
