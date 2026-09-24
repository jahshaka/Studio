// ssr.rings — THE MARCH'S OWN PHASE, MEASURED (SSR-RINGS-1; SMOKE-41 item 1a,
// the render audit's SYNTHESIS item 12).
//
// THE SYMPTOM, on the real subject. On the Grand Showroom's glossy spheres the
// screen-space reflection is not smooth: it shows NESTED CONCENTRIC CRESCENTS,
// a staircase that follows the sphere's curvature, and they are the march's —
// with SSR off the same pixels show a smooth probe reflection. The audit asked
// for the mechanism to be MEASURED before anything was proposed, and it was, on
// the Showroom itself (the numbers are in PostFxDesc::ssrMarchPhase's note and
// the pictures in the lane's evidence directory): the accepted hit region
// covers 15.3 % of the crop at a 1.04 m step, 29.5 % at the shipped 0.26 m,
// 34.3 % at 0.13 m and 58.4 % when the thickness tolerance is widened instead.
// The crescents ARE that region's boundary.
//
// WHY THAT IS A DEFECT AND NOT A RESOLUTION LIMIT. Two questions decide whether
// a pixel gets a screen reflection at all — did the ray cross a surface, and
// how much does the depth buffer vouch for the crossing — and the shipped march
// answers BOTH at the coarse sample the ray happened to land on. So the answer
// depends on the ray's PHASE within a fixed step, which on a curved surface is
// a smooth function of the angle off the sphere's centre: concentric bands.
// The refinement that follows a crossing already locates it to 1/32 of a step;
// nothing but history says the two questions cannot be asked there.
//
// WHAT THIS SUITE PINS, on a fixture of its own (a mirror floor under an
// emissive cube, the same shape tests/ssr and tests/planar use, because it is
// the one fixture in the tree with a big, stable march footprint):
//
//   1. THE DEFAULT IS TODAY'S PICTURE, to the bit. `checker` is what every
//      shipped scene renders and what every pixel suite in the tree asserts.
//   2. THE MECHANISM, as a dose-response INSIDE one run: with `checker` the
//      march's accepted footprint moves with the STEP LENGTH (the same
//      dose-response the Showroom measurement found); with `refined` it moves
//      by much less, because the step no longer decides whether a ray hit.
//      This is the lane's whole claim, and it is measured rather than asserted
//      about a picture that is not this fixture's.
//   3. `dither` keeps the shipped hit rule exactly — its footprint still
//      follows the step — so it is a PHASE change and not a coverage change,
//      which is precisely what makes it the minimal candidate.
//
// TWO ENV-GATED EXTRAS, off in the gate:
//   JAH_SSR_RINGS_DUMP=1  writes one PPM per arm beside the binary.
//   JAH_SSR_RINGS_BENCH=1 times 300 offscreen frames per rule at 1920x1080 and
//                         3840x2160 and prints the milliseconds. Never
//                         asserted — an absolute timing has no business failing
//                         a build on somebody else's GPU.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf(cond ? "ok: " : "FAIL: ");                                  \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

static bool envOn(const char *name)
{
    const char *v = std::getenv(name);
    return v && *v && *v != '0';
}

static void writePpm(const Image &img, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const unsigned char rgb[3] = {
                (unsigned char)(c.r < 0 ? 0 : (c.r > 1 ? 255 : c.r * 255.0f + 0.5f)),
                (unsigned char)(c.g < 0 ? 0 : (c.g > 1 ? 255 : c.g * 255.0f + 0.5f)),
                (unsigned char)(c.b < 0 ? 0 : (c.b > 1 ? 255 : c.b * 255.0f + 0.5f)) };
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
    std::printf("    wrote %s\n", path.c_str());
}

/// THE MARCH'S ACCEPTED FOOTPRINT: how many pixels this frame differs from the
/// SSR-off frame by more than a bit of quantisation. It is the one statistic
/// the mechanism is about — "where did the screen replace the probe's answer" —
/// and it needs no assumption about what the reflection looks like.
static unsigned footprint(const Image &a, const Image &b)
{
    if (a.width != b.width || a.height != b.height) return 0u;
    unsigned n = 0;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour p = a.at(x, y), q = b.at(x, y);
            const float d = std::max(std::max(std::fabs(p.r - q.r), std::fabs(p.g - q.g)),
                                     std::fabs(p.b - q.b));
            if (d > 2.0f / 255.0f) ++n;
        }
    return n;
}

