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
//   E  THE FILM CURVE (IMAGE-1, Unreal's filmic): nine exposures -4..+4 stops
//      of the card against the reference; +4 stops below 250, -4 above 0.
//   F  THE IMAGE BLOCK: neutral at its defaults; contrast, shadows,
//      highlights and the film slope all leave the grey card where it is.
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

// THE FILM CURVE (IMAGE-1): Unreal Engine's filmic tonemapper (4.15+,
// TonemapCommon.ush FilmToneMap) on a grey at its five defaults, spelled out
// here again rather than shared, so a curve edit in either place fails here.
// iris::lens::filmCurve (irisgl cameralens.cpp) is the third copy and
// tests/cameras holds it to this one.
double toneCurve(double x)             // FinalToneMapping's LINEAR output, before the encode
{
    const double slope = 0.88, toe = 0.55, shoulder = 0.26, black = 0.0, white = 0.04;
    const double toeScale = 1.0 + black - toe, shoulderScale = 1.0 + white - shoulder;
    const double bt = (0.18 + black) / toeScale - 1.0;
    const double toeMatch = std::log10(0.18) - 0.5 * std::log((1.0 + bt) / (1.0 - bt)) * (toeScale / slope);
    const double straightMatch = (1.0 - toe) / slope - toeMatch;
    const double shoulderMatch = shoulder / slope - straightMatch;
    const double lc = std::log10(std::max(x, 1e-10));
    const double straight = slope * (lc + straightMatch);
    double toeC = -black + 2.0 * toeScale / (1.0 + std::exp((-2.0 * slope / toeScale) * (lc - toeMatch)));
    double shC = (1.0 + white) - 2.0 * shoulderScale / (1.0 + std::exp((2.0 * slope / shoulderScale) * (lc - shoulderMatch)));
    toeC = lc < toeMatch ? toeC : straight;
    shC = lc > shoulderMatch ? shC : straight;
    double t = std::min(std::max((lc - toeMatch) / (shoulderMatch - toeMatch), 0.0), 1.0);
    if (shoulderMatch < toeMatch) t = 1.0 - t;
    t = (3.0 - 2.0 * t) * t * t;
    return std::max(0.0, toeC + (shC - toeC) * t);
}
double inputFor(double value)          // the tonemapper input that develops to `value`
{
    double lo = -6.0, hi = 3.0;        // log10 of the input
    for (int i = 0; i < 80; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (toneCurve(std::pow(10.0, mid)) < value) lo = mid; else hi = mid;
    }
    return std::pow(10.0, 0.5 * (lo + hi));
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
        CHECK(std::fabs(xStar - 0.18) < 1e-4,
              "D: the curve maps the grey card to itself (input %.6f displays as 0.18)", xStar);
    }

    // ---- E: THE TONEMAP ARM (IMAGE-1) --------------------------------------
    // Nine exposures of the grey card, -4 to +4 stops, against the reference;
    // the highlight four stops over the card is NOT clipped and the shadow four
    // stops under it is not crushed (the curve this replaced clipped to white
    // 3.68 stops over the card).
    {
        double worst = 0.0, over4 = -1.0, under4 = -1.0;
        for (int st = -4; st <= 4; ++st) {
            const double e = 0.18 * std::pow(2.0, st);
            double code = -1.0;
            if (!shoot(float(e), code)) { CHECK(false, "E: render at %+d stops", st); break; }
            const double want = 255.0 * oetf(toneCurve(e));
            std::printf("   [E film] %+d stops (E %.5f) -> code %.2f (reference %.2f)\n", st, e,
                        code, want);
            worst = std::max(worst, std::fabs(code - want));
            if (st == 4) over4 = code;
            if (st == -4) under4 = code;
        }
        CHECK(worst <= 1.0, "E: the curve matches the reference at nine exposures (worst %.2f codes)",
              worst);
        CHECK(over4 >= 0.0 && over4 < 250.0, "E: +4 stops over the card is not clipped (%.2f < 250)",
              over4);
        CHECK(under4 > 0.5, "E: -4 stops under the card is not crushed to black (%.2f)", under4);
    }

    // ---- F: THE IMAGE BLOCK at its defaults is the curve above, and each of
    // its fields reaches the picture in the direction it names (IMAGE-1). A grey
    // emitter at the card, so the curve's input is known.
    {
        PostFxDesc fx;
        fx.allowOffscreen = true; fx.hdr = true; fx.tonemapFixed = true;
        fx.exposureScale = 1.0f; fx.bloom = false;
        const auto shootWith = [&](const ImageGrade &g, double &code) {
            fx.image = g;
            gView->setPostFx(fx);
            return shoot(0.18f, code);
        };
        double base = 0.0, c = 0.0;
        CHECK(shootWith(ImageGrade(), base) && std::fabs(base - 118.0) <= 1.0,
              "F: the default image block is the neutral curve (card %.2f)", base);
        ImageGrade g;
        g.highlights = 2.0f;           // the card sits below the highlight mask
        CHECK(shootWith(g, c) && std::fabs(c - base) <= 1.0,
              "F: highlights leave the grey card alone (%.2f vs %.2f)", c, base);
        g = ImageGrade(); g.shadows = 2.0f;   // ...and the shadow mask
        CHECK(shootWith(g, c) && std::fabs(c - base) <= 1.0,
              "F: shadows leave the grey card alone (%.2f vs %.2f)", c, base);
        g = ImageGrade(); g.contrast = 1.5f;  // contrast pivots ON the card
        CHECK(shootWith(g, c) && std::fabs(c - base) <= 1.0,
              "F: contrast pivots on the grey card (%.2f vs %.2f)", c, base);
        g = ImageGrade(); g.filmSlope = 1.2f; // 0.18 stays 0.18 for any film
        CHECK(shootWith(g, c) && std::fabs(c - base) <= 1.0,
              "F: a steeper film still develops the card to 0.18 (%.2f vs %.2f)", c, base);
        fx.image = ImageGrade();
        gView->setPostFx(fx);
    }

    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
