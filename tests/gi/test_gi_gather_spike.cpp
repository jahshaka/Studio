// gi.gather_spike — GATHER-0, THE NAKED SCREEN-PROBE TRACE
// (SPECS/SCREEN_PROBE_GATHER_SPEC.md section 7 phase 0; the brief
// SPECS/briefs/GATHER-0.md).
//
// A MEASUREMENT SUITE. Its job is to PRINT the numbers the spec records as
// unmeasured and to assert only the things a phase-0 spike can honestly
// assert: that the trace and the integrate run and are timed, that a still
// frame is byte-deterministic, that the diffuse answer really comes from the
// rays, that the leak through a thin wall is measured against the field's own
// bars in the SAME process at the SAME pose, and that a machine without rays
// is untouched.
//
// THREE ENTRIES, ONE BINARY (the gi.rt_reflect shape):
//   gi.gather_spike         — the leak room, the picture, determinism
//   gi.gather_spike_cost    — the GPU milliseconds at 1080p on a dense scene
//   gi.gather_spike_norays  — the fallback: arming REFUSES, the picture is the
//                             one a no-ray machine already draws
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

static bool armGather(Scene *s, bool on, ProbeGatherSpikeResult &out, unsigned stride = 16u,
                      unsigned rays = 64u, bool freeze = false, bool farOff = false)
{
    ProbeGatherSpikeDesc d;
    d.on = on;
    d.probeStride = stride;
    d.raysPerProbe = rays;
    d.freezeFrameIndex = freeze;
    d.farTermOff = farOff;
    return s->probeGatherSpike(d, out);
}

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
// THE LEAK ROOM (gi.leak_room's fixture, rebuilt here so both arms and the
// gather run in ONE process at ONE pose — the paired-arm rule).
static NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId node = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = albedo;
    p.metalness = 0.0f;
    p.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    s->setNodeTransform(node, pos, Quat(), scale);
    return node;
}

