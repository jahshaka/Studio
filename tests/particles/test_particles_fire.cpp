// particles.fire_shape — the fire gate (PARTICLES_FX2_SPEC.md §11 phase 3).
//
// The owner's ask was "real particle fire, not textures". That is not a claim a
// green suite can make by counting bright pixels: an orange sticker is bright
// too. So this suite asserts the SHAPE of a flame, statistically, from the
// SHIPPING recipe — it builds an iris::ParticleSystemNode, calls
// applyPreset(Fire), mirrors it, and looks at what the engine actually drew.
// If someone detunes the preset, this is what notices.
//
// Six assertions, all population-level, because emission is seeded from a
// process-wide LCG and the affectors run on worker threads — there is no
// pixel-exact answer to compare against (spec §10.4):
//
//   1. it EXISTS      the flame column is lit and the corners are black
//                     (nothing sprays sideways)
//   2. it is FLAME-SHAPED   the body sits in the lower half, luminance falls
//                     monotonically from it to the tip, body/tip > 2
//   3. it TAPERS      fill density peaks below the tip and falls from there
//   4. it is FIRE-COLOURED  mean R > G > B over the lit pixels, R/B > 3
//   5. it MOVES       two captures 30 frames apart differ across the plume
//   6. it STOPS       with the clock at 0, two captures are IDENTICAL
//
// Tolerances come from the first green run and are recorded inline, the way the
// GI suites do it. Two of them were rewritten by what the first runs MEASURED,
// and both notes are worth more than the assertions:
//
//   * a flame's brightest band is not its lowest. The scale ramp starts a
//     particle small, grows it, then shrinks it, and particles rise as they age
//     — so fire has a narrow root, a bulge and a taper. A strict base-to-tip
//     luminance decrease failed on a flame that was correct.
//   * a flame's OUTLINE barely tapers at all (18/22/23/22 px here). At any
//     single-pixel threshold the width is set by the furthest straggler, and
//     turbulence keeps flinging those outwards. What tapers is FILL DENSITY.
#include <QGuiApplication>
#include <QColor>
#include <QImage>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/assets/texture2d.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "irisgl/mirror/scenemirror.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

constexpr unsigned kSize = 128;
constexpr float    kLit  = 0.08f;    // sum of channels above which a pixel counts as flame

float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

