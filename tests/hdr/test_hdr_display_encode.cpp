// hdr.display_encode — EXACTLY ONE sRGB ENCODE ON EVERY DISPLAY PATH (SRGB-ENCODE-1).
//
// THE DEFECT. Nothing in the chain applied the sRGB OETF: the film curve's
// LINEAR output (and the ungraded scene's linear radiance) reached plain UNORM
// targets as codes, so an 18 % card at 0.18 displayed as code 46 where every
// sRGB display and every reference renderer shows 118 — ~2.7 stops dark at mid
// grey (the owner's Unreal comparison).
//
// THE FIXTURE is a flat EMISSIVE plane filling the frame, black albedo, no
// light and no ambient, so the radiance at every pixel IS the emissive value —
// a number this file chooses. Each arm renders a ramp of such values and
// compares the bytes with the curve computed here on paper:
//
//   A  THE PLAIN INSTRUMENT (an offscreen view that did not opt into the post
//      chain) stays LINEAR by contract: code = 255 E within one code. Nothing
//      in this lane may move the measuring instrument every pixel suite reads.
//   B  THE UNGRADED DISPLAY PATH, passthrough shape (allowOffscreen, HDR off,
//      no effect): code = 255 OETF(E) within one code — the exact piecewise
//      curve, and 0.18 lands on 118.
//   C  the same through the POST CHAIN's ungraded composite (SMAA forces the
//      post shape with HDR off): the same bytes as B — a float scene target, one
//      encode.
//   D  THE GRADED PATH (HDR, a fixed exposure of exactly 1): code =
//      255 OETF(film(E)) within one code, dither on; and the grey card — the
//      tonemapper input that develops to 0.18 — reads 118 (the brief: +-3; this
//      holds it to +-1). A second encode would put it at ~181; none puts it at 46.
//
// Every arm reports its worst error, so a failure says which curve the bytes
// are really on.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                           std::printf("\n"); ++failures; } \
                           else { std::printf("  ok: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {

constexpr unsigned kSide = 64;

Engine   *gEngine = nullptr;
View     *gView = nullptr;
Scene    *gScene = nullptr;
MaterialId gMat = 0;

MeshData planeMesh(float half)
{
    MeshData d;
    const float p[4][3] = { {-half, -half, 0.0f}, { half, -half, 0.0f},
                            { half,  half, 0.0f}, {-half,  half, 0.0f} };
    for (int i = 0; i < 4; ++i) {
        d.positions.insert(d.positions.end(), { p[i][0], p[i][1], p[i][2] });
        d.normals.insert(d.normals.end(), { 0.0f, 0.0f, 1.0f });
    }
    d.indices = { 0, 1, 2, 0, 2, 3 };
    return d;
}

PbrParams emitter(float e)
{
    PbrParams p;
    p.albedo = Colour(0.0f, 0.0f, 0.0f);
    p.metalness = 0.0f;
    p.roughness = 1.0f;
    p.emissive = Colour(e, e, e);
    return p;
}

double oetf(double v)                  // the exact piecewise sRGB encode
{
    v = std::min(std::max(v, 0.0), 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

constexpr double kW = 11.2;
double filmic(double x)
{
    const double A = 0.22, B = 0.3, C = 0.10, D = 0.20, E = 0.01, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
double toneCurve(double x)             // FinalToneMapping's LINEAR output, before the encode
{
    return (filmic(x) / filmic(kW) - 0.5) * 1.25 + 0.5 + 0.11;
}
double inputFor(double value)          // the tonemapper input that develops to `value`
{
    double lo = 0.0, hi = kW;
    for (int i = 0; i < 80; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (toneCurve(mid) < value) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

/// Mean green code over the central quarter of the frame (the fixture is flat).
bool shoot(float e, double &code)
{
    if (!gScene->setPbrMaterial(gMat, emitter(e))) return false;
    for (int i = 0; i < 6; ++i) gEngine->renderOneFrame();
    Image img;
    if (!gView->readPixels(img) || img.width != kSide || img.height != kSide) return false;
    double sum = 0.0; int n = 0;
    for (unsigned y = kSide / 4; y < 3 * kSide / 4; ++y)
        for (unsigned x = kSide / 4; x < 3 * kSide / 4; ++x) {
            sum += img.rgba[(size_t(y) * kSide + x) * 4 + 1];
            ++n;
        }
    code = sum / n;
    return true;
}

void setFx(bool allowOffscreen, bool hdr, int smaa)
{
    PostFxDesc fx;
    fx.allowOffscreen = allowOffscreen;
    fx.hdr = hdr;
    fx.tonemapFixed = true;
    fx.exposureScale = 1.0f;           // the tonemapper's input IS the radiance
    fx.bloom = false;
    fx.smaaPreset = smaa;
    gView->setPostFx(fx);
}

/// Renders the ramp and returns the worst |code - expected(E)|.
template <class F>
double ramp(const char *arm, const std::vector<float> &values, F expected)
{
    double worst = 0.0;
    for (float e : values) {
        double code = -1.0;
        if (!shoot(e, code)) { std::printf("FAIL: %s: render at %.3f: %s\n", arm, e,
                                           gEngine->lastError().c_str()); ++failures; return 999.0; }
        const double want = expected(double(e));
        std::printf("   [%s] E %.4f -> code %.2f (curve %.2f)\n", arm, e, code, want);
        worst = std::max(worst, std::fabs(code - want));
    }
    return worst;
}

}   // namespace

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-hdr-display-encode-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    gEngine = engine.get();

    gView = engine->createOffscreenView("encode", kSide, kSide, Colour(0, 0, 0));
    gScene = engine->createScene("encode");
    if (!gView || !gScene) { std::printf("FAIL: view/scene\n"); return 1; }
    gView->setScene(gScene);
    gScene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    gView->setShadows(false);
    {
        const NodeId n = gScene->createNode();
        const MeshId m = gScene->createMesh(planeMesh(40.0f));
        gMat = gScene->createPbrMaterial(emitter(0.18f));
        if (!n || !m || !gMat || !gScene->attachMesh(n, m, gMat)) {
            std::printf("FAIL: fixture: %s\n", engine->lastError().c_str());
            return 1;
        }
    }
    enginetest::testCameraAt(gView, Vec3(0.0f, 0.0f, 28.0f));

    const std::vector<float> values = { 0.0f, 0.002f, 0.01f, 0.05f, 0.1f, 0.18f, 0.3f,
                                        0.5f, 0.75f, 1.0f };

    // ---- A: the Plain instrument stays linear -----------------------------
    setFx(false, false, -1);
    const double wa = ramp("A plain", values, [](double e) { return 255.0 * e; });
    CHECK(wa <= 1.0, "A: the PLAIN instrument is linear radiance, un-encoded (worst %.2f codes)", wa);

    // ---- B: the ungraded display path, passthrough shape ------------------
    setFx(true, false, -1);
    const double wb = ramp("B ungraded", values, [](double e) { return 255.0 * oetf(e); });
    CHECK(wb <= 1.0, "B: the UNGRADED display picture is the sRGB OETF of the radiance "
                     "(worst %.2f codes)", wb);
    {
        double code = 0.0;
        CHECK(shoot(0.18f, code) && std::fabs(code - 118.0) <= 1.0,
              "B: an 18%% linear value displays at 118 (%.2f)", code);
    }

    // ---- C: the same through the post chain's ungraded composite ----------
    setFx(true, false, 1);
    const double wc = ramp("C ungraded+smaa", values, [](double e) { return 255.0 * oetf(e); });
    CHECK(wc <= 1.0, "C: the post chain's ungraded composite encodes once, from a float "
                     "scene (worst %.2f codes)", wc);

    // ---- D: the graded path ------------------------------------------------
    setFx(true, true, -1);
    const double wd = ramp("D graded", values,
                           [](double e) { return 255.0 * oetf(toneCurve(e)); });
    CHECK(wd <= 1.0, "D: the GRADED picture is the OETF of the film curve's output "
                     "(worst %.2f codes)", wd);
    {
        const double xStar = inputFor(0.18);
        double code = 0.0;
        const bool ok = shoot(float(xStar), code);
        CHECK(ok && std::fabs(code - 118.0) <= 1.0,
              "D: the GREY CARD (film input %.5f, developing to 0.18) reads 118 (%.2f; "
              "unencoded would be 46, twice encoded ~%.0f)", xStar, code,
              255.0 * oetf(oetf(0.18)));
    }

    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
