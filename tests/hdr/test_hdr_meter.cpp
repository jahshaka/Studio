// THE HISTOGRAM METER'S ARITHMETIC, MEASURED (EXPOSURE-2).
//
// The companion of hdr.drag_stable: that suite asserts the meter is STEADY,
// this one asserts it is RIGHT — in absolute terms, against numbers computed
// on paper from the frame the fixture renders.
//
// WHY IT CAN BE EXACT. The meter's answer is not a picture: it is the adapted
// luminance the tonemapper samples, `View::measuredExposureScale()`, and the
// resolve pass computes it as
//
//      newLum = 1024 * e^(E-2) / e^( clamp( measured, 7.5-maxEv, 7.5-minEv ) )
//
// so with the window open wide the measurement comes straight back out:
//
//      measured = ln( 1024 * e^(E-2) / newLum )
//
// and `measured` is a weighted mean of ln( 1024 * Y ) over the frame. A frame
// of ONE known radiance therefore has ONE right answer, to four figures.
//
// SETTLING IS COUNTED IN FRAMES AND THEN READ UNTIL IT STOPS MOVING, which is
// the law (CLAUDE.md: "a wall-clock settle measures nothing in this engine").
// The adaptation is a first-order filter at 2.284 % of the gap per frame on the
// engine's fixed 1/60 s clock, so `converge()` below renders until two
// consecutive reads agree to 1e-7 relative, and reports how many frames that
// took instead of guessing a number.
//
// THE FOUR PARTS
//   A  THE AXIS. A uniform frame of known radiance, average metering, no clips:
//      the meter must report ln( 1024 * Y ) exactly. This is the assertion that
//      would catch a wrong luminance vector, a wrong 1024, a missing floor, a
//      mis-bound texture or a bin-centre approximation.
//   B  A UNIFORM FRAME IS PATTERN-INDEPENDENT. Average, centre-weighted and
//      spot must all report the same number on it, because a weighted mean of
//      one value is that value whatever the weights. It is the control that
//      keeps part C from passing for the wrong reason.
//   C  THE PATTERN. A small bright patch IN THE CENTRE of an otherwise dim
//      frame must move the SPOT meter a long way and the AVERAGE meter barely
//      at all; the same patch in the CORNER must leave the spot meter where it
//      was and move the average meter by the same amount as the centre patch
//      did (the area it covers is what average sees, and that has not changed).
//   D  THE PERCENTILE CLIPS, PREDICTED. With two known radiances in the frame
//      the average reading with no clips GIVES the covered fraction f:
//          measured = f * L_bright + (1-f) * L_dim   ->   f
//      and a 10/90 clip on a two-value distribution then has a closed form (a
//      cumulative walk over two bins, done below in C++). The shader must agree
//      with it. That is a hand-computed expectation, not a regression pin.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                           std::printf("\n"); ++failures; } \
                           else { std::printf("  ok: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {

Engine *gEngine = nullptr;
View   *gView = nullptr;

// The window, wide open: the measurement must come back unclamped.
constexpr float kExposure = 0.0f, kMinEv = -12.0f, kMaxEv = 12.0f;
// 1024 * e^(E-2), the pin's own scale (HdrUtils::setExposure).
float exposureScaleTerm() { return 1024.0f * std::exp(kExposure - 2.0f); }

/// ln( 1024 * Y ) for a linear-RGB radiance, with the meter's own luminance
/// vector and its floor. The paper answer.
double logLumOf(float r, float g, float b) {
    double y = 0.2125 * double(r) + 0.7154 * double(g) + 0.0721 * double(b);
    if (y < 1e-4) y = 1e-4;                 // c_minLuminance, before the log
    return std::log(y * 1024.0);
}

/// Render until the adapted luminance stops moving, then return the MEASUREMENT
/// it implies. Frames, never a clock.
double converge(const char *what) {
    double prev = -1.0;
    int frames = 0;
    for (int i = 0; i < 4000; ++i) {
        gEngine->renderOneFrame();
        ++frames;
        const double lum = double(gView->measuredExposureScale());
        if (lum > 0.0 && prev > 0.0 && std::fabs(lum - prev) <= prev * 1e-7) break;
        prev = lum;
    }
    const double lum = double(gView->measuredExposureScale());
    if (!(lum > 0.0)) {
        std::printf("FAIL: %s: the chain reported no adapted luminance at all\n", what);
        ++failures;
        return 0.0;
    }
    const double measured = std::log(double(exposureScaleTerm()) / lum);
    std::printf("   [%s] %d frames, adapted luminance %.6f, measurement %.5f\n",
                what, frames, lum, measured);
    return measured;
}

double meterWith(ExposureMeterPattern pattern, float low, float high, const char *what) {
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.hdr = true;
    fx.exposure = kExposure; fx.exposureMin = kMinEv; fx.exposureMax = kMaxEv;
    fx.meterPattern = pattern;
    fx.meterLowPercent = low; fx.meterHighPercent = high;
    gView->setPostFx(fx);
    // A pattern or clip change is a UNIFORM, so the graph does not move and the
    // adaptation history survives: converge() walks from wherever the last arm
    // left it, which is also the honest way to read this dial in the editor.
    return converge(what);
}

/// The percentile walk of the resolve pass, for a distribution of exactly two
/// values. Closed form, in C++, from the fraction of the metered weight each
/// value holds. `lo`/`hi` are fractions.
double twoValueClip(double fBright, double lBright, double lDim, double lo, double hi) {
    // Darkest first, which is the order the shader walks the bins in.
    struct Bin { double w, l; } bins[2] = { { 1.0 - fBright, lDim }, { fBright, lBright } };
    double cum = 0.0, wSum = 0.0, lSum = 0.0;
    for (const Bin &b : bins) {
        const double a = std::max(cum, lo), c = std::min(cum + b.w, hi);
        if (c > a) { wSum += c - a; lSum += (c - a) * b.l; }
        cum += b.w;
    }
    return wSum > 0.0 ? lSum / wSum : 0.0;
}

}   // namespace

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-hdr-meter-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    gEngine = engine.get();

    // SQUARE, so the metering geometry's aspect correction is the identity and
    // every number below can be reasoned about on paper. 512 px gives the meter
    // 128x128 = 16,384 samples at its one-per-4x4-pixels grid.
    const unsigned kSide = 512;
    // THE DIM BASE: the view's background, which the HDR chain clears rt0 to, so
    // it is EXACTLY this radiance with no shading in it at all.
    const Colour kDim(0.02f, 0.02f, 0.02f);
    gView = engine->createOffscreenView("meter", kSide, kSide, kDim);
    Scene *s = engine->createScene("meter");
    if (!gView || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    gView->setScene(s);
    // NOTHING BUT THE FIXTURE'S OWN RADIANCE: no lights, no ambient, no shadows.
    // The patch below is EMISSIVE with a black albedo, so its pixels are its
    // emissive radiance and nothing else.
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    gView->setShadows(false);
    enginetest::testCameraAt(gView, Vec3(0.0f, 0.0f, 10.0f));   // looking down -Z

    const double lDim = logLumOf(kDim.r, kDim.g, kDim.b);

    // ---- A  THE AXIS -----------------------------------------------------
    {
        const double got = meterWith(ExposureMeterPattern::Average, 0.0f, 100.0f,
                                     "A uniform, average, no clips");
        CHECK(std::fabs(got - lDim) < 0.01,
              "A: a uniform frame of radiance %.3f measures ln(1024*Y) = %.5f (got %.5f)",
              double(kDim.g), lDim, got);
    }

    // ---- B  A UNIFORM FRAME IS PATTERN-INDEPENDENT -----------------------
    {
        const double centre = meterWith(ExposureMeterPattern::CentreWeighted, 0.0f, 100.0f,
                                        "B uniform, centre-weighted");
        const double spot   = meterWith(ExposureMeterPattern::Spot, 0.0f, 100.0f,
                                        "B uniform, spot");
        CHECK(std::fabs(centre - lDim) < 0.01 && std::fabs(spot - lDim) < 0.01,
              "B: every pattern reports the same number on a uniform frame "
              "(average %.5f, centre-weighted %.5f, spot %.5f)", lDim, centre, spot);
        // ...and the clips cannot move it either: one value is every percentile.
        const double clipped = meterWith(ExposureMeterPattern::Average, 10.0f, 90.0f,
                                         "B uniform, average, 10/90");
        CHECK(std::fabs(clipped - lDim) < 0.01,
              "B: and clipping 10/90 of ONE value is still that value (%.5f)", clipped);
    }

    // ---- THE BRIGHT PATCH ------------------------------------------------
    // A small emissive square. At 45 deg vertical fov and 10 units away the
    // visible half-height is 10*tan(22.5 deg) = 4.1421, so a side of 1.0 world
    // unit is 12.1 % of the frame height — inside the spot's disc (whose
    // half-weight radius is sqrt(0.025*4/pi) = 17.8 % of the half-height, i.e.
    // 8.9 % of the frame height) and about 1.5 % of the frame's area.
    const Colour kBright(8.0f, 8.0f, 8.0f);
    const double lBright = logLumOf(kBright.r, kBright.g, kBright.b);
    const NodeId patch = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0, 0, 0);
        p.emissive = kBright;
        p.roughness = 1.0f;
        p.metalness = 0.0f;
        s->attachMesh(patch, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(p));
        enginetest::setNodeScale(s, patch, Vec3(1.0f, 1.0f, 0.02f));
    }

    double fCentre = 0.0;
    // ---- C  THE PATTERN --------------------------------------------------
    {
        enginetest::setNodePosition(s, patch, Vec3(0.0f, 0.0f, 0.0f));
        const double avgCentre = meterWith(ExposureMeterPattern::Average, 0.0f, 100.0f,
                                           "C centre patch, average");
        const double spotCentre = meterWith(ExposureMeterPattern::Spot, 0.0f, 100.0f,
                                            "C centre patch, spot");
        // The fraction of the frame the patch covers, read off the AVERAGE
        // meter: measured = f*lBright + (1-f)*lDim.
        fCentre = (avgCentre - lDim) / (lBright - lDim);
        std::printf("   [C] the patch covers %.3f %% of the frame (from the average meter)\n",
                    fCentre * 100.0);
        CHECK(fCentre > 0.005 && fCentre < 0.05,
              "C: the patch is a small minority of the frame (%.2f %%)", fCentre * 100.0);
        // A MINORITY THAT AVERAGE BARELY SEES...
        CHECK(avgCentre - lDim < 0.25 * (lBright - lDim),
              "C: the average meter barely moves for it (%.5f, dim %.5f, bright %.5f)",
              avgCentre, lDim, lBright);
        // ...AND THAT THE SPOT METER IS MOSTLY LOOKING AT.
        CHECK(spotCentre - lDim > 0.40 * (lBright - lDim),
              "C: the SPOT meter moves a long way for it (%.5f, i.e. %.1f %% of the way "
              "from dim to bright, against average's %.1f %%)",
              spotCentre, (spotCentre - lDim) / (lBright - lDim) * 100.0,
              (avgCentre - lDim) / (lBright - lDim) * 100.0);
        // The classic pattern sits between the two, by construction.
        const double cw = meterWith(ExposureMeterPattern::CentreWeighted, 0.0f, 100.0f,
                                    "C centre patch, centre-weighted");
        CHECK(cw > avgCentre + 0.05 && cw < spotCentre,
              "C: centre-weighted sits between average and spot (%.5f vs %.5f and %.5f)",
              cw, avgCentre, spotCentre);

        // THE SAME PATCH IN THE CORNER. 3.1 of the 4.1421 half-extents out on
        // both axes: still fully on screen, nowhere near the centre.
        enginetest::setNodePosition(s, patch, Vec3(3.1f, 3.1f, 0.0f));
        const double avgEdge = meterWith(ExposureMeterPattern::Average, 0.0f, 100.0f,
                                         "C corner patch, average");
        const double spotEdge = meterWith(ExposureMeterPattern::Spot, 0.0f, 100.0f,
                                          "C corner patch, spot");
        CHECK(std::fabs(avgEdge - avgCentre) < 0.15,
              "C: MOVING it does not change what AVERAGE sees (%.5f vs %.5f)",
              avgEdge, avgCentre);
        CHECK(std::fabs(spotEdge - lDim) < 0.02,
              "C: and the SPOT meter no longer sees it at all (%.5f, dim %.5f)",
              spotEdge, lDim);
        CHECK(spotCentre - spotEdge > 0.40 * (lBright - lDim),
              "C: the spot meter's whole point, in one number: centre %.5f, corner %.5f",
              spotCentre, spotEdge);
    }

    // ---- D  THE PERCENTILE CLIPS, PREDICTED ------------------------------
    {
        enginetest::setNodePosition(s, patch, Vec3(0.0f, 0.0f, 0.0f));
        const double avg = meterWith(ExposureMeterPattern::Average, 0.0f, 100.0f,
                                     "D centre patch, average, no clips");
        const double f = (avg - lDim) / (lBright - lDim);
        struct Case { float lo, hi; };
        const Case cases[] = { { 10.0f, 90.0f }, { 0.0f, 90.0f }, { 50.0f, 100.0f },
                               { 45.0f, 55.0f } };
        for (const Case &c : cases) {
            char what[64];
            std::snprintf(what, sizeof(what), "D average, %.0f/%.0f", double(c.lo), double(c.hi));
            const double got = meterWith(ExposureMeterPattern::Average, c.lo, c.hi, what);
            const double want = twoValueClip(f, lBright, lDim,
                                             double(c.lo) * 0.01, double(c.hi) * 0.01);
            CHECK(std::fabs(got - want) < 0.05,
                  "D: clipping %.0f/%.0f of a two-value frame measures %.5f, and the "
                  "cumulative walk over those two values says %.5f",
                  double(c.lo), double(c.hi), got, want);
        }
        // AND THE HEADLINE: a bright minority is what the top clip is FOR.
        const double clipped = meterWith(ExposureMeterPattern::Average, 0.0f, 90.0f,
                                         "D the bright tail, cut");
        CHECK(clipped < avg - 0.05,
              "D: cutting the brightest tenth takes the bright patch out of the "
              "measurement (%.5f against %.5f with nothing cut)", clipped, avg);
    }

    gView->setPostFx(PostFxDesc());
    engine->destroyView(gView);
    engine->destroyScene(s);
    std::printf("\n%s\n", failures ? "FAILURES" : "ALL OK");
    return failures ? 1 : 0;
}