static bool identical(const Image &a, const Image &b, unsigned *outDiff = nullptr)
{
    if (a.width != b.width || a.height != b.height) return false;
    unsigned diff = 0;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour p = a.at(x, y), q = b.at(x, y);
            if (p.r != q.r || p.g != q.g || p.b != q.b) ++diff;
        }
    if (outDiff) *outDiff = diff;
    return diff == 0;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-ssr-rings-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    const unsigned kSize = 256;
    View *view = engine->createOffscreenView("rings", kSize, kSize, Colour(0, 0, 0));
    Scene *s = engine->createScene("rings");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.10f, 0.10f, 0.10f));

    // THE MIRROR FLOOR AND THE EMISSIVE CUBE (tests/ssr's fixture): a big,
    // stable march footprint, which is what a dose-response needs. The rings
    // themselves are a CURVED surface's symptom and are photographed on the
    // Showroom, not here — a fixture that reproduced them would be a second
    // renderer's worth of content for no extra proof.
    {
        const NodeId floor = s->createNode();
        PbrParams p;
        p.albedo = Colour(1.0f, 1.0f, 1.0f);
        p.metalness = 1.0f;
        p.roughness = 0.0f;
        const MaterialId m = s->createPbrMaterial(p);
        const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
        CHECK(floor && m && mesh && s->attachMesh(floor, mesh, m), "the glossy floor exists");
        enginetest::setNodeScale(s, floor, Vec3(12.0f, 0.2f, 12.0f));
        enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    }
    {
        const NodeId cube = s->createNode();
        PbrParams p;
        p.albedo = Colour(0.05f, 0.05f, 0.05f);
        p.emissive = Colour(3.0f, 0.0f, 0.0f);
        p.roughness = 0.5f;
        const MaterialId m = s->createPbrMaterial(p);
        const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
        CHECK(cube && m && mesh && s->attachMesh(cube, mesh, m), "the emissive cube exists");
        enginetest::setNodeScale(s, cube, Vec3(1.5f, 1.5f, 1.5f));
        enginetest::setNodePosition(s, cube, Vec3(0.0f, 2.2f, 0.0f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.4f, 7.0f), Vec3(0.0f, 0.6f, 0.0f));

    const auto shoot = [&](const PostFxDesc &fx, Image &out, const char *dumpName) {
        view->setPostFx(fx);
        for (int i = 0; i < 5; ++i) engine->renderOneFrame();   // the history is a frame late
        if (!view->readPixels(out)) { std::printf("FAIL: readPixels\n"); ++failures; }
        if (dumpName && envOn("JAH_SSR_RINGS_DUMP"))
            writePpm(out, std::string(dumpName) + ".ppm");
    };

    PostFxDesc off;
    off.allowOffscreen = true;
    Image noSsr;
    shoot(off, noSsr, "rings-ssr-off");

    // ---- 1. THE DEFAULT IS TODAY'S PICTURE --------------------------------
    PostFxDesc fx = off;
    fx.ssr = 2;                     // full-res rays: the Showroom's own row
    Image byDefault, explicitChecker;
    shoot(fx, byDefault, "rings-default");
    CHECK(fx.ssrMarchPhase == 0, "PostFxDesc::ssrMarchPhase defaults to `checker`");
    fx.ssrMarchPhase = 0;
    shoot(fx, explicitChecker, "rings-checker");
    unsigned diff = 0;
    CHECK_MSG(identical(byDefault, explicitChecker, &diff),
              "asking for `checker` is BYTE-IDENTICAL to leaving the rule alone (%u pixels differ)",
              diff);
    const unsigned fpChecker = footprint(byDefault, noSsr);
    // A FOOTPRINT BIG ENOUGH TO MEASURE: the reflected cube is a small, bright
    // object in a mirror floor, so the march's whole footprint is a few percent
    // of the frame by construction (measured: 3048 px, 4.7 %). What the bar
    // guards is a fixture that stopped reflecting at all — a thousand pixels is
    // two orders of magnitude above the noise and a third of what it should be.
    CHECK_MSG(fpChecker > 1000u,
              "the fixture HAS a march footprint to measure (%u px, %.1f%% of the frame)",
              fpChecker, 100.0 * double(fpChecker) / double(kSize * kSize));

    // ---- 2. THE MECHANISM: the footprint's dependence on the STEP ---------
    //
    // `ssrMaxDistance / steps` IS the step, so a quarter of the distance is a
    // quarter of the step with nothing else changed — not the resolution, not
    // the budget, not the fixture. What is compared is the CHANGE the step
    // makes to the accepted footprint under each rule.
    std::printf("\n-- the step's grip on the footprint (ssr = full-res rays, 96 steps) --\n");
    struct Arm { const char *name; int phase; };
    const Arm arms[] = { { "checker", 0 }, { "refined", 1 }, { "dither", 2 } };
    double grip[3] = { 0.0, 0.0, 0.0 };
    for (size_t a = 0; a < 3; ++a) {
        PostFxDesc v = off;
        v.ssr = 2;
        v.ssrMarchPhase = arms[a].phase;
        unsigned fp[3] = { 0, 0, 0 };
        const float dists[3] = { 25.0f, 6.25f, 50.0f };
        for (int d = 0; d < 3; ++d) {
            v.ssrMaxDistance = dists[d];
            Image img;
            char dump[64];
            std::snprintf(dump, sizeof(dump), "rings-%s-dist%.0f", arms[a].name, dists[d]);
            shoot(v, img, dump);
            fp[d] = footprint(img, noSsr);
        }
        // EVERY ARM MUST HAVE FOUND SOMETHING. `ssrMaxDistance` is the march's
        // RANGE as well as its step, and an arm short enough to reach nothing
        // would report a grip of 1.0 — "the step decides everything" — from a
        // range effect. The guard is what keeps the dose-response about the
        // step (measured at this pose: 3048 / 3296 / 3051 pixels).
        CHECK_MSG(fp[0] && fp[1] && fp[2],
                  "%s: every step length still reaches the reflected cube (%u / %u / %u px)",
                  arms[a].name, fp[0], fp[1], fp[2]);
        // The grip: how much of the footprint the step moves, as a fraction of
        // the shipped step's footprint. One number, both directions.
        const double base = std::max(1.0, double(fp[0]));
        grip[a] = (std::fabs(double(fp[1]) - base) + std::fabs(double(fp[2]) - base)) / base;
        std::printf("   %-8s footprint  0.26 m step %6u | 0.065 m %6u | 0.52 m %6u  -> grip %.3f\n",
                    arms[a].name, fp[0], fp[1], fp[2], grip[a]);
    }
    // THE BARS AND THEIR HEADROOM (measured on this rig: 8.2 %, 0.2 %, 9.4 %;
    // 2.6 %, 0.7 %, 4.1 % before SSR-EDGE-1). TWO SSR-EDGE-1 changes moved the
    // footprints, and they are not the same effect:
    //  * THE LAST SAMPLE ON THE RANGE'S END moved ONLY the 6.25 m arm under
    //    `checker` and `dither` (3119 -> 3296; their 25 m and 50 m arms held):
    //    its 24 steps now reach the crossings at the end of its short range
    //    that its last jittered step overshot — a RANGE effect, which is why
    //    this dose-response confounds range with step (the fixed-range arm
    //    below does not).
    //  * THE THICKNESS FADE MOVED TO THE ENVELOPE moved EVERY `refined` arm by
    //    ~+90 px (3175/3181/3159 -> 3266/3261/3263): refined's hitDiff is the
    //    bisected crossing's, non-zero on marginal crossings, and those used to
    //    be COUNTED out by the resolve's trust threshold; checker's coarse
    //    hitDiff is ~0 on a flat floor, so it did not move.
    // This is a FLAT floor, where the shipped march is already close to
    // trustworthy — its rays arrive face-on and well inside the tolerance, as
    // the shader's own note says — so the grip here is a few percent where on
    // the Showroom's CURVED sphere it is 15.3 % -> 34.3 % of the footprint. The
    // fixture is the one with a stable footprint, and the direction is what is
    // being pinned; the magnitude lives with the pictures.
    CHECK_MSG(grip[0] > 0.015,
              "WITH `checker` THE STEP DECIDES THE FOOTPRINT: a 4x / 2x step moves it by %.1f%% "
              "of itself", 100.0 * grip[0]);
    CHECK_MSG(grip[1] < grip[0] * 0.5,
              "...and `refined` takes that grip off: %.1f%% against %.1f%% (the step no longer "
              "decides WHETHER a ray hit)", 100.0 * grip[1], 100.0 * grip[0]);
    CHECK_MSG(grip[2] > grip[1],
              "...while `dither` keeps the shipped hit rule — its footprint still follows the "
              "step (%.1f%%), which is what makes it a PHASE change and not a coverage one",
              100.0 * grip[2]);

    // ---- 2b. THE STEP AT A FIXED RANGE ------------------------------------
    // The arms above change `ssrMaxDistance`, i.e. the RANGE with the step.
    // Here the range is held at 25 m, the resolution at full, and only the
    // step count moves (PostFxDesc::ssrSteps): 96 steps (0.26 m) against 24
    // (1.04 m) and 128 (0.195 m).
    std::printf("\n-- the step at a fixed 25 m range (ssrSteps 96 / 24 / 128) --\n");
    double fixedGrip[3] = { 0.0, 0.0, 0.0 };
    for (size_t a = 0; a < 3; ++a) {
        PostFxDesc v = off;
        v.ssr = 2;
        v.ssrMarchPhase = arms[a].phase;
        v.ssrMaxDistance = 25.0f;
        unsigned fp[3] = { 0, 0, 0 };
        const int stepsArm[3] = { 96, 24, 128 };
        for (int k = 0; k < 3; ++k) {
            v.ssrSteps = stepsArm[k];
            Image img;
            shoot(v, img, nullptr);
            fp[k] = footprint(img, noSsr);
        }
        const double base = std::max(1.0, double(fp[0]));
        fixedGrip[a] = (std::fabs(double(fp[1]) - base) + std::fabs(double(fp[2]) - base)) / base;
        std::printf("   %-8s footprint  96 steps %6u | 24 steps %6u | 128 steps %6u  -> grip %.3f\n",
                    arms[a].name, fp[0], fp[1], fp[2], fixedGrip[a]);
    }
    CHECK_MSG(fixedGrip[1] < fixedGrip[0] * 0.5,
              "AT A FIXED RANGE `refined` TAKES THE STEP'S GRIP OFF: %.1f%% against checker's %.1f%%",
              100.0 * fixedGrip[1], 100.0 * fixedGrip[0]);

    // ---- 3. THE COST, printed and never asserted --------------------------
    if (envOn("JAH_SSR_RINGS_BENCH")) {
        std::printf("\n-- the cost per rule (offscreen, 300 frames after 60 warm-up) --\n");
        const unsigned sizes[2][2] = { { 1920u, 1080u }, { 3840u, 2160u } };
        for (int si = 0; si < 2; ++si) {
            View *bench = engine->createOffscreenView("rings-bench", sizes[si][0], sizes[si][1],
                                                      Colour(0, 0, 0));
            if (!bench) { std::printf("   (no view at %ux%u)\n", sizes[si][0], sizes[si][1]); continue; }
            bench->setScene(s);
            enginetest::testCameraLookAt(bench, Vec3(0.0f, 1.4f, 7.0f), Vec3(0.0f, 0.6f, 0.0f));
            for (int a = -1; a < 3; ++a) {
                PostFxDesc v = off;
                if (a >= 0) { v.ssr = 2; v.ssrMarchPhase = arms[a].phase; }
                bench->setPostFx(v);
                for (int i = 0; i < 60; ++i) engine->renderOneFrame();
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < 300; ++i) engine->renderOneFrame();
                const double ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                        .count() / 300.0;
                std::printf("   %ux%u  %-8s %.3f ms/frame\n", sizes[si][0], sizes[si][1],
                            a < 0 ? "ssr off" : arms[a].name, ms);
            }
            engine->destroyView(bench);
        }
    }

    engine->destroyView(view);
    engine->destroyScene(s);
    std::printf(failures ? "FAILURES: %d\n" : "PASS: ssr.rings\n", failures);
    return failures ? 1 : 0;
}
