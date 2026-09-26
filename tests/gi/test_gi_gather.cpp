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
/// noise and nothing else (a far-term A/B once read its own noise as a 53 %
/// effect: spikes/measure-1bd/FINDINGS.md). So every
/// arm holds the frame term, and the ONE arm that must be live (the check that
/// the frame index really is the sequence's input) says so explicitly.
static void armGather(Scene *s, GiParams gi, bool on, unsigned stride = 16u,
                      unsigned octRes = 8u, bool freeze = true, float rayLength = 0.0f,
                      int adaptiveCap = -1, bool jitterOff = false)
{
    gi.gather = on ? GiToggle::On : GiToggle::Off;
    s->setGlobalIllumination(gi);
    GatherTuning t;
    t.probeStride = stride;
    t.octRes = octRes;
    t.freezeFrameIndex = freeze;
    t.rayLength = rayLength;
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
static int shippedCostMain(Engine *e);
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

    if (std::getenv("JAH_GATHER_COST_SHIPPED")) return shippedCostMain(e);
    if (std::getenv("JAH_GATHER_COST")) return costMain(e);
    if (std::getenv("JAHSHAKA_NO_RAY_QUERY")) return noRaysMain(e);
    // THE ESTIMATOR, NOT ITS MEAN (PHOTON-GATHER-1c): every arm of this suite is
    // an A/B that changes the SCENE between two readings a few frames apart (a
    // lamp on and off, a panel's arm, a ray length), and the pixel history would
    // carry the first arm's light into the second for as long as it remembers
    // (the leak room's "lamp off" read r 0.0040 of the "on" arm's 0.069 after 16
    // frames). So the suite holds the history's measurement lever for its whole
    // run — the frozen frame index's pair: the frozen index makes consecutive
    // frames the same estimate, the lever makes each picture that estimate.
    // gi.gather_stable and gi.gather_motion are what measure the history.
    ::setenv("JAHSHAKA_GATHER_NO_TEMPORAL", "1", 1);

    const unsigned kSize = 256u;
    View *view = e->createOffscreenView("gather", kSize, kSize, Colour(0, 0, 0));
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
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
    // 1. THE LEAK ROOM, four wall thicknesses (and the thinnest again, moved into ONE cascade-0
    //    texel), three arms each.
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
    // THE FIFTH ROW IS THE 0.05 m WALL MOVED -0.02 m (PHOTON-VOXEL-5): at the fixture's own place
    // the wall's inner face lies ON a cascade-0 texel plane (z = -5.0 on this pose's lattice), so
    // the two faces fall in two texels and the thin wall is two-sided only from cascade 2 on;
    // moved, both faces share one cascade-0 texel - the class the row is about (asserted below).
    constexpr int kRows = 5;
    const float thicknesses[kRows] = { 0.5f, 0.2f, 0.1f, 0.05f, 0.05f };
    const float shifts[kRows] = { 0.0f, 0.0f, 0.0f, 0.0f, -0.02f };
    // PHOTON-S2's numbers, quoted for provenance and NOT asserted against: they
    // were taken at six metres with no post chain, before PHOTON-M2 corrected
    // the field's units. gi.leak_room's own asserted bars (this pose, chain
    // arm) are 0.068 / 0.071 / 0.093 / 0.325 and are the honest comparison.
    const float s2[kRows] = { 0.041f, 0.046f, 0.054f, 0.083f, 0.083f };
    const float leakRoomBars[kRows] = { 0.068f, 0.071f, 0.093f, 0.325f, 0.325f };

    struct Row { float field = 0, cones = 0, gather = 0, greenGather = 0; bool twoSided = false; };
    Row rows[kRows];
    for (int a = 0; a < kRows; ++a) {
        const float T = thicknesses[a];
        Scene *s = e->createScene("leak" + std::to_string(a));
        view->setScene(s);
        enginetest::leakroom::Room room = enginetest::leakroom::build(s, view, T, shifts[a]);

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
        {   // is the -Z wall, where the camera looks, held by ONE cascade-0 texel (two-sided)?
            GiVoxelVolume v;
            if (s->giVoxelVolume(0, v) && v.available && !v.normal.empty()) {
                const double wz = -5.0 - 0.5 * double(T) + double(shifts[a]);
                const int ix = int(std::floor((3.6 - v.origin[0]) / v.cell[0]));
                const int iy = int(std::floor((2.0 - v.origin[1]) / v.cell[1]));
                const int iz = int(std::floor((wz - v.origin[2]) / v.cell[2]));
                if (ix >= 0 && iy >= 0 && iz >= 0 && ix < v.width && iy < v.height && iz < v.depth)
                    rows[a].twoSided = v.normal[((size_t(iz) * v.height + iy) * v.width + ix) * 4 + 3] > 0.5f;
            }
            std::printf("   wall %.2f m moved %.2f m: the -Z wall at cascade 0 is %s\n", double(T),
                        double(shifts[a]), rows[a].twoSided ? "ONE two-sided texel" : "not one two-sided texel");
        }
        e->destroyScene(s);
    }
    CHECK(rows[kRows - 1].twoSided,
          "the moved 0.05 m row measures what it claims: its wall is ONE two-sided cascade-0 texel");

    std::printf("\n THE LEAK, by wall thickness (red inside a sealed room, all three arms in one "
                "process at one pose)\n");
    std::printf(" wall(m)     field      cones     GATHER   leak_room bar   PHOTON-S2 (6 m, "
                "pre-M2)\n");
    for (int a = 0; a < kRows; ++a)
        std::printf("  %5.2f%s   %9.4f  %9.4f  %9.4f   %12.4f   %12.4f\n", double(thicknesses[a]),
                    shifts[a] != 0.0f ? "*" : " ",
                    double(rows[a].field), double(rows[a].cones), double(rows[a].gather),
                    double(leakRoomBars[a]), double(s2[a]));
    std::printf(" (* the wall moved %.2f m: both its faces in one cascade-0 texel)\n\n", double(shifts[kRows - 1]));
    // THE CLAIM PHASE 0 IS ASKED TO TEST: does a RAY gather leak less through a
    // thin wall than the cone/field estimate does? The physics says yes — a ray
    // is stopped by a triangle, a cone is stopped by a voxel, and at 0.05 m the
    // wall is sub-voxel at every cascade cell size. This is the assertion, and
    // it is stated as a comparison rather than a bar because a bar on a
    // measurement nobody has taken before is a number invented, not measured.
    //
    // AGAINST PHASE 1 (PHOTON-GATHER-1b, audit F8): 0.0367 / 0.0381 / 0.0457 /
    // 0.0764 against phase 1's 0.0367 / 0.0382 / 0.0458 / 0.0762 — the 0.05 m
    // wall reads +0.0002 (+0.3 %), deterministic (the frame index frozen). Its
    // mechanism: the filter's same-plane neighbour across a thin wall, in the
    // directions BOTH probes see far away — the plane test cannot separate two
    // floor probes on either side of a wall, the hit-distance test does for
    // every direction that hits the wall. Accepted: "at or below phase 1" is
    // missed by the letter, by 0.3 %, with the mechanism named.
    // ...AND THE RAY START AT THE SURFACE (PHOTON-GATHER-1d's audit round), the
    // bar DERIVED (the lead's ruling). The gather's read of this leak is a
    // smooth function of how far off the floor its rays start — the 0.05 m wall
    // measured at a start of 4 / 2 / 1 / 0.5 / <= 0.2 cm: 0.0764 / 0.0768 /
    // 0.0770 / 0.0771 / 0.0772 — a sensitivity S = (0.0772 - 0.0764) / 0.039 m
    // = 0.021 per metre, never a hole. So the gather's excess over the field is
    // accounted for term by term:
    //   0.0020  phase 1's accepted filter leak (the same-plane neighbour across a
    //           thin wall, above; measured with the lifted start),
    // + 0.0008  S x the start's MOVE, 4 cm -> the surface (0.021 x 0.039 m) — a
    //           deterministic shift, the physics of the fix, not noise,
    // + 0.00004 S x the start's own uncertainty (its epsilon, <= 2 mm here),
    // + the store's quantum, half float: the reading x 2^-10 (~0.00008).
    // = the field + ~0.0029 at the 0.05 m wall (measured +0.0024: 0.0772 vs
    // 0.0748). Both columns are the VOXEL read's leak through a sub-voxel wall
    // (the slabs carry no cards), so VOXEL-4's store will move both numbers; the
    // bar is the field's own read plus these terms and survives it.
    const float kSensitivity = (0.0772f - 0.0764f) / 0.039f;       // per metre of start height
    const float kStartMove = 0.039f, kStartUncertainty = 0.002f;
    // A.1 IS CLOSED (PHOTON-VOXEL-5 (ii), LIGHT PER FACE SIDE): a wall thinner than a cascade-0 cell
    // used to hold both faces in one texel with ONE radiance, and the gather's ray stopped on the
    // inner face read the lit outer one (VOXEL-4's named residual, 0.0102 at 0.05 m). The store
    // keeps each side's light now and the hit reads the side its ray meets - no residual, at the
    // fixture's place or moved into one texel (the fifth row).
    for (int a = 0; a < kRows; ++a) {
        const float bar = rows[a].field + 0.002f + kSensitivity * (kStartMove + kStartUncertainty) +
                          rows[a].gather / 1024.0f;
        CHECK_MSG(rows[a].gather <= bar,
                  "the ray gather leaks no more than the field through the %.2f m wall%s "
                  "(%.4f vs %.4f; bar %.4f = the field + 0.0020 phase 1's filter leak + %.4f the "
                  "start's move and uncertainty x %.3f/m + %.5f quantum)",
                  double(thicknesses[a]), shifts[a] != 0.0f ? " (one texel)" : "", double(rows[a].gather),
                  double(rows[a].field), double(bar), double(kSensitivity * (kStartMove + kStartUncertainty)),
                  double(kSensitivity), double(rows[a].gather / 1024.0f));
        // THE CONES THROUGH A LIT WALL (PHOTON-VOXEL-5 items (iii) / (ii)): the pixel's four cones
        // from inside the room read the wall's lit outer face in a coarse texel holding both faces
        // - 0.2190 / 0.2081 / 0.2213 / 0.2071 at the four thicknesses before light per face side and
        // per half-axis (spikes/photon-voxel-5/lab3; the lab's ideal 0 of 36 cones). BAR: the
        // field's own read + one display code (the block's mean of 8-bit codes).
        CHECK_MSG(rows[a].cones <= rows[a].field + 1.0f / 255.0f,
                  "the cones leak no more than the field through the %.2f m wall%s (%.4f vs %.4f + one code)",
                  double(thicknesses[a]), shifts[a] != 0.0f ? " (one texel)" : "", double(rows[a].cones),
                  double(rows[a].field));
    }
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
                    "rays/frame over %ux%u, place %.4f ms, trace %.4f ms, filter %.4f ms, "
                    "integrate %.5f ms, record %.4f ms CPU, VRAM %llu bytes\n",
                    stats.probesX, stats.probesY, stats.probes, stats.adaptive, stats.adaptiveCap,
                    stats.raysPerProbe, (unsigned long long)stats.raysPerFrame, stats.targetW,
                    stats.targetH, double(stats.placeMs), double(stats.traceMs),
                    double(stats.filterMs), double(stats.integrateMs), double(stats.cpuMs),
                    (unsigned long long)stats.vramBytes);
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

        // ---- THE RAY'S LENGTH, A/B, BOTH ARMS FROZEN (PHOTON-GAFAR-1) -----
        // The gather's ray is as long as the lit volume's inscribed radius —
        // half the outer cascade's extent — and a miss is the sky. The decision
        // was taken on a sweep (spikes/photon-gafar-1): against rays of the
        // outer box's full DIAGONAL (the old length) the half extent moved at
        // most 0.21 % of pixels by 1-2/255 on three fixtures x three tiers,
        // and cost the same. This is that claim at this suite's pose: the
        // shipped length against the diagonal, both frozen, inside the
        // instrument's 1 % bar. If it reds, geometry beyond the lit volume now
        // matters to the picture and the length has to be re-decided at
        // OgreScreenProbeGather.cpp's `reach`, with the sweep.
        {
            const GiStatus g = s->giStatus();
            const float outerHalf = g.cascades.empty() ? 0.0f : g.cascades.back().halfSize;
            const float diagonal = std::sqrt(3.0f) * 2.0f * outerHalf;
            CHECK_MSG(outerHalf > 0.0f, "the cascade chain reports its outer box (half %.2f m)",
                      double(outerHalf));
            armGather(s, gatherGi, true, 16u, 8u, true, diagonal);
            render(e, 40);
            Image longRays; view->readPixels(longRays);
            armGather(s, gatherGi, true, 16u, 8u, true, outerHalf);
            render(e, 40);
            Image forced; view->readPixels(forced);
            armGather(s, gatherGi, true);
            render(e, 40);
            Image shipped; view->readPixels(shipped);
            const Delta ld = deltaOf(shipped, longRays), fdd = deltaOf(shipped, forced);
            std::printf("   THE RAY'S LENGTH, frozen: the shipped (half extent %.2f m) against the "
                        "diagonal (%.2f m) moves %u of %u px (%.2f%%), mean %.2f/255, worst %u\n",
                        double(outerHalf), double(diagonal), ld.moved, ld.total,
                        100.0 * ld.moved / std::max(1u, ld.total), ld.meanMoved, ld.worst);
            CHECK_MSG(fdd.moved == 0u,
                      "THE SHIPPED LENGTH IS THE OUTER HALF EXTENT: derived and forced %.2f m "
                      "draw the same picture (%u px moved)", double(outerHalf), fdd.moved);
            CHECK_MSG(ld.moved * 100u <= ld.total,
                      "NOTHING BEYOND THE LIT VOLUME'S INSCRIBED RADIUS REACHES THE PICTURE: "
                      "rays of the full diagonal move %u of %u px against the shipped length "
                      "(bar 1 %%)", ld.moved, ld.total);
            if (dumpDir) writePpm(longRays, std::string(dumpDir) + "/g1a-bounce-gather-diagonal.ppm");
        }

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
        armGather(s, gi, true, 16u, 8u, true, 0.0f, 0);
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
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
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
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
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
    // THE ARMS' EXTRA KNOBS (PHOTON-GATHER-1b): the SH bands the integrate
    // evaluates (9 shipped; 4 = the memory-traffic arm the SH9 record is priced
    // against) and the filter in probe space off (the arm that prices it).
    // PHOTON-GATHER-1c: the pixel history off (the JAHSHAKA_GATHER_NO_TEMPORAL
    // lever — the phase-2 block exactly).
    struct Knobs { unsigned shBands = 0u; bool filterOff = false; bool noTemporal = false; };
    float lastIntegrate = 0.0f;
    const auto measure = [&](View *, unsigned stride, unsigned octRes, const char *what,
                             Knobs k = Knobs()) {
        armGather(s, gi, true, stride, octRes);
        {
            GatherTuning t;
            t.probeStride = stride;
            t.octRes = octRes;
            t.freezeFrameIndex = true;
            t.shBands = k.shBands;
            t.filterOff = k.filterOff;
            s->setGatherTuning(t);
        }
        if (k.noTemporal) ::setenv("JAHSHAKA_GATHER_NO_TEMPORAL", "1", 1);
        else              ::unsetenv("JAHSHAKA_GATHER_NO_TEMPORAL");
        std::vector<float> place, trace, filter, integrate;
        for (int i = 0; i < 90; ++i) {
            e->renderOneFrame();
            const GatherStatus q = gatherStatus(s);
            if (q.placeMs >= 0.0f) place.push_back(q.placeMs);
            if (q.traceMs >= 0.0f) trace.push_back(q.traceMs);
            if (q.filterMs >= 0.0f) filter.push_back(q.filterMs);
            if (q.integrateMs >= 0.0f) integrate.push_back(q.integrateMs);
        }
        const GatherStatus fin = gatherStatus(s);
        const auto median = [](std::vector<float> v) {
            if (v.empty()) return -1.0f;
            std::vector<float> tail(v.end() - std::min<size_t>(30u, v.size()), v.end());
            std::sort(tail.begin(), tail.end());
            return tail[tail.size() / 2];
        };
        const float pm = median(place), tm = median(trace), fm = median(filter),
                    im = median(integrate);
        std::printf("   %-44s %u probes (+%u adaptive) x %u rays = %llu rays: PLACE %.4f, "
                    "TRACE %.4f, FILTER %.4f, INTEGRATE %.4f ms, sum %.4f, CPU %.4f ms, VRAM "
                    "%.2f MB\n",
                    what, fin.probes, fin.adaptive, fin.raysPerProbe,
                    (unsigned long long)fin.raysPerFrame, double(pm), double(tm), double(fm),
                    double(im), double(pm + tm + fm + im), double(fin.cpuMs),
                    double(fin.vramBytes) / (1024.0 * 1024.0));
        CHECK_MSG(pm > 0.0f && tm > 0.0f && fm > 0.0f && im > 0.0f,
                  "%s: all four stages were timed", what);
        ::unsetenv("JAHSHAKA_GATHER_NO_TEMPORAL");
        lastIntegrate = im;
        armGather(s, gi, false);
        render(e, 2);
        return pm + tm + fm + im;
    };
    const float high = measure(view, 16u, 8u, "1080p, 16 px probes, 64 rays (High)");
    measure(view, 32u, 8u, "1080p, 32 px probes, 64 rays");
    // THE FILTER'S PRICE, PAIRED IN ONE PROCESS (fix round, audit F3): each tier
    // measured with the filter in probe space ON and OFF, interleaved over three
    // rounds, and the ratio of the medians quoted. OFF still runs the SH reduce
    // and the five-probe integrate, so the ratio is what the neighbourhood taps
    // (now a per-workgroup table in shared memory) cost on top of the rest.
    Knobs nof; nof.filterOff = true;
    const auto paired = [&](View *v, unsigned stride, unsigned octRes, const char *tier) {
        std::vector<float> on, off;
        for (int round = 0; round < 3; ++round) {
            on.push_back(measure(v, stride, octRes, (std::string(tier) + ", filter ON").c_str()));
            off.push_back(measure(v, stride, octRes, (std::string(tier) + ", filter OFF").c_str(), nof));
        }
        std::sort(on.begin(), on.end());
        std::sort(off.begin(), off.end());
        std::printf("   PAIRED %-10s block %.4f ms with the filter, %.4f without: ratio %.3f\n", tier,
                    double(on[1]), double(off[1]), double(on[1] / std::max(off[1], 1e-6f)));
    };
    paired(view, 16u, 8u, "High");
    paired(view, 8u, 8u, "Epic");
    paired(view, 16u, 6u, "Medium");
    // THE PIXEL HISTORY'S PRICE, PAIRED IN ONE PROCESS (PHOTON-GATHER-1c item 4):
    // each tier with the history OFF (the lever: the phase-2 block exactly) and
    // ON (the shipped block), interleaved over three rounds; the medians and the
    // INTEGRATE delta (the history lives in the integrate) quoted.
    const auto pairedHistory = [&](View *v, unsigned stride, unsigned octRes, const char *tier) {
        std::vector<float> off, on, intOff, intOn;
        Knobs kOff; kOff.noTemporal = true;
        Knobs kOn;
        for (int round = 0; round < 3; ++round) {
            off.push_back(measure(v, stride, octRes, (std::string(tier) + ", history OFF").c_str(), kOff));
            intOff.push_back(lastIntegrate);
            on.push_back(measure(v, stride, octRes, (std::string(tier) + ", history ON").c_str(), kOn));
            intOn.push_back(lastIntegrate);
        }
        for (auto *vec : { &off, &on, &intOff, &intOn }) std::sort(vec->begin(), vec->end());
        std::printf("   PAIRED %-10s block %.4f ms history OFF, %.4f ON (ratio %.3f; INTEGRATE %.4f -> "
                    "%.4f, +%.4f ms)\n",
                    tier, double(off[1]), double(on[1]), double(on[1] / std::max(off[1], 1e-6f)),
                    double(intOff[1]), double(intOn[1]), double(intOn[1] - intOff[1]));
    };
    pairedHistory(view, 16u, 8u, "High");
    pairedHistory(view, 8u, 8u, "Epic");
    pairedHistory(view, 16u, 6u, "Medium");
    // THE SH9 RECORD'S PRICE (PHOTON-GATHER-1b item 3's premise): the same Epic
    // arm with the integrate reading 3 of the record's 7 SH vec4s (L0-L1)
    // against all 7, paired and interleaved in this process — the INTEGRATE
    // column is the memory traffic's cost.
    {
        Knobs sh4; sh4.shBands = 4u;
        Knobs sh9; sh9.shBands = 9u;
        for (int round = 0; round < 3; ++round) {
            measure(view, 8u, 8u, "Epic, SH9 integrate", sh9);
            measure(view, 8u, 8u, "Epic, SH4 integrate (L0-L1 only)", sh4);
        }
    }

    // ---- THE VR EYE SIZE, as ONE mono target of the same pixel count ------
    // Two Quest Pro eyes are 10.26 Mpx (SCREEN_PROBE_GATHER_SPEC section 4's
    // VR column, from spikes/v1-rig). The gather's phase-0 shape declines a
    // STEREO target — the probe grid would have to be split at the eye seam,
    // which is phase 7's arm — so the cost is measured on a mono target of the
    // same area, which is exactly what the two dispatches would cost: the
    // trace is per PROBE and the integrate per PIXEL, and neither knows about
    // the seam. Stated as an equivalence, not as a VR measurement.
    View *vr = e->createOffscreenView("gathervr", 4320u, 2384u, Colour(0, 0, 0));
    if (vr) vr->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
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
        paired(vr, 16u, 8u, "VR 10.3Mpx");
        pairedHistory(vr, 16u, 8u, "VR 10.3Mpx");
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

