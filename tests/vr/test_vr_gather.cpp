// vr.gather_stereo — THE SCREEN-PROBE GATHER ON A TWO-EYE TARGET (PHOTON-GA-VR,
// SPECS/briefs/PHOTON-GA-VR.md section 3.2 / 3.3).
//
// WHAT IT PROVES, and how each claim is made exact rather than loose:
//
//   1. BOTH EYES RECORD A PROBE GRID, AND IT IS TWO GRIDS SPLIT AT THE SEAM.
//      The session's eyes are rendered at 500 x 300 — an eye width that the
//      16-pixel stride does NOT divide (31.25 cells). A grid laid over the whole
//      two-eye target as if it were one image would put a cell across the seam
//      (pixels 496-511 = the left eye's last four columns and the right eye's
//      first twelve) and start the right eye's cells a quarter-cell out of step
//      with the left eye's. GatherStatus must report the two-grid shape
//      (probesX = 2 x eyeProbesX, eyeProbesX = ceil(500 / 16) = 32), and the
//      irradiance must be covered in BOTH halves and in the columns against the
//      seam on either side.
//
//   2. THE CONTROL: worldScale ZERO (vr.session's control, the same reason). At
//      worldScale 0 the two eyes are at one pose, so if each eye is gathered
//      through its OWN grid, its OWN basis and its OWN history — and every
//      stochastic input keyed on the eye's own cell and pixel — the two halves
//      of the gather's irradiance AGREE. Not bit for bit, and that is measured,
//      not assumed: the two halves' DEPTH is rasterised at window coordinates
//      500 pixels apart and differs in its last bits (the eyes' 8-bit pictures
//      are byte-identical — vr.session_undithered — their float depth is not),
//      so a probe's position moves by float noise and a value by a half-float
//      ulp or two: 56-1,838 of 600,000 values, a different set every run (the
//      simulated head sways), with the hit list off as with it on. The bar sits
//      on that noise — at most 1 % of the values differ, by at most 1/1000 of the
//      eye's mean irradiance averaged over every value — and a straddling grid, an eye whose basis was never written, a
//      filter or an integrate that reaches across the seam, a history read from
//      the other eye's half each move most of the covered pixels by far more
//      (the lane's mutation arm: spikes/photon-ga-vr/EVIDENCE.md).
//
//   3. THE PARALLAX ARM: worldScale ONE. The halves must DIFFER (the eyes see the
//      pillar against the wall from 6 cm apart) and each must still be covered;
//      the irradiance being a property of the SURFACE and not of the eye, the two
//      eyes' mean irradiance over their covered pixels agrees within 5 %.
//
//   4. THE HISTORY IS THE PACKED SIZE: 16 bytes a pixel for the pair (two rg32ui
//      halves of the ping-pong), and a stereo view holds no rest mean (it never
//      rests — Monado's simulated head sways, a real one jitters): the reported
//      VRAM is exactly the atlas + the records + the irradiance + the history +
//      one stand-in texel.
//
// SKIPS (exit 77) with no monado-service, no manifest, no display (the runner),
// no OpenXR runtime, or no ray-query device (the gather traces).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using namespace enginetest;

