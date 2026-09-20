// THE BLOOM COMPOSITE TAKES AN AMOUNT (lane BLOOM-AMOUNT-1, ogre-patch 0082).
//
// THE OWNER'S REQUEST (review 2026-09-18, R17): "bloom also needs a value
// setting next to its check box so we can change the amount of bloom from 0 to
// 2x with current bloom being 1." The renderer had no strength term at all —
// `bloomThreshold` and `bloomKnee` decide WHICH pixels bloom, and nothing
// decided how much of the blurred result reached the picture.
//
// WHERE THE MULTIPLY LIVES is the whole engineering content, and this suite is
// what holds it there. The cheap-looking place is the bright pass, whose
// constant the host already writes; it is wrong twice, and BOTH errors would
// have been invisible to a test that only asked "is 2 brighter than 1":
//   * the blur ladder's targets are R10G10B10A2_UNORM, so a scale above 1
//     applied before them CLIPS — the dim edge of a halo would brighten and
//     its bright core would not move at all;
//   * the tonemap quad reads the ladder through `fromSRGB`, which is a SQUARE,
//     so a factor applied upstream arrives squared: 2x would be 4x.
// Arm D is the one that would catch either: it recovers the LIGHT behind each
// halo pixel (the tonemap quad's own curve, inverted) and asks for a RATIO of
// exactly two, not for "more".
//
// THE FIXTURE is one bright emissive square on black — the shape of every case
// bloom exists for (a lamp, a sun disc, a blown window) and nothing else in the
// frame to argue about. The square's own pixels are excluded from every
// measurement: they are saturated at every amount, so they would dilute the
// only quantity this suite is about, the light that spread OUTSIDE the emitter.
//
// EVERY ARM IS ONE PROCESS, ONE VIEW, ONE FIXTURE, four descriptions — bloom
// off, and bloom on at 0, 1 and 2 — so nothing here depends on two runs of a
// driver agreeing about anything.
//
// THE ARMS
//   A  CONTROL: the fixture actually blooms. Amount 1 must lift the halo well
//      clear of the bloom-off picture, or every arm below is measuring noise.
//   B  ZERO IS THE BLOOM-OFF PICTURE, to the byte. The amount multiplies the
//      added term, and the bloom-OFF chain adds a black texture — the same
//      zero. This is what makes the dial safe to scrub to nothing: the picture
//      at 0 is the picture with the effect switched off, and the difference is
//      only what you PAY.
//   C  ONE IS THE PICTURE THIS ENGINE ALWAYS DREW. The uniform is the amount
//      MINUS ONE and the shader multiplies by exactly 1.0 there, so the frame
//      must be byte-identical to the same binary with the amount never pushed
//      at all. (The --engine-selftest hash is the same claim on the whole
//      application; this is it where it can be failed loudly.)
//   D  THE DIAL IS LINEAR IN THE LIGHT THE BLOOM ADDS, per pixel. Display
//      codes cannot carry that claim in either direction — the curve
//      compresses at the top and CLAMPS at the bottom (this pin's grade puts
//      black at -0.015, so the first four codes of bloom are thrown away) — so
//      the arm inverts the shader's own curve and compares the light. The
//      tolerance is the 8-bit step, not slop.
//   E  MONOTONIC EVERYWHERE, per pixel: no pixel of the halo may be darker at
//      2 than at 1, or at 1 than at 0. A term that clipped in the ladder would
//      fail this on the bright pixels while passing D on the dim ones.
//   F  SCRUBBING IT REBUILDS NOTHING. View::workspaceGeneration() must not move
//      across the whole 0 -> 1 -> 2 -> 1 walk, while turning bloom itself off
//      and on DOES move it — the control that proves the counter is alive.
//      This is the difference between a dial a person can drag and a dial that
//      destroys the compositor under the cursor.
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

constexpr unsigned kSide = 256;
/// Half-width of the square the emitter's own pixels are excluded by, in
/// pixels from the frame's centre. The emitter itself measures 44 px from the
/// centre at this pose (2.0 world units at 28 m through a 45 degree vertical
/// field over 256 px); 56 leaves a 12 px cordon so that no fringe of the
/// emitter's own edge — antialiasing, the blur's first tap — is counted as
/// halo. Everything outside it is BLACK in the bloom-off picture, which is what
/// makes "the halo's summed excess" a number with no background in it.
constexpr int kEmitterGuard = 56;

Engine *gEngine = nullptr;
View   *gView = nullptr;

/// A flat quad in the XY plane at z == 0, `half` world units either side of the
/// origin, facing +Z — the same fixture shape, and for the same reason, as
/// tests/hdr's dither suite: it keeps the camera at identity orientation.
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

