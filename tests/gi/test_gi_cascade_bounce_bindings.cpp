// gi.cascade_bounce_bindings — THE CHAIN'S BOUNCE IS A FUNCTION OF THE LIGHT,
// NOT OF ITS OWN HISTORY (ogre-patch 0060; PHOTON_SPEC §7 E2, the §509 F2
// follow-up 0057's header named).
//
// THE DEFECT THIS EXISTS FOR, in the pin. `VctLighting::runBounce()` ends with
// `std::swap( mLightVoxel[0], mLightBounce )` and re-binds slot 2 — its OWN
// light voxel — immediately after. Slots 3..N are the EXTRA CASCADES' light
// voxels and are written ONCE, in `setupBounceTextures()`, which nothing on the
// per-frame path calls again. Every cascade swaps its own pointers on every
// bounce iteration it runs, so after an ODD number of them this job reads the
// texture that cascade has just stopped writing: the cross-cascade term of the
// bounce integrates the PREVIOUS injection's radiance.
//
// WHY IT IS REACHABLE HERE AND NOT UPSTREAM: upstream's own cascade manager
// gives every cascade the same bounce count, and an EVEN number of swaps comes
// back to where it started (a swap is an involution). Our per-cascade
// stabilisation — which is upstream's own formula, a coarser cascade gets more
// bounces — resolves to 1 / 3 / 7 on a four-cascade chain at THREE total
// bounces, which is three odd counts out of three. That is Photon's Epic tier.
//
// THE OBSERVABLE, and why it is this one. "Reads the previous injection's
// radiance" is a statement about HISTORY, so the assertion is a history test and
// not a pixel constant: the same scene, the same light, reached two different
// ways, must render the same. Sun RED -> read; sun BLUE -> read; sun RED again
// -> read. The first and third states are identical in every input, so their
// pixels must agree; with a stale cross-cascade binding the third carries the
// blue injection the second left behind in the texture the job is still
// pointing at. The suite also asserts that the red/blue difference is LARGE,
// because a history test proves nothing if it is measuring nothing.
//
// Its own binary like every GI suite: the voxel lighting binds process-wide to
// HlmsPbs, so this scene must not share a process with another arm's.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                     \
    } while (0)

static const unsigned kSize = 128;

static void render(Engine *e, int frames = 1)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

/// The mean colour of a band of the picture — a band and not a pixel so one
/// dithered texel cannot decide a history test.
static Colour bandMean(const Image &img, unsigned y0, unsigned y1)
{
    float r = 0, g = 0, b = 0; unsigned n = 0;
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = kSize / 4u; x < kSize * 3u / 4u; ++x) {
            const Colour c = img.at(x, y);
            r += c.r; g += c.g; b += c.b; ++n;
        }
    if (!n) return Colour(0, 0, 0);
    return Colour(r / float(n), g / float(n), b / float(n));
}

static float dist(const Colour &a, const Colour &b)
{
    return std::sqrt((a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g) +
                     (a.b - b.b) * (a.b - b.b));
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-cascade-bounce-bindings-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("bounce", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("bounce");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    // NO AMBIENT AT ALL: every photon in this picture has bounced, which is what
    // makes a bounce term measurable instead of a correction to a constant.
    scene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // A long hall: the camera stands at one end, a big coloured bouncer 25 m
    // away — OUTSIDE cascade 0's 10 m box, so the light that reaches the far
    // wall from it travels through the cascades this suite is about.
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.85f, 0.85f, 0.85f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(120.0f, 0.1f, 120.0f));
    const NodeId bouncer = enginetest::addTestCube(scene, Colour(0.9f, 0.9f, 0.9f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, bouncer, Vec3(0.0f, 5.0f, -25.0f));
    enginetest::setNodeScale(scene, bouncer, Vec3(24.0f, 10.0f, 0.4f));
    const NodeId sun = enginetest::addDirectionalLight(scene, Vec3(0.0f, -0.35f, -1.0f), 6.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 6.0f), Vec3(0.0f, 2.0f, -25.0f));

    // THE CHAIN, at THREE bounces — the counts that make this reachable.
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;     // the four-cascade table: 5/10/15/60 m
    gi.numBounces = 3;                // -> cascadeBounces 1 / 3 / 7: three odd counts
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;
    gi.cascades = true;
    CHECK(scene->setGlobalIllumination(gi), "the four-cascade chain builds at three bounces");
    render(e, 10);
    const GiStatus st = scene->giStatus();
    CHECK(st.cascades.size() >= 3, "the chain is a chain (3+ cascades)");
    CHECK(st.vctBound, "...and the shader is sampling it");
    if (st.cascades.size() < 3 || !st.vctBound) {
        std::printf("FAILED: no chain to measure\n");
        return 1;
    }

    const auto lightIs = [&](const Colour &c) {
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = c;
        l.intensity = 6.0f / 3.14159265358979f;
        l.castShadows = false;
        scene->setLight(sun, l);
        scene->refreshGlobalIllumination();     // the light-only re-injection path
        render(e, 4);
        Image img;
        view->readPixels(img);
        return bandMean(img, kSize / 2u, kSize * 7u / 8u);
    };

    // ---- the history test ------------------------------------------------
    const Colour red1  = lightIs(Colour(1.0f, 0.15f, 0.15f));
    const Colour blue  = lightIs(Colour(0.15f, 0.15f, 1.0f));
    const Colour red2  = lightIs(Colour(1.0f, 0.15f, 0.15f));
    std::printf("   red  (first)  = %.4f %.4f %.4f\n", red1.r, red1.g, red1.b);
    std::printf("   blue          = %.4f %.4f %.4f\n", blue.r, blue.g, blue.b);
    std::printf("   red  (again)  = %.4f %.4f %.4f\n", red2.r, red2.g, red2.b);
    const float swing = dist(red1, blue);
    const float drift = dist(red1, red2);
    std::printf("   red<->blue swing %.4f, red-to-red drift %.4f (%.1f%% of the swing)\n",
                swing, drift, swing > 0.0f ? 100.0f * drift / swing : 0.0f);

    // The suite is measuring something: the two light colours must actually
    // change this band, or the history test below is vacuous.
    CHECK(swing > 0.02f, "the two light colours move the bounced picture measurably");
    // AND THE PICTURE IS A FUNCTION OF THE LIGHT, NOT OF THE HISTORY. 10 % of the
    // swing is a wide margin on purpose — the pin's own bounce is progressive and
    // the third state is reached after two more injections than the first — and it
    // is far below what one whole injection of the OTHER colour can produce.
    CHECK(drift < 0.10f * swing,
          "THE SAME LIGHT RENDERS THE SAME PICTURE WHATEVER CAME BEFORE IT "
          "(the cross-cascade bindings are not a bounce behind)");

    // ...and it holds when the same state is reached a third time, so a single
    // lucky ordering cannot pass it.
    const Colour blue2 = lightIs(Colour(0.15f, 0.15f, 1.0f));
    const float driftBlue = dist(blue, blue2);
    std::printf("   blue-to-blue drift %.4f\n", driftBlue);
    CHECK(driftBlue < 0.10f * swing, "...and the same holds for the other state");

    GiParams off; off.mode = GiMode::Off;
    CHECK(scene->setGlobalIllumination(off), "the chain comes down");
    render(e, 2);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
