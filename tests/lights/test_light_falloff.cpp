// POINT/SPOT FALLOFF AND THE SPOT CONE.
//
// THE FALLOFF (IMAGE-1). A point or spot light follows the inverse square law
// from its source radius, windowed to zero at its authored range (Karis 2013):
//     E = I / max(d^2, rSrc^2) * saturate(1 - (d/R)^4)^2
// — `lightFalloff` in Types.h, the fork's JahBrdf `jahLightAttenuation` in the
// shaders. MEASURED IN FLOAT (readPixelsHdr, the scene's linear radiance): a
// small patch faces the light at a known distance — the light straight above
// it, the camera straight above both — so the only variable is d. The law is
// checked within 2 % between 1 m and R/2 (the window itself is 0.879 at R/2,
// so a bare 1/d^2 is NOT what is asserted there: the stated law is), and the
// light reaches zero smoothly at R and stays there past it. The editor's
// range circle is drawn at R (scenemirror scales the ring by `distance`), so
// "the wire is where the light ends" is the same assertion.
//
// FIX 5 — THE CONE (F-S1). `spotCutOff` is a HALF angle everywhere in the
// document — the editor's cone wire is `radius = range * tan(spotCutOff)` —
// while `Light::setSpotlightRange` wants the FULL apex angle. The half angle
// went straight through, so every spot rendered a cone half as wide as the wire
// drawn around it.
//
// HOW IT IS MEASURED. A single small probe patch, the camera looking straight
// down at it: the centre pixel is then that patch and nothing else.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static float probeAt(Engine *engine, View *view, Scene *s, NodeId probe, float x, float z)
{
    enginetest::setNodePosition(s, probe, Vec3(x, 0.0f, z));
    // Straight down onto the patch, close enough that it fills the frame.
    enginetest::testCameraLookAt(view, Vec3(x, 3.0f, z + 0.001f), Vec3(x, 0.0f, z));
    for (int i = 0; i < 3; ++i) engine->renderOneFrame();
    Image img; view->readPixels(img);
    const Colour c = img.at(64, 64);
    return (c.r + c.g + c.b) / 3.0f;
}

// ---------------------------------------------------------------------------
// THE FALLOFF: the light straight above a patch that faces it, at distance d.
// ---------------------------------------------------------------------------
static float radianceAt(Engine *engine, View *view, Scene *s, NodeId lightNode, float d)
{
    // The patch's top face is at y = 0.025 (a 0.05-high box at the origin).
    s->setNodeTransform(lightNode, Vec3(0.0f, 0.025f + d, 0.0f), Quat(), Vec3(1, 1, 1));
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.6f, 0.001f), Vec3(0.0f, 0.0f, 0.0f));
    for (int i = 0; i < 3; ++i) engine->renderOneFrame();
    ImageF img;
    if (!view->readPixelsHdr(img)) return -1.0f;
    const Colour c = img.at(64, 64);
    return (c.r + c.g + c.b) / 3.0f;
}

static void pointRangeCase(Engine *engine, View *view)
{
    const float R = 6.0f;
    std::printf("-- point light, range %.1f: the inverse square law, windowed to the range\n", R);
    Scene *s = engine->createScene("falloff_point");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    PostFxDesc fx;
    fx.hdrReadback = true;          // the scene's linear radiance, before any grade
    view->setPostFx(fx);

    const NodeId probe = enginetest::addTestCube(s, Colour(0.9f, 0.9f, 0.9f), 0.0f, 0.9f);
    enginetest::setNodeScale(s, probe, Vec3(1.2f, 0.05f, 1.2f));

    const NodeId lightNode = s->createNode();
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1, 1, 1);
    l.intensity = 4.0f;
    l.range = R;
    l.castShadows = false;
    s->setLight(lightNode, l);

    // THE LAW: radiance / lightFalloff(d) is one constant (the patch's albedo,
    // pi and the BRDF at normal incidence), so normalise by d = 1 m.
    const double ref = double(radianceAt(engine, view, s, lightNode, 1.0f)) /
                       lightFalloff(1.0, R, l.sourceRadius);
    double worst = 0.0, worstBare = 0.0;
    for (float d : { 1.0f, 1.5f, 2.0f, 2.5f, 3.0f }) {
        const float e = radianceAt(engine, view, s, lightNode, d);
        const double law = ref * lightFalloff(d, R, l.sourceRadius);
        const double bare = ref / (double(d) * d);
        std::printf("   d %.1f m: radiance %.5f  law %.5f (%+.2f %%)  1/d^2 %.5f (%+.2f %%)\n", d, e,
                    law, 100.0 * (e / law - 1.0), bare, 100.0 * (e / bare - 1.0));
        worst = std::max(worst, std::fabs(e / law - 1.0));
        worstBare = std::max(worstBare, std::fabs(e / bare - 1.0));
    }
    CHECK(ref > 0.0, "the light lights the patch at all (the test is not vacuous)");
    CHECK(worst < 0.02, "between 1 m and R/2 the light follows I / d^2 x the window within 2 %");
    std::printf("   (a bare 1/d^2 is off by up to %.1f %% at R/2: the window's own 0.879)\n",
                100.0 * worstBare);

    // INSIDE THE SOURCE: no brighter than at the source radius.
    const float atSrc = radianceAt(engine, view, s, lightNode, l.sourceRadius);
    const float inside = radianceAt(engine, view, s, lightNode, l.sourceRadius * 0.5f);
    std::printf("   d = rSrc %.5f, d = rSrc/2 %.5f\n", atSrc, inside);
    CHECK(std::fabs(inside / atSrc - 1.0f) < 0.02f,
          "inside the source radius the light stops getting brighter (1 / max(d^2, rSrc^2))");

    // THE WINDOW: smooth to zero at R, nothing past it.
    const float at90 = radianceAt(engine, view, s, lightNode, R * 0.9f);
    const float at98 = radianceAt(engine, view, s, lightNode, R * 0.98f);
    const float beyond = radianceAt(engine, view, s, lightNode, R * 1.1f);
    const double law90 = ref * lightFalloff(R * 0.9, R, l.sourceRadius);
    std::printf("   d 0.9R %.6f (law %.6f)  0.98R %.6f  1.1R %.6f\n", at90, law90, at98, beyond);
    CHECK(std::fabs(at90 / law90 - 1.0) < 0.05, "the window is the stated one near the range (0.9 R)");
    CHECK(at98 < at90 * 0.2f && at98 >= 0.0f, "the light fades smoothly into the range circle");
    CHECK(beyond == 0.0f, "NOTHING is lit past the authored range");

    view->setPostFx(PostFxDesc());   // the spot case reads the plain 8-bit instrument
    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// Fix 5: a SPOT light's lit disc on the floor must have the radius its wire is