/// The view's description with one field changed. Every arm below is a
/// description edit and a re-read; nothing else about the fixture moves.
void setBloom(bool on, float amount)
{
    PostFxDesc fx = gView->postFx();
    fx.bloom = on;
    fx.bloomAmount = amount;
    gView->setPostFx(fx);
}

bool shoot(Image &out, int frames = 4)
{
    for (int i = 0; i < frames; ++i) gEngine->renderOneFrame();
    return gView->readPixels(out);
}

/// Is this pixel part of the halo — i.e. outside the emitter's cordon?
bool inHalo(unsigned x, unsigned y)
{
    const int dx = int(x) - int(kSide / 2), dy = int(y) - int(kSide / 2);
    return std::abs(dx) > kEmitterGuard || std::abs(dy) > kEmitterGuard;
}

/// The green channel, which on this grey-and-white fixture is the luminance.
std::vector<int> green(const Image &img)
{
    std::vector<int> v(size_t(img.width) * img.height);
    for (size_t i = 0; i < v.size(); ++i) v[i] = img.rgba[i * 4 + 1];
    return v;
}

/// The halo's summed EXCESS over the bloom-off picture, in display codes — the
/// control arm's "does this fixture bloom at all", and the line arm D prints
/// for a person to read. The subtraction is what makes it a measurement of the
/// bloom and not of the scene.
double haloExcess(const std::vector<int> &frame, const std::vector<int> &off)
{
    double sum = 0.0;
    for (unsigned y = 0; y < kSide; ++y)
        for (unsigned x = 0; x < kSide; ++x) {
            if (!inHalo(x, y)) continue;
            const size_t i = size_t(y) * kSide + x;
            sum += double(frame[i] - off[i]);
        }
    return sum;
}