/// An accumulated capture: the RNG makes any single frame noisy, so every
/// measurement below is the mean of N frames.
struct Average {
    std::vector<Colour> px;
    unsigned n = 0;
    void add(const Image &im) {
        if (px.empty()) px.assign(size_t(im.width) * im.height, Colour(0, 0, 0, 1));
        for (unsigned y = 0; y < im.height; ++y)
            for (unsigned x = 0; x < im.width; ++x) {
                const Colour c = im.at(x, y);
                Colour &acc = px[size_t(y) * im.width + x];
                acc.r += c.r; acc.g += c.g; acc.b += c.b;
            }
        ++n;
    }
    Colour at(unsigned x, unsigned y) const {
        const Colour &c = px[size_t(y) * kSize + x];
        return Colour(c.r / float(n), c.g / float(n), c.b / float(n), 1.0f);
    }
};

}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_particles_fire-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("fire", kSize, kSize, Colour(0, 0, 0));
    Scene *target = engine->createScene("fire");
    CHECK(view && target, "offscreen view + engine scene");
    if (!view || !target) return 1;
    view->setScene(target);
    // The flame reaches ~2.5 m on a typical particle and over 4 m on a fast,
    // long-lived one (velocity up to 2.9 m/s for up to 1.2 s, plus buoyancy).
    // Frame all of it: a plume clipped by the top of the image has no tip to
    // measure, and the first run of this gate found exactly that.
    enginetest::testCameraLookAt(view, Vec3(0, 2.0f, 6.5f), Vec3(0, 2.0f, 0));
    engine->setFixedFrameDelta(1.0f / 60.0f);

    // ---- the SHIPPING recipe, through the document and the mirror ----------
    // WITH ITS TEXTURE, and the first run is why. An untextured particle is a
    // solid opaque quad; a hundred of them blended additively saturate to a
    // white slab with no edges, no taper and R/B barely above 2 — flat enough
    // that every shape assertion below fails on a fire that looks fine in the
    // app. A soft radial alpha blob is what fire is made of in every engine
    // that is not raymarching a volume, and it is what the sample ships. So the
    // gate uses one, written to disk so it travels the REAL path the sample
    // does (document Texture2D -> mirror textureFor -> engine loadTexture).
    const std::string blobPath = std::string(JAHSHAKA_TEST_MEDIA_DIR) + "../fire_blob.png";
    {
        const int N = 64;
        QImage blob(N, N, QImage::Format_RGBA8888);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const float dx = (x + 0.5f) / N - 0.5f, dy = (y + 0.5f) / N - 0.5f;
                const float d = std::sqrt(dx * dx + dy * dy) * 2.0f;
                // Smootherstep falloff: opaque core, feathered rim, nothing at
                // the corners (a hard rim reads as a square at any distance).
                const float t = std::max(0.0f, std::min(1.0f, 1.0f - d));
                const float a = t * t * (3.0f - 2.0f * t);
                blob.setPixelColor(x, y, QColor(255, 255, 255, int(a * 255.0f)));
            }
        blob.save(QString::fromStdString(blobPath), "PNG");
    }

    auto doc = iris::Scene::create();
    auto fire = iris::ParticleSystemNode::create();
    fire->setName("Fire");
    fire->applyPreset(iris::ParticlePreset::Fire);
    fire->texture = iris::Texture2D::load(QString::fromStdString(blobPath));
    CHECK(!!fire->texture, "the fire's particle image loaded");
    doc->getRootNode()->addChild(fire);

    SceneMirror mirror(target);
    mirror.setSource(doc);
    CHECK(mirror.sync() == 1, "the fire emitter mirrored");
    const NodeId eng = mirror.engineNode(fire.data());
    CHECK(eng != 0, "it has an engine node");

    // Warm up: the plume has to fill before its SHAPE means anything. 120 frames
    // at 1/60 is 2 s, comfortably past the 0.95 s +/- 0.25 particle lifetime.
    for (int i = 0; i < 120; ++i) engine->renderOneFrame();
    std::printf("    warmed up: %u live particles\n", target->particleCount(eng));
    CHECK(target->particleCount(eng) > 20, "the flame is populated");

    // Ten captures, averaged, to beat the RNG.
    Average avg;
    Image img;
    for (int i = 0; i < 10; ++i) {
        engine->renderOneFrame();
        view->readPixels(img);
        avg.add(img);
    }

    // ---- 1. it EXISTS, and only where a flame belongs ----------------------
    // The column: the middle third horizontally, the lower two thirds
    // vertically (image y grows DOWN, so the flame base is at the bottom).
    const unsigned cx0 = kSize / 3, cx1 = kSize * 2 / 3;
    const unsigned cy0 = kSize / 5, cy1 = kSize;
    double columnLum = 0; int columnPx = 0, columnLit = 0;
    for (unsigned y = cy0; y < cy1; ++y)
        for (unsigned x = cx0; x < cx1; ++x) {
            const Colour c = avg.at(x, y);
            columnLum += lum(c); ++columnPx;
            if (c.r + c.g + c.b > kLit) ++columnLit;
        }
    const double meanColumn = columnLum / std::max(1, columnPx);
    std::printf("    column mean luminance %.4f, %d/%d lit\n", meanColumn, columnLit, columnPx);
    CHECK(meanColumn > 0.01, "1a. the flame column is lit");
    CHECK(columnLit > 200, "1b. and it is a plume, not a speck");

    int cornerLit = 0;
    const unsigned tile = kSize / 8;
    for (int cy = 0; cy < 2; ++cy)
        for (int cx = 0; cx < 2; ++cx)
            for (unsigned y = 0; y < tile; ++y)
                for (unsigned x = 0; x < tile; ++x) {
                    const unsigned px = cx ? kSize - 1 - x : x;
                    const unsigned py = cy ? kSize - 1 - y : y;
                    const Colour c = avg.at(px, py);
                    if (c.r + c.g + c.b > kLit) ++cornerLit;
                }
    std::printf("    corner tiles: %d lit pixels\n", cornerLit);
    CHECK(cornerLit == 0, "1c. nothing sprays into the corners");

    // ---- 2 + 3. FLAME SHAPE: banded luminance and taper --------------------
    // Four horizontal bands over the FLAME'S OWN vertical extent, not over the
    // frame: the assertion is about the shape of the plume, and hard-coded bands
    // measure the camera framing as much as the fire. Band 0 is the BASE, which
    // in image coordinates is the LARGEST y (y grows downwards).
    //
    // The colour ramp runs bright-warm at birth to near-black at death, and
    // particles rise as they age, so a flame is bright and wide at the bottom
    // and dim and narrow at the top. A sticker is flat in both.
    unsigned plumeTop = kSize, plumeBottom = 0;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = cx0; x < cx1; ++x)
            if (avg.at(x, y).r + avg.at(x, y).g + avg.at(x, y).b > kLit) {
                plumeTop = std::min(plumeTop, y);
                plumeBottom = std::max(plumeBottom, y);
            }
    std::printf("    plume spans image rows %u..%u (%u tall)\n",
                plumeTop, plumeBottom, plumeBottom - plumeTop + 1);
    CHECK(plumeBottom > plumeTop + 20, "2. the plume is tall enough to have a shape");

    double bandLum[4] = { 0, 0, 0, 0 };
    int    bandLit[4] = { 0, 0, 0, 0 };
    int    bandWidth[4] = { 0, 0, 0, 0 };
    const unsigned span = plumeBottom - plumeTop + 1;
    for (int b = 0; b < 4; ++b) {
        const unsigned y1 = plumeBottom + 1 - unsigned(double(b) * span / 4.0);
        const unsigned y0 = plumeBottom + 1 - unsigned(double(b + 1) * span / 4.0);
        unsigned minX = kSize, maxX = 0;
        for (unsigned y = y0; y < y1; ++y)
            for (unsigned x = cx0; x < cx1; ++x) {
                const Colour c = avg.at(x, y);
                if (c.r + c.g + c.b > kLit) {
                    bandLum[b] += lum(c); ++bandLit[b];
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                }
            }
        bandWidth[b] = (maxX >= minX) ? int(maxX - minX + 1) : 0;
    }
    for (int b = 0; b < 4; ++b)
        std::printf("    band %d (base->tip): %d lit, %d px wide, total luminance %.3f, mean %.4f\n",
                    b, bandLit[b], bandWidth[b], bandLum[b],
                    bandLit[b] ? bandLum[b] / bandLit[b] : 0.0);

    // The brightest band is the FLAME BODY, and it is not the very bottom row:
    // the scale ramp starts particles small (0.6), grows them to full at 35% of
    // life and shrinks them after, and they rise as they age — so a flame has a
    // narrow root, a bulge, and a taper. That is what fire looks like, and the
    // first run of this gate is what taught it (a strict base-to-tip decrease
    // failed on a flame that was correct). What must hold is that the body sits
    // in the LOWER HALF and everything above it falls away.
    int peak = 0;
    for (int b = 1; b < 4; ++b) if (bandLum[b] > bandLum[peak]) peak = b;
    std::printf("    brightest band: %d\n", peak);
    CHECK(peak <= 1, "2a. the flame's body is in its lower half, not floating at the top");

    bool fallsToTip = true;
    for (int b = peak; b < 3; ++b)
        if (!(bandLum[b] > bandLum[b + 1])) fallsToTip = false;
    CHECK(fallsToTip, "2b. luminance falls monotonically from the body to the tip");
    const double ratio = bandLum[3] > 0 ? bandLum[peak] / bandLum[3] : 1e9;
    std::printf("    body/tip luminance ratio %.2f (measured 11.4 on the first green run)\n", ratio);
    CHECK(ratio > 2.0, "2c. the body is at least twice the tip (a sticker is flat)");

    // TAPER, measured as FILL DENSITY rather than as outline width — and the
    // difference is a finding, not a convenience. The lit OUTLINE of this flame
    // is 18/22/23/22 px across its four bands: nearly a straight column, because
    // at any single-pixel threshold the width is set by the one furthest
    // straggler and turbulence keeps flinging those outwards long after the
    // colour ramp has dimmed them. What actually reads as a taper is how much of
    // the band is FILLED: 0.29 / 0.48 / 0.50 / 0.34 on the first green run.
    // So: density peaks below the tip, and falls from there.
    const unsigned colWidth = cx1 - cx0;
    double density[4] = { 0, 0, 0, 0 };
    for (int b = 0; b < 4; ++b) {
        const unsigned rows = unsigned(double(b + 1) * span / 4.0) - unsigned(double(b) * span / 4.0);
        density[b] = rows ? double(bandLit[b]) / double(rows * colWidth) : 0.0;
        std::printf("    band %d fill density %.3f (outline %d px)\n", b, density[b], bandWidth[b]);
    }
    int densest = 0;
    for (int b = 1; b < 4; ++b) if (density[b] > density[densest]) densest = b;
    std::printf("    densest band: %d\n", densest);
    CHECK(densest <= 2, "3a. the flame is not densest at its tip");
    bool thins = true;
    for (int b = densest; b < 3; ++b)
        if (!(density[b] > density[b + 1])) thins = false;
    CHECK(thins, "3b. the flame TAPERS: every band above the densest is thinner");
    const double taper = density[3] > 0 ? density[densest] / density[3] : 1e9;
    std::printf("    densest/tip fill ratio %.2f (measured 1.48 on the first green run)\n", taper);
    CHECK(taper > 1.25, "3c. and the tip is meaningfully thinner, not a straight column");

    // ---- 4. it is FIRE-COLOURED -------------------------------------------
    double sr = 0, sg = 0, sb = 0; int litN = 0;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Colour c = avg.at(x, y);
            if (c.r + c.g + c.b > kLit) { sr += c.r; sg += c.g; sb += c.b; ++litN; }
        }
    const double mr = sr / std::max(1, litN), mg = sg / std::max(1, litN),
                 mb = sb / std::max(1, litN);
    std::printf("    mean lit colour: %.3f %.3f %.3f  (R/B = %.2f)\n", mr, mg, mb, mr / std::max(1e-6, mb));
    CHECK(mr > mg && mg > mb, "4a. R > G > B over the lit pixels");
    CHECK(mr / std::max(1e-6, mb) > 3.0, "4b. and strongly warm (R/B > 3)");

    // ---- 5. it MOVES ------------------------------------------------------
    Image a, b;
    view->readPixels(a);
    for (int i = 0; i < 30; ++i) engine->renderOneFrame();
    view->readPixels(b);
    int moved = 0, litEither = 0;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Colour ca = a.at(x, y), cb = b.at(x, y);
            const bool la = ca.r + ca.g + ca.b > kLit, lb = cb.r + cb.g + cb.b > kLit;
            if (!la && !lb) continue;
            ++litEither;
            if (std::fabs(ca.r - cb.r) + std::fabs(ca.g - cb.g) + std::fabs(ca.b - cb.b) > 0.03f)
                ++moved;
        }
    const int movedPct = litEither ? moved * 100 / litEither : 0;
    std::printf("    %d/%d lit pixels changed over 30 frames (%d%%)\n", moved, litEither, movedPct);
    CHECK(movedPct > 5, "5. the flame moves — it is a simulation, not a frozen sprite");

    // ---- 6. it STOPS ------------------------------------------------------
    engine->setParticleTimeScale(0.0f);
    engine->renderOneFrame();
    Image f1, f2;
    view->readPixels(f1);
    for (int i = 0; i < 30; ++i) engine->renderOneFrame();
    view->readPixels(f2);
    int frozenDiff = 0;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Colour c1 = f1.at(x, y), c2 = f2.at(x, y);
            if (c1.r != c2.r || c1.g != c2.g || c1.b != c2.b) ++frozenDiff;
        }
    std::printf("    frozen: %d pixels differ over 30 frames\n", frozenDiff);
    CHECK(frozenDiff == 0, "6. the clock at 0 freezes it exactly (not approximately)");
    engine->setFixedFrameDelta(1.0f / 60.0f);

    mirror.setSource(nullptr);
    engine->destroyView(view);
    engine->destroyScene(target);
    engine.reset();
    CHECK(true, "teardown clean");
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