// ---------------------------------------------------------------------------
// THE SHIPPED GATHER'S ABSOLUTE COST (PHOTON-GATHER-1d fix round, item 8) — a
// MEASUREMENT, run by hand under scripts/gpu-exclusive.sh with
// JAH_GATHER_COST_SHIPPED=1 (no ctest registers it): 1080p, the tier's own
// stride and rays (High 16 px, Epic 8 px, 64 rays), the frame index LIVE and the
// rest door shut (a still view would hold and dispatch nothing), in a carded
// room, the card read ON against OFF — the two arms paired in one process. The
// four jobs' GPU timestamps, median of 60 frames after 60 of warm-up.
static int shippedCostMain(Engine *e)
{
    View *view = e->createOffscreenView("gathershipped", 1920u, 1080u, Colour(0, 0, 0));
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);
    Scene *s = e->createScene("gathershipped");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    view->setShadows(true);
    armChain(view);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine\n");
        return 0;
    }
    s->setAmbient(Colour(0.02f, 0.02f, 0.03f), Colour(0.01f, 0.01f, 0.02f));
    MeshData md = enginetest::unitCubeMesh();
    md.cards = enginetest::boxCards(0.5f);
    const MeshId cubeMesh = s->createMesh(md);
    PbrParams wallP;
    wallP.albedo = Colour(0.8f, 0.78f, 0.75f);
    wallP.roughness = 0.9f;
    const MaterialId wallMat = s->createPbrMaterial(wallP);
    const Vec3 walls[6][2] = {
        { Vec3(24, 12, 0.4f), Vec3(0, 4, 12) }, { Vec3(24, 12, 0.4f), Vec3(0, 4, -12) },
        { Vec3(0.4f, 12, 24), Vec3(12, 4, 0) }, { Vec3(0.4f, 12, 24), Vec3(-12, 4, 0) },
        { Vec3(24, 0.4f, 24), Vec3(0, -1, 0) }, { Vec3(24, 0.4f, 24), Vec3(0, 10, 0) } };
    for (const auto &w : walls) {
        const NodeId n = s->createNode();
        if (!n || !s->attachMesh(n, cubeMesh, wallMat)) { std::printf("FAIL: wall\n"); return 1; }
        enginetest::setNodeScale(s, n, w[0]);
        enginetest::setNodePosition(s, n, w[1]);
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
        enginetest::setNodePosition(s, n, Vec3(-10.0f + 1.7f * float(i % 13), 0.2f + 0.6f * float(i % 7),
                                               -10.0f + 2.1f * float(i % 11)));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, -4.0f), Vec3(2.0f, 3.0f, 6.0f));
    const auto median = [](std::vector<float> v) {
        if (v.empty()) return -1.0f;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    std::printf("   tier   cards  stride rays probes(+adaptive)  place  trace  filter integrate  TOTAL ms  "
                "(cards resident)\n");
    for (int epic = 0; epic < 2; ++epic)
        for (int cards = 1; cards >= 0; --cards) {
            GiParams gi;
            gi.mode = GiMode::Vct;
            gi.quality = GiQuality::High;
            gi.epicTier = epic != 0;
            gi.ddgi = GiToggle::Off;
            gi.numBounces = epic ? 3 : 1;
            gi.cascades = true;
            gi.cards = cards ? GiToggle::On : GiToggle::Off;
            gi.gather = GiToggle::On;
            s->setGlobalIllumination(gi);
            GatherTuning t;
            t.restOff = true;
            s->setGatherTuning(t);
            render(e, 60);
            std::vector<float> pl, tr, fi, in, tot;
            for (int i = 0; i < 60; ++i) {
                e->renderOneFrame();
                const GatherStatus q = gatherStatus(s);
                if (q.placeMs < 0.0f || q.traceMs < 0.0f || q.filterMs < 0.0f || q.integrateMs < 0.0f)
                    continue;
                pl.push_back(q.placeMs); tr.push_back(q.traceMs); fi.push_back(q.filterMs);
                in.push_back(q.integrateMs);
                tot.push_back(q.placeMs + q.traceMs + q.filterMs + q.integrateMs);
            }
            const GatherStatus q = gatherStatus(s);
            std::printf("   %-6s %-5s  %4u  %4u  %6u(+%u)      %.3f  %.3f  %.3f   %.3f     %.3f   (%u)\n",
                        epic ? "Epic" : "High", cards ? "ON" : "off", q.stride, q.raysPerProbe, q.probes,
                        q.adaptive, double(median(pl)), double(median(tr)), double(median(fi)),
                        double(median(in)), double(median(tot)), s->giStatus().cards.cardsResident);
        }
    return 0;
}