namespace {

int gFailures = 0;

#define CHECK_MSG(cond, ...)                                                     \
    ([&]() -> bool {                                                             \
        const bool ok_ = (cond);                                                 \
        if (!ok_) {                                                              \
            ++gFailures;                                                         \
            std::printf("    FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);     \
            std::printf(__VA_ARGS__); std::printf("\n");                         \
        } else { std::printf("    ok   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
        return ok_;                                                              \
    }())

constexpr unsigned kEyeW = 500u, kEyeH = 300u;
constexpr unsigned kStride = 16u;   // the tier table's gather stride at High

EngineConfig vrConfig() {
    EngineConfig cfg;
    cfg.backend      = Backend::Vulkan;
    cfg.pluginDir    = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile      = "test_vr_gather-ogre.log";
    cfg.vr           = VrMode::IfAvailable;
    return cfg;
}

PbrParams matte(const Colour &albedo) {
    PbrParams p;
    p.albedo = albedo;
    p.roughness = 1.0f;
    p.workflow = PbrParams::Workflow::Specular;
    p.ior = 1.0f;
    p.specularColour = Colour(0.0f, 0.0f, 0.0f);
    return p;
}

void addBox(Scene *s, MeshId mesh, const Colour &albedo, const Vec3 &pos, const Vec3 &scale) {
    const NodeId n = s->createNode();
    const MaterialId m = s->createPbrMaterial(matte(albedo));
    if (!n || !m || !s->attachMesh(n, mesh, m)) return;
    s->setNodeTransform(n, pos, Quat(), scale);
}

/// vr.session's fixture shape — a near pillar against a far wall, both tall (in
/// frame from any head height), on a floor — with a warm side wall so the bounce
/// has a colour. The sun is the only light; the ambient is black, so every
/// indirect photon is the gather's.
void buildScene(Scene *s) {
    const MeshId cube = s->createMesh(unitCubeMesh());
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    addDirectionalLight(s, Vec3{ -0.4f, -1.0f, -0.55f }, 3.14159f);
    addBox(s, cube, Colour(0.80f, 0.25f, 0.15f), Vec3(0.35f, 1.0f, -1.0f), Vec3(0.25f, 4.0f, 0.25f));
    addBox(s, cube, Colour(0.20f, 0.55f, 0.85f), Vec3(0.0f, 1.5f, -4.0f), Vec3(12.0f, 8.0f, 0.2f));
    addBox(s, cube, Colour(0.60f, 0.60f, 0.60f), Vec3(0.0f, -0.05f, 0.0f), Vec3(40.0f, 0.1f, 40.0f));
    addBox(s, cube, Colour(0.85f, 0.55f, 0.25f), Vec3(-2.5f, 1.5f, -1.5f), Vec3(0.2f, 4.0f, 5.0f));
}

unsigned long long pump(Engine *e, unsigned long long want, unsigned budget) {
    const unsigned long long start = e->vrStatus().frames;
    for (unsigned i = 0; i < budget && e->vrStatus().frames < start + want; ++i) {
        e->advanceResources();
        e->renderOneFrame();
    }
    return e->vrStatus().frames - start;
}

struct EyeStats {
    size_t covered = 0, pixels = 0;
    double meanE = 0.0;   ///< mean E/pi luminance over covered pixels
    /// Covered pixels in the stride-wide bands at the eye's LEFT and RIGHT edges
    /// (of `bandPixels` each): the left eye's right band and the right eye's
    /// left band are against the SEAM; the other two are the target's outer
    /// borders, the same eye-local columns — the seam's counterpart.
    size_t leftBand = 0, rightBand = 0, bandPixels = 0;
};

/// One eye's half of the irradiance readback (rgba floats, the target's width).
EyeStats eyeStats(const GatherStatus &g, unsigned eye) {
    EyeStats st;
    const unsigned w = g.irradianceW, h = g.irradianceH, eyeW = w / 2u;
    double sum = 0.0;
    for (unsigned y = 0; y < h; ++y)
        for (unsigned lx = 0; lx < eyeW; ++lx) {
            const float *t = &g.irradiance[(size_t(y) * w + eye * eyeW + lx) * 4u];
            ++st.pixels;
            if (lx < kStride) ++st.bandPixels;
            if (!(t[3] > 0.5f)) continue;
            ++st.covered;
            if (lx < kStride) ++st.leftBand;
            if (lx + kStride >= eyeW) ++st.rightBand;
            sum += 0.2126 * t[0] + 0.7152 * t[1] + 0.0722 * t[2];
        }
    st.meanE = st.covered ? sum / double(st.covered) : 0.0;
    return st;
}

/// The two halves compared value for value: how many differ, the worst
/// absolute difference and the mean absolute difference over every value.
struct HalfDiff { size_t differ = 0, values = 0; double worstAbs = 0.0, meanAbs = 0.0; };
HalfDiff compareHalves(const GatherStatus &g) {
    HalfDiff d;
    const unsigned w = g.irradianceW, eyeW = w / 2u;
    for (unsigned y = 0; y < g.irradianceH; ++y)
        for (unsigned x = 0; x < eyeW; ++x)
            for (int c = 0; c < 4; ++c) {
                const float a = g.irradiance[(size_t(y) * w + x) * 4u + c];
                const float b = g.irradiance[(size_t(y) * w + x + eyeW) * 4u + c];
                ++d.values;
                if (a == b) continue;
                ++d.differ;
                d.worstAbs = std::max(d.worstAbs, std::fabs(double(a) - double(b)));
                d.meanAbs += std::fabs(double(a) - double(b));
            }
    if (d.values) d.meanAbs /= double(d.values);
    return d;
}

/// Neither seam band answers less than 90 % of its outer counterpart.
bool seamHolds(const EyeStats &l, const EyeStats &r) {
    return l.rightBand * 10u >= r.rightBand * 9u && r.leftBand * 10u >= l.leftBand * 9u;
}

/// `JAH_GATHER_DUMP=<dir>`: the irradiance of both eyes as a PPM (E/pi x 2,
/// clamped; uncovered pixels magenta) — the evidence picture, never asserted.
void dumpIrradiance(const GatherStatus &g, const char *tag) {
    const char *dir = std::getenv("JAH_GATHER_DUMP");
    if (!dir || g.irradiance.empty()) return;
    const std::string path = std::string(dir) + "/vr-gather-" + tag + ".ppm";
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", g.irradianceW, g.irradianceH);
    for (size_t i = 0; i < size_t(g.irradianceW) * g.irradianceH; ++i) {
        const float *t = &g.irradiance[i * 4u];
        unsigned char px[3] = { 255, 0, 255 };
        if (t[3] > 0.0f)
            for (int c = 0; c < 3; ++c)
                px[c] = (unsigned char)std::min(255.0f, std::max(0.0f, t[c] * 2.0f * 255.0f));
        std::fwrite(px, 1, 3, f);
    }
    std::fclose(f);
}

/// Frames until the readback holds a frame the gather drew in THIS session.
bool readIrradiance(Engine *e, Scene *s, GatherStatus &out) {
    for (int i = 0; i < 40; ++i) {
        pump(e, 1ull, 8u);
        out = s->giStatus().gather;
        if (out.running && !out.irradiance.empty() && out.irradianceW == 2u * kEyeW &&
            out.irradianceH == kEyeH)
            return true;
    }
    return false;
}

/// One session: begin at `worldScale`, gather, read, end.
bool session(Engine *e, Scene *s, float worldScale, GatherStatus &out) {
    VrConfig cfg;
    cfg.mirror = VrMirrorMode::None;
    cfg.worldScale = worldScale;
    cfg.overrideEyeWidth = kEyeW;
    cfg.overrideEyeHeight = kEyeH;
    if (!CHECK_MSG(e->beginVrSession(s, cfg), "beginVrSession at worldScale %.0f (%s)",
                   double(worldScale), e->lastError().c_str()))
        return false;
    // Stand the rig back from the pillar at a person's height (Monado's head sits
    // at the stage's origin).
    e->setVrOrigin(Vec3(0.0f, 1.4f, 1.6f), 0.0f);
    // THE SETTLE: the chain, the field and the history (N = 16) all warm.
    pump(e, 150ull, 1500u);
    const bool ok = readIrradiance(e, s, out);
    {
        const RayQueryStatus rq = s->rayQueryStatus();
        std::printf("    (hit list: %llu records the last traced frame, %llu dropped)\n",
                    rq.hitRecords, rq.hitDropped);
    }
    e->endVrSession();
    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    dumpIrradiance(out, worldScale > 0.5f ? "worldscale1" : "worldscale0");
    return CHECK_MSG(ok, "the gather's irradiance was read back from the stereo view (%ux%u)",
                     out.irradianceW, out.irradianceH);
}

}   // namespace

int main() {
    std::printf("vr.gather_stereo — the screen-probe gather on a two-eye target (PHOTON-GA-VR)\n");
    std::unique_ptr<Engine> engine;
    {
        std::string error;
        engine.reset(Engine::create(vrConfig(), error).release());
        if (!engine) { std::printf("SKIP: the engine did not start: %s\n", error.c_str()); return 77; }
    }
    if (!engine->vrAvailable()) {
        std::printf("SKIP: no OpenXR session-capable runtime: %s\n", engine->vrInfo().reason.c_str());
        return 77;
    }
    if (const char *want = std::getenv("JAH_VR_EXPECT_RUNTIME"))
        CHECK_MSG(engine->vrInfo().runtime.find(want) != std::string::npos,
                  "the runtime is the one the runner named ('%s', got '%s')", want,
                  engine->vrInfo().runtime.c_str());
    View *desktop = engine->createOffscreenView("desktop", 320, 240, Colour(0, 0, 0));
    if (!desktop) { std::printf("SKIP: no offscreen view: %s\n", engine->lastError().c_str()); return 77; }
    Scene *scene = engine->createScene("vr-gather");
    if (!scene) { std::printf("FAIL: no scene\n"); return 1; }
    desktop->setScene(scene);
    if (!engine->rayQueryAvailable() || !engine->rayTracing()) {
        std::printf("SKIP: no ray-query device — the gather does not trace here\n");
        return 77;
    }
    buildScene(scene);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.cascades = true;
    gi.numBounces = 1;
    gi.gather = GiToggle::On;   // forced: the test is the stereo gather, whatever the VR column says
    if (!CHECK_MSG(scene->setGlobalIllumination(gi), "the chain builds")) return 1;
    GatherTuning t;
    t.readback = true;
    // Room for every adaptive probe either eye asks for — one per cell, the
    // most a cell appends: a cap that clips would hand the two eyes' slots out
    // in the scheduler's order and the control below would compare two
    // different sets of probes.
    t.adaptiveCap = int(2u * ((kEyeW + kStride - 1u) / kStride) * ((kEyeH + kStride - 1u) / kStride));
    scene->setGatherTuning(t);
    for (int i = 0; i < 30; ++i) engine->renderOneFrame();

    // ---- THE CONTROL: worldScale 0 ------------------------------------------
    GatherStatus zero;
    if (!session(engine.get(), scene, 0.0f, zero)) return 1;
    const unsigned eyeCells = (kEyeW + kStride - 1u) / kStride;
    CHECK_MSG(zero.stereo && zero.stride == kStride && zero.eyeProbesX == eyeCells &&
                  zero.probesX == 2u * eyeCells,
              "TWO GRIDS SPLIT AT THE SEAM: stereo %d, stride %u, %u probe columns = 2 x %u "
              "(ceil(%u / %u) = %u per eye)",
              int(zero.stereo), zero.stride, zero.probesX, zero.eyeProbesX, kEyeW, kStride, eyeCells);
    const unsigned long long px = 2ull * kEyeW * kEyeH;
    CHECK_MSG(zero.historyBytes == px * 16ull,
              "THE HISTORY IS THE PACKED SIZE: %llu bytes = 16 B x %llu px (the rgba16f + r32ui "
              "pairs were 24 B: %llu)",
              zero.historyBytes, px, px * 24ull);
    CHECK_MSG(zero.vramBytes > zero.historyBytes && zero.vramBytes < zero.historyBytes + px * 8ull + (8ull << 20),
              "...and NO REST MEAN for a stereo view: the view's VRAM %llu bytes = the history + the "
              "irradiance (%llu) + the atlas and records, no second full-resolution image",
              zero.vramBytes, px * 8ull);
    CHECK_MSG(zero.restFrames == 0u, "a stereo view never rests (restFrames %u)", zero.restFrames);
    const EyeStats zl = eyeStats(zero, 0u), zr = eyeStats(zero, 1u);
    std::printf("    worldScale 0: left covered %zu/%zu, mean E/pi %.5f; right covered %zu/%zu, mean "
                "%.5f\n", zl.covered, zl.pixels, zl.meanE, zr.covered, zr.pixels, zr.meanE);
    CHECK_MSG(zl.covered > zl.pixels / 2u && zr.covered > zr.pixels / 2u,
              "BOTH EYES RECORD A PROBE GRID: %.1f %% and %.1f %% of each eye's pixels answered",
              100.0 * double(zl.covered) / double(zl.pixels),
              100.0 * double(zr.covered) / double(zr.pixels));
    {
        const HalfDiff d = compareHalves(zero);
        // THE BAR IS A COUNT AND A MEAN, NOT A WORST: the depth's last-bit noise
        // moves a value by a half-float ulp or two, and where it tips a
        // threshold — a plane test at its tolerance, the 6-bit coverage's
        // stochastic rounding — by one step of that quantity (up to 0.016 of
        // coverage, measured 0.010 once in four runs).
        const double bar = zl.meanE / 1000.0;
        CHECK_MSG(d.differ * 100u <= d.values && d.meanAbs <= bar,
                  "THE CONTROL: two eyes at one pose gather the SAME halves — %zu of %zu values "
                  "differ (bar 1 %%), by %.2e on average over every value (bar %.2e = 1/1000 of "
                  "the eye's mean E/pi), the worst %.4f: the depth's last-bit noise between the "
                  "halves, where it tips a threshold",
                  d.differ, d.values, d.meanAbs, bar, d.worstAbs);
    }
    CHECK_MSG(seamHolds(zl, zr),
              "NOTHING IS LOST AT THE SEAM: its bands answer %zu (left eye) and %zu (right eye) of "
              "%zu pixels against %zu and %zu at the same columns on the target's outer borders",
              zl.rightBand, zr.leftBand, zl.bandPixels, zr.rightBand, zl.leftBand);

    // ---- THE PARALLAX ARM: worldScale 1 --------------------------------------
    GatherStatus one;
    if (!session(engine.get(), scene, 1.0f, one)) return 1;
    const EyeStats ol = eyeStats(one, 0u), orr = eyeStats(one, 1u);
    std::printf("    worldScale 1: left covered %zu/%zu, mean E/pi %.5f; right covered %zu/%zu, "
                "mean %.5f\n",
                ol.covered, ol.pixels, ol.meanE, orr.covered, orr.pixels, orr.meanE);
    size_t differ1 = 0;
    {
        const unsigned w = one.irradianceW, eyeW = w / 2u;
        for (unsigned y = 0; y < one.irradianceH; ++y)
            for (unsigned x = 0; x < eyeW; ++x)
                if (one.irradiance[(size_t(y) * w + x) * 4u + 3u] !=
                        one.irradiance[(size_t(y) * w + x + eyeW) * 4u + 3u] ||
                    one.irradiance[(size_t(y) * w + x) * 4u] != one.irradiance[(size_t(y) * w + x + eyeW) * 4u])
                    ++differ1;
    }
    CHECK_MSG(differ1 > size_t(kEyeW) * kEyeH / 100u,
              "the eyes 6 cm apart gather DIFFERENT halves (%zu of %u pixels differ) — the "
              "parallax the control removes", differ1, kEyeW * kEyeH);
    CHECK_MSG(ol.covered > ol.pixels / 2u && orr.covered > orr.pixels / 2u,
              "both eyes answered with parallax: %.1f %% and %.1f %%",
              100.0 * double(ol.covered) / double(ol.pixels),
              100.0 * double(orr.covered) / double(orr.pixels));
    CHECK_MSG(seamHolds(ol, orr),
              "...and nothing is lost at the seam with parallax: %zu and %zu of %zu against %zu and %zu",
              ol.rightBand, orr.leftBand, ol.bandPixels, orr.rightBand, ol.leftBand);
    CHECK_MSG(ol.meanE > 0.0 && std::fabs(ol.meanE - orr.meanE) <= 0.05 * ol.meanE,
              "THE IRRADIANCE IS THE SURFACE'S, NOT THE EYE'S: the two eyes' mean E/pi %.5f and "
              "%.5f agree within 5 %% (%.2f %%)",
              ol.meanE, orr.meanE, ol.meanE > 0.0 ? 100.0 * std::fabs(ol.meanE - orr.meanE) / ol.meanE : 0.0);

    std::printf("%s (%d failure(s))\n", gFailures ? "FAILED" : "PASSED", gFailures);
    return gFailures ? 1 : 0;
}