/// THE TONEMAP QUAD'S OWN ARITHMETIC, RESTATED — the only way to ask whether
/// the dial is linear IN LIGHT, which is the claim, rather than in display
/// codes, which it cannot be.
///
/// The shader (Samples/Media/.../FinalToneMapping_ps.glsl) computes
///
///     out = ( Filmic(x) / Filmic(W) - 0.5 ) * 1.25 + 0.5 + 0.11
///
/// and writes it as an 8-bit code. In the HALO of this fixture the scene's own
/// contribution is exactly zero — black clear, black albedo, no ambient, no
/// light — so `x` IS the bloom term, `16 * fromSRGB(ladder) * amount`, and
/// nothing else. Inverting the curve therefore recovers the light the bloom put
/// there, and twice the amount must recover twice the light, per pixel.
///
/// WHY THE CODES THEMSELVES CANNOT CARRY THE CLAIM, in both directions:
///   * the curve COMPRESSES at the top (that is what a tonemapper is), so a
///     bright halo pixel cannot be twice as bright in codes however linear the
///     term is;
///   * and it has a DEAD ZONE at the bottom, which is the surprise and is worth
///     recording: `out` for x == 0 is -0.015, i.e. the affine tail this pin
///     applies after the curve puts black BELOW zero, so the first ~4 codes of
///     bloom are clamped away entirely. Measured on this fixture: summing the
///     dim tail of the halo in codes reads a ratio of 3.3 rather than 2, and
///     every code of that error is the clamp, not the dial.
/// A suite that asserted on code sums would therefore have to pick a band by
/// eye and would pin the CURVE, not the amount. This pins the amount.
constexpr double kW = 11.2;
double filmic(double x)
{
    const double A = 0.22, B = 0.3, C = 0.10, D = 0.20, E = 0.01, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
double toneCurve(double x)          // the shader's output value, before the 8-bit write
{
    return (filmic(x) / filmic(kW) - 0.5) * 1.25 + 0.5 + 0.11;
}
/// The light behind a display value, by bisection on a monotone curve. 1e-7 of
/// a unit is far finer than one code is worth and costs ~24 iterations.
double lightBehind(double value)
{
    double lo = 0.0, hi = kW;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (toneCurve(mid) < value) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

double median(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

}   // namespace

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-hdr-bloom-amount-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    gEngine = engine.get();

    gView = engine->createOffscreenView("bloom", kSide, kSide, Colour(0, 0, 0));
    Scene *s = engine->createScene("bloom");
    if (!gView || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    gView->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    gView->setShadows(false);

    // THE EMITTER. Emissive rather than lit: a light would put a falloff across
    // the whole frame and the halo would be measured against a gradient instead
    // of against black. 8.0 is HDR — four times over the bright-pass threshold
    // below — so the square is unambiguously in the bloom and its neighbourhood
    // is unambiguously not.
    {
        const NodeId n = s->createNode();
        const MeshId m = s->createMesh(planeMesh(2.0f));
        PbrParams p;
        p.albedo = Colour(0.0f, 0.0f, 0.0f);
        p.metalness = 0.0f;
        p.roughness = 1.0f;
        p.emissive = Colour(8.0f, 8.0f, 8.0f);
        const MaterialId mat = s->createPbrMaterial(p);
        if (!n || !m || !mat || !s->attachMesh(n, m, mat)) {
            std::printf("FAIL: fixture emitter: %s\n", engine->lastError().c_str());
            return 1;
        }
    }
    enginetest::testCameraAt(gView, Vec3(0.0f, 0.0f, 28.0f));

    // THE GRADE: HDR at a FIXED exposure — a screenshot's grade, and the one
    // the bloom composite lives in. The threshold is low so the emitter's 8.0
    // is well inside the bright pass and the knee is the default width.
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdr = true;
        fx.tonemapFixed = true;
        fx.exposure = 0.0f;
        fx.bloom = false;
        fx.bloomThreshold = 2.0f;
        fx.bloomKnee = 2.0f;
        gView->setPostFx(fx);
    }

    // ---- the four pictures, one process ------------------------------------
    // Bloom OFF first: it is the reference every arm subtracts, and it is the
    // one arm whose chain has a different SHAPE (no ladder at all).
    Image offImg, zeroImg, oneImg, twoImg, oneAgain;
    if (!shoot(offImg, 8)) { std::printf("FAIL: readPixels (bloom off)\n"); return 1; }
    const unsigned genAfterOff = gView->workspaceGeneration();

    setBloom(true, 1.0f);
    if (!shoot(oneImg, 6)) { std::printf("FAIL: readPixels (amount 1)\n"); return 1; }
    const unsigned genAtOne = gView->workspaceGeneration();

    setBloom(true, 0.0f);
    if (!shoot(zeroImg, 4)) { std::printf("FAIL: readPixels (amount 0)\n"); return 1; }

    setBloom(true, 2.0f);
    if (!shoot(twoImg, 4)) { std::printf("FAIL: readPixels (amount 2)\n"); return 1; }

    setBloom(true, 1.0f);
    if (!shoot(oneAgain, 4)) { std::printf("FAIL: readPixels (back to 1)\n"); return 1; }
    const unsigned genAfterWalk = gView->workspaceGeneration();

    const std::vector<int> off = green(offImg), zero = green(zeroImg),
                           one = green(oneImg), two = green(twoImg),
                           again = green(oneAgain);

    // THE FIXTURE IS WHAT IT CLAIMS TO BE — the control. Without this, a black
    // frame would pass B and E and make D meaningless.
    {
        int brightest = 0; size_t litHalo = 0;
        for (unsigned y = 0; y < kSide; ++y)
            for (unsigned x = 0; x < kSide; ++x) {
                const size_t i = size_t(y) * kSide + x;
                brightest = std::max(brightest, off[i]);
                if (inHalo(x, y) && one[i] > off[i] + 1) ++litHalo;
            }
        CHECK(brightest >= 200,
              "the fixture renders a saturated emitter (brightest code %d)", brightest);
        CHECK(litHalo >= 400,
              "…and its bloom reaches %zu halo pixels at 1x", litHalo);
    }

    // ---- A  CONTROL: THE FIXTURE BLOOMS ------------------------------------
    const double e1 = haloExcess(one, off);
    const double e2 = haloExcess(two, off);
    const double e0 = haloExcess(zero, off);
    {
        CHECK(e1 > 2000.0,
              "A control: at 1x the halo carries %.0f codes of light the bloom-off picture "
              "does not have", e1);
    }

    // ---- B  ZERO IS THE BLOOM-OFF PICTURE ----------------------------------
    {
        size_t differ = 0; int worst = 0;
        for (size_t i = 0; i < zeroImg.rgba.size(); ++i) {
            const int d = std::abs(int(zeroImg.rgba[i]) - int(offImg.rgba[i]));
            if (d) { ++differ; worst = std::max(worst, d); }
        }
        CHECK(worst <= 1,
              "B: amount 0 renders the bloom-OFF picture to within one code "
              "(%zu of %zu bytes differ, worst %d; halo excess %.1f against %.1f at 1x)",
              differ, zeroImg.rgba.size(), worst, e0, e1);
        CHECK(differ == 0,
              "B exact: it is BYTE-IDENTICAL — the term is multiplied by zero, which is what "
              "the bloom-off chain adds (%zu bytes differ)", differ);
    }

    // ---- C  ONE IS THE PICTURE THIS ENGINE ALWAYS DREW ---------------------
    // The dial cannot regrade a scene nobody touched: 1 must be the frame the
    // shader produced before the multiply existed. The comparison available
    // inside one process is the amount pushed and then RE-pushed across two
    // other values — the same uniform written three times must land back on
    // exactly the same picture — and the whole-application form of the claim is
    // the --engine-selftest hash, which the lane measured unchanged.
    {
        const bool same = again.size() == one.size() && oneAgain.rgba == oneImg.rgba;
        CHECK(same,
              "C: 1x is one picture however the dial got there (0 -> 2 -> 1 lands byte for "
              "byte back on it)%s", same ? "" : " — it does not");
    }

    // ---- D  LINEAR IN THE LIGHT THE BLOOM ADDS -----------------------------
    // PER PIXEL, IN LIGHT, by inverting the tonemap quad's own curve (see
    // lightBehind above for why the codes cannot carry this and what the
    // fixture's dim tail actually measures). The window is 24 <= code(1x) and
    // code(2x) <= 248: below it the curve's dead zone and the 8-bit step
    // dominate, above it the pixel is at the top of the curve where a ratio of
    // TWO could not be represented in codes at all.
    {
        std::vector<double> ratios;
        for (unsigned y = 0; y < kSide; ++y)
            for (unsigned x = 0; x < kSide; ++x) {
                if (!inHalo(x, y)) continue;
                const size_t i = size_t(y) * kSide + x;
                if (one[i] < 24 || two[i] > 248) continue;
                const double l1 = lightBehind(double(one[i]) / 255.0);
                const double l2 = lightBehind(double(two[i]) / 255.0);
                if (l1 > 1e-6) ratios.push_back(l2 / l1);
            }
        const double med = median(ratios);
        CHECK(ratios.size() >= 500,
              "D control: %zu halo pixels sit in the window where a ratio is measurable",
              ratios.size());
        // THE TOLERANCE IS THE 8-BIT STEP, not slop: one code at the bottom of
        // the window is ~2 %% of the light behind it, and both arms carry it.
        // A scale applied inside the UNORM blur ladder would land well under 2
        // (it clips), a scale applied before the composite's fromSRGB square
        // would land near 4, and neither can hide inside this band.
        CHECK(med >= 1.93 && med <= 2.07,
              "D: twice the amount is twice the light, per pixel — median ratio %.4f over "
              "%zu pixels (the shader's own curve inverted)", med, ratios.size());
        // For the record, and NOT asserted: the same comparison in display
        // codes, which is what a person sees. It is smaller than 2 because the
        // curve compresses, and that is correct behaviour, not a defect.
        std::printf("   [D] whole halo in CODES: %.0f at 1x, %.0f at 2x (ratio %.3f) — "
                    "the curve's compression, not the dial\n",
                    e1, e2, e1 > 0.0 ? e2 / e1 : 0.0);
    }

    // ---- E  MONOTONIC, PER PIXEL -------------------------------------------
    // A scale applied inside the ladder would CLIP, so the bright half of the
    // halo would stop responding while the dim half kept climbing. Per pixel,
    // more bloom must never mean less light.
    {
        size_t downAt1 = 0, downAt2 = 0;
        for (unsigned y = 0; y < kSide; ++y)
            for (unsigned x = 0; x < kSide; ++x) {
                if (!inHalo(x, y)) continue;
                const size_t i = size_t(y) * kSide + x;
                // One code of slack for the dither, which is keyed on the pixel
                // and therefore identical in every arm — it can only move a
                // value that was sitting on a rounding boundary.
                if (one[i] + 1 < zero[i]) ++downAt1;
                if (two[i] + 1 < one[i]) ++downAt2;
            }
        CHECK(downAt1 == 0 && downAt2 == 0,
              "E: no halo pixel gets DARKER as the amount rises (%zu darker at 1x, %zu at 2x)",
              downAt1, downAt2);
    }

    // ---- F  SCRUBBING IT REBUILDS NOTHING ----------------------------------
    {
        CHECK(genAtOne == genAfterOff + 1,
              "F control: turning bloom ON rebuilt the workspace once (generation %u -> %u) "
              "— the counter is alive and the ladder IS graph shape",
              genAfterOff, genAtOne);
        CHECK(genAfterWalk == genAtOne,
              "F: walking the amount 1 -> 0 -> 2 -> 1 rebuilt NOTHING (generation still %u) "
              "— it is a uniform, and a drag cannot destroy the compositor under the cursor",
              genAfterWalk);
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