// drawn with — `range * tan(halfAngle)` for a light `range` above the floor.
// Before the fix the rendered disc was `range * tan(halfAngle / 2)`: for the
// 40-degree half angle used here, 0.42x the wire instead of 1.0x, so the sample
// at 0.75 of the wire radius (well inside the wire) came back BLACK.
// ---------------------------------------------------------------------------
static void spotConeCase(Engine *engine, View *view)
{
    const float halfDeg = 40.0f, height = 5.0f;
    const float wireRadius = height * std::tan(halfDeg * 3.14159265f / 180.0f);
    std::printf("-- spot light, half angle %.0f deg at height %.1f: wire radius %.2f\n",
                halfDeg, height, wireRadius);
    Scene *s = engine->createScene("falloff_spot");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    const NodeId probe = enginetest::addTestCube(s, Colour(0.9f, 0.9f, 0.9f), 0.0f, 0.9f);
    enginetest::setNodeScale(s, probe, Vec3(0.8f, 0.05f, 0.8f));

    const NodeId lightNode = s->createNode();
    LightDesc l;
    l.type = LightType::Spot;
    l.colour = Colour(1, 1, 1);
    l.intensity = 20.0f;           // 5 m away: 20 / 25 = 0.8 at the centre, no saturation
    l.range = height * 3.0f;        // the cone must not be cut short by the range
    l.spotAngleDegrees = halfDeg;   // HALF angle, the document's convention
    l.spotSoftness = 0.15f;         // the new default: a narrow soft edge
    l.spotFalloff = 1.0f;
    l.castShadows = false;
    // Identity rotation: engine lights shine down -Y (Types.h / the -Y
    // convention the document shares).
    s->setNodeTransform(lightNode, Vec3(0.0f, height, 0.0f), Quat(), Vec3(1, 1, 1));
    s->setLight(lightNode, l);

    const float centre = probeAt(engine, view, s, probe, 0.0f, 0.0f);
    const float in75   = probeAt(engine, view, s, probe, wireRadius * 0.75f, 0.0f);
    const float out140 = probeAt(engine, view, s, probe, wireRadius * 1.40f, 0.0f);
    std::printf("   luminance:  centre %.3f   0.75x wire %.3f   1.40x wire %.3f\n",
                centre, in75, out140);

    CHECK(centre > 0.05f, "the spot lights its own centre (not vacuous)");
    // THE FIX, stated as the user sees it.
    CHECK(in75 > centre * 0.25f,
          "the floor 75% of the way to the WIRE is lit (before: black — the cone was half as wide)");
    CHECK(out140 < centre * 0.05f, "...and outside the wire it is not");

    // The softness knob still means what it says: a hard-edged spot lights the
    // same disc but with a brighter shoulder than a soft-edged one.
    l.spotSoftness = 0.0f;
    s->setLight(lightNode, l);
    const float hard95 = probeAt(engine, view, s, probe, wireRadius * 0.95f, 0.0f);
    l.spotSoftness = 0.9f;
    s->setLight(lightNode, l);
    const float soft95 = probeAt(engine, view, s, probe, wireRadius * 0.95f, 0.0f);
    std::printf("   at 0.95x wire:  softness 0.0 -> %.3f   softness 0.9 -> %.3f\n", hard95, soft95);
    CHECK(hard95 > soft95, "softness widens the penumbra (0 is a hard edge, 0.9 is nearly all of it)");

    // ...and so does the falloff exponent, which had no document field at all
    // until this fix. A higher exponent concentrates the light into the core,
    // so the penumbra shoulder darkens.
    l.spotSoftness = 0.5f;
    l.spotFalloff = 1.0f;
    s->setLight(lightNode, l);
    const float fall1 = probeAt(engine, view, s, probe, wireRadius * 0.85f, 0.0f);
    l.spotFalloff = 4.0f;
    s->setLight(lightNode, l);
    const float fall4 = probeAt(engine, view, s, probe, wireRadius * 0.85f, 0.0f);
    std::printf("   at 0.85x wire:  falloff 1.0 -> %.3f   falloff 4.0 -> %.3f\n", fall1, fall4);
    CHECK(fall4 < fall1, "the falloff exponent reaches the renderer (the new document field works)");

    view->setScene(nullptr);
    engine->destroyScene(s);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-light-falloff-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("falloff", 128, 128, Colour(0, 0, 0));
    pointRangeCase(engine.get(), view);
    spotConeCase(engine.get(), view);

    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