static void meanRG(const Image &img, float &r, float &g)
{
    double sr = 0.0, sg = 0.0; int n = 0;
    const unsigned x0 = img.width * 5u / 16u, x1 = img.width * 11u / 16u;
    const unsigned y0 = img.height * 5u / 16u, y1 = img.height * 11u / 16u;
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = x0; x < x1; ++x) { const Colour c = img.at(x, y); sr += c.r; sg += c.g; ++n; }
    r = float(sr / n); g = float(sg / n);
}

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
        std::printf("ok: no ray queries on this machine — gi.gather_spike is about the tier "
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
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        const Colour white(0.8f, 0.8f, 0.8f);
        const float ho = 5.0f + T * 0.5f;
        const float span = 10.0f + 2.0f * T;
        addSlab(s, white, Vec3(0, -T * 0.5f, 0), Vec3(span, T, span));
        addSlab(s, white, Vec3(0, 4.0f + T * 0.5f, 0), Vec3(span, T, span));
        addSlab(s, white, Vec3(0, 2, -ho), Vec3(span, 4.0f, T));
        addSlab(s, white, Vec3(0, 2, ho), Vec3(span, 4.0f, T));
        addSlab(s, white, Vec3(-ho, 2, 0), Vec3(T, 4.0f, span));
        addSlab(s, white, Vec3(ho, 2, 0), Vec3(T, 4.0f, span));

        const NodeId inside = s->createNode();
        s->setNodeTransform(inside, Vec3(0.0f, 3.0f, 2.5f), Quat(), Vec3(1, 1, 1));
        LightDesc li;
        li.type = LightType::Point;
        li.colour = Colour(0.0f, 1.0f, 0.0f);
        li.intensity = 3.0f;
        li.range = 14.0f;
        li.castShadows = true;
        s->setLight(inside, li);

        const NodeId outside = s->createNode();
        s->setNodeTransform(outside, Vec3(0.0f, 2.0f, -(ho + T * 0.5f + 1.0f)), Quat(),
                            Vec3(1, 1, 1));
        LightDesc lo;
        lo.type = LightType::Point;
        lo.colour = Colour(1.0f, 0.0f, 0.0f);
        lo.intensity = 25.0f;
        lo.range = 12.0f;
        lo.castShadows = true;
        s->setLight(outside, lo);

        enginetest::testCameraLookAt(view, Vec3(3.6f, 2.0f, -2.0f), Vec3(3.6f, 2.0f, -5.0f));

        const auto leakOf = [&](GiToggle ddgi, bool gather, const char *what) {
            GiParams gi;
            gi.mode = GiMode::Vct;
            gi.quality = GiQuality::High;
            gi.ddgi = ddgi;
            gi.updateBudget = 1;
            gi.numBounces = 1;
            gi.cascades = true;
            s->setGlobalIllumination(gi);
            ProbeGatherSpikeResult gr;
            armGather(s, gather, gr);
            lo.intensity = 25.0f; s->setLight(outside, lo); s->refreshGlobalIllumination();
            render(e, 16);
            Image img; view->readPixels(img);
            float onR = 0, onG = 0; meanRG(img, onR, onG);
            if (dumpDir && a == 0)
                writePpm(img, std::string(dumpDir) + "/g0-leakroom-" + what + ".ppm");
            lo.intensity = 0.0f; s->setLight(outside, lo); s->refreshGlobalIllumination();
            render(e, 16);
            view->readPixels(img);
            float offR = 0, offG = 0; meanRG(img, offR, offG);
            armGather(s, false, gr);
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
            s->setGlobalIllumination(gi);
            ProbeGatherSpikeResult r;
            armGather(s, gather, r);
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
                writePpm(out, std::string(dumpDir) + "/g0-bounce-" + what + ".ppm");
            armGather(s, false, r);
            return red - green;
        };
        Image offImg, conesImg, fieldImg, gatherImg;
        const float offRed = arm(GiMode::Off, GiToggle::Off, false, "off", offImg);
        const float conesRed = arm(GiMode::Vct, GiToggle::Off, false, "cones", conesImg);
        const float fieldRed = arm(GiMode::Vct, GiToggle::On, false, "field", fieldImg);
        const float gatherRed = arm(GiMode::Vct, GiToggle::Off, true, "gather", gatherImg);

        ProbeGatherSpikeResult stats;
        {
            GiParams gi;
            gi.mode = GiMode::Vct; gi.quality = GiQuality::High; gi.ddgi = GiToggle::Off;
            gi.numBounces = 1; gi.cascades = true;
            s->setGlobalIllumination(gi);
            armGather(s, true, stats);
            render(e, 40);
            armGather(s, true, stats);
        }
        std::printf("\n   gather: %u x %u probes (%u) x %u rays = %llu rays/frame over %ux%u, "
                    "trace %.4f ms, integrate %.4f ms, record %.4f ms CPU, VRAM %llu bytes\n",
                    stats.probesX, stats.probesY, stats.probes, stats.raysPerProbe,
                    (unsigned long long)stats.raysPerFrame, stats.targetW, stats.targetH,
                    double(stats.traceMs), double(stats.integrateMs), double(stats.cpuMs),
                    (unsigned long long)stats.vramBytes);
        CHECK_MSG(gatherRed > offRed + 0.01f,
                  "THE BOUNCE IS THE RAYS': the floor's red excess is %.4f under the gather "
                  "against %.4f with no diffuse GI at all", double(gatherRed), double(offRed));
        CHECK_MSG(stats.probes > 0u && stats.raysPerFrame > 0ull,
                  "the tier reports the trace it dispatched (%u probes, %llu rays)", stats.probes,
                  (unsigned long long)stats.raysPerFrame);
        CHECK_MSG(stats.traceMs > 0.0f && stats.integrateMs > 0.0f,
                  "both stages are timed on the GPU (trace %.4f ms, integrate %.4f ms)",
                  double(stats.traceMs), double(stats.integrateMs));
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

        // ---- the far term's A/B (spec section 10 item 6) -----------------
        {
            GiParams gi;
            gi.mode = GiMode::Vct; gi.quality = GiQuality::High; gi.ddgi = GiToggle::Off;
            gi.numBounces = 1; gi.cascades = true;
            s->setGlobalIllumination(gi);
        }
        ProbeGatherSpikeResult fr;
        armGather(s, true, fr, 16u, 64u, false, true);
        render(e, 40);
        Image farOff; view->readPixels(farOff);
        armGather(s, true, fr, 16u, 64u, false, false);
        render(e, 40);
        Image farOn; view->readPixels(farOn);
        const Delta fd = deltaOf(farOn, farOff);
        std::printf("   THE FAR TERM (the outer cascades' voxel at tMax, against the sky): "
                    "%u of %u px moved (%.1f%%), mean %.2f/255, worst %u\n", fd.moved, fd.total,
                    100.0 * fd.moved / std::max(1u, fd.total), fd.meanMoved, fd.worst);
        if (dumpDir) writePpm(farOff, std::string(dumpDir) + "/g0-bounce-gather-farOff.ppm");

        // ---- DETERMINISM (spec section 5) --------------------------------
        // With the sample sequence's frame term HELD, the whole estimator is a
        // pure function of the scene and the pixel: no clock, no atomic whose
        // order is the scheduler's, no subgroup identity. Three consecutive
        // frames of a still scene must be byte-identical.
        ProbeGatherSpikeResult fz;
        armGather(s, true, fz, 16u, 64u, true);
        render(e, 24);
        Image a1, a2, a3;
        view->readPixels(a1);
        render(e, 1); view->readPixels(a2);
        render(e, 1); view->readPixels(a3);
        CHECK(identical(a1, a2) && identical(a2, a3),
              "DETERMINISM: with the frame term held, three consecutive frames of a still "
              "scene are byte-identical (no clock, no ordered atomic in the estimator)");
        // ...and with it LIVE the estimate moves, which is what says the frame
        // index really is the sequence's input and nothing else is.
        armGather(s, true, fz, 16u, 64u, false);
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
        armGather(s, false, fz);
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

    ProbeGatherSpikeResult r;
    const bool armed = armGather(s, true, r);
    CHECK(!armed && !r.armed, "arming REFUSES on a machine with no ray query, with a reason");
    std::printf("   refusal: %s\n", r.error.c_str());
    render(e, 24);
    Image after; view->readPixels(after);
    CHECK(identical(before, after),
          "THE FALLBACK PICTURE IS UNTOUCHED: byte-identical before and after the refused arm");
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
        std::printf("ok: no ray queries on this machine — gi.gather_spike_cost skips cleanly\n");
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
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, -4.0f), Vec3(2.0f, 3.0f, 6.0f));

    const auto measure = [&](View *v, unsigned stride, unsigned rays, const char *what) {
        (void)v;
        ProbeGatherSpikeResult r;
        armGather(s, true, r, stride, rays);
        std::vector<float> trace, integrate;
        for (int i = 0; i < 90; ++i) {
            e->renderOneFrame();
            ProbeGatherSpikeResult q;
            armGather(s, true, q, stride, rays);
            if (q.traceMs >= 0.0f) trace.push_back(q.traceMs);
            if (q.integrateMs >= 0.0f) integrate.push_back(q.integrateMs);
        }
        ProbeGatherSpikeResult fin;
        armGather(s, true, fin, stride, rays);
        const auto median = [](std::vector<float> v) {
            if (v.empty()) return -1.0f;
            std::vector<float> tail(v.end() - std::min<size_t>(30u, v.size()), v.end());
            std::sort(tail.begin(), tail.end());
            return tail[tail.size() / 2];
        };
        const float tm = median(trace), im = median(integrate);
        std::printf("   %-34s %u probes x %u rays = %llu rays: TRACE %.4f ms, INTEGRATE %.4f ms, "
                    "sum %.4f, CPU %.4f ms, VRAM %.2f MB\n",
                    what, fin.probes, fin.raysPerProbe, (unsigned long long)fin.raysPerFrame,
                    double(tm), double(im), double(tm + im), double(fin.cpuMs),
                    double(fin.vramBytes) / (1024.0 * 1024.0));
        CHECK_MSG(tm > 0.0f && im > 0.0f, "%s: both stages were timed", what);
        ProbeGatherSpikeResult off;
        armGather(s, false, off);
        render(e, 2);
        return tm + im;
    };
    const float high = measure(view, 16u, 64u, "1080p, 16 px probes, 64 rays (High)");
    measure(view, 16u, 16u, "1080p, 16 px probes, 16 rays");
    measure(view, 8u, 64u, "1080p, 8 px probes, 64 rays (Epic)");
    measure(view, 32u, 64u, "1080p, 32 px probes, 64 rays");

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
        vr->setScene(s);
        vr->setShadows(true);
        armChain(vr);
        enginetest::testCameraLookAt(vr, Vec3(0.0f, 3.0f, -4.0f), Vec3(2.0f, 3.0f, 6.0f));
        render(e, 8);
        measure(vr, 16u, 64u, "10.3 Mpx (two Quest Pro eyes), 16 px, 64 rays");
        measure(vr, 16u, 32u, "10.3 Mpx (two Quest Pro eyes), 16 px, 32 rays");
    }
    // THE ONLY BAR, and it is the spec's own estimate for the whole block at
    // High: 1.0-2.0 ms. A phase-0 trace plus a nearest integrate is the floor
    // of that, so the bar is the estimate's ceiling — a number the spec already
    // committed to in writing, not one invented here.
    CHECK_MSG(high > 0.0f && high < 2.0f,
              "1080p High (trace + integrate) is inside the spec's 1.0-2.0 ms estimate for the "
              "whole block: %.4f ms", double(high));
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
