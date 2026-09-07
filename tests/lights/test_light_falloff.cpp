// POINT/SPOT ATTENUATION AND THE SPOT CONE (LIGHTING_FIX fixes 4 and 5).
//
// Both fixes are about the same thing: the numbers a user types were not the
// numbers the renderer used, and the editor's own wireframes drew the numbers
// rather than the renderer.
//
// FIX 4 — THE RANGE (F-A1..A4). `setLight` called
// `setAttenuationBasedOnRadius(range, 0.01f)`, which reads the range as the
// radius of the falloff CURVE and then solves for the distance at which the
// light dims to 1% of its peak. That distance is sqrt(199) = 14.1 times the
// number typed (OgreLight.cpp:194-217). So a light authored at range 5 lit out
// to 70 units, the Forward+ cut-off sat 14x too far out, and the range circle
// the editor draws at 5 was decoration. `setAttenuation(r, 0.5, 0, 0.5/r^2)`
// keeps Ogre's own curve and puts the cut-off where the user put it.
//
// FIX 5 — THE CONE (F-S1). `spotCutOff` is a HALF angle everywhere in the
// document — the editor's cone wire is `radius = range * tan(spotCutOff)` —
// while `Light::setSpotlightRange` wants the FULL apex angle. The half angle
// went straight through, so every spot rendered a cone half as wide as the wire
// drawn around it.
//
// HOW IT IS MEASURED. A single small probe patch on the floor, moved to a known
// radius from the light, with the camera moved with it and looking straight
// down at it: the centre pixel is then that patch and nothing else. No
// world-to-pixel arithmetic, no reliance on a projection — just "is the floor
// lit HERE?". The light's own geometry is the only variable.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

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
// Fix 4: a POINT light's illumination must end at the authored range — which is
// exactly the radius the editor's range circle is drawn at (scenemirror.cpp
// scales the ring mesh by `light->distance`), so this is the "wire radius ==
// cut-off" assertion, measured in pixels rather than asserted in code.
// ---------------------------------------------------------------------------
static void pointRangeCase(Engine *engine, View *view)
{
    const float R = 6.0f;
    std::printf("-- point light, authored range %.1f (the editor's range circle)\n", R);
    Scene *s = engine->createScene("falloff_point");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // A small white patch, moved around; nothing else is in the scene, so the
    // only thing that can light it is the point light.
    const NodeId probe = enginetest::addTestCube(s, Colour(0.9f, 0.9f, 0.9f), 0.0f, 0.9f);
    enginetest::setNodeScale(s, probe, Vec3(1.2f, 0.05f, 1.2f));

    const NodeId lightNode = s->createNode();
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1, 1, 1);
    // Low enough that nothing saturates: every probe below is a measurement,
    // and a clipped 1.000 would hide the very falloff being measured.
    l.intensity = 1.2f;
    l.range = R;
    l.castShadows = false;
    s->setNodeTransform(lightNode, Vec3(0.0f, 0.4f, 0.0f), Quat(), Vec3(1, 1, 1));
    s->setLight(lightNode, l);

    const float at0    = probeAt(engine, view, s, probe, 0.0f, 0.0f);
    const float atHalf = probeAt(engine, view, s, probe, R * 0.5f, 0.0f);
    const float atEdge = probeAt(engine, view, s, probe, R * 0.98f, 0.0f);
    const float beyond = probeAt(engine, view, s, probe, R * 1.6f, 0.0f);
    std::printf("   luminance:  d=0 %.3f   d=R/2 %.3f   d=0.98R %.3f   d=1.6R %.3f\n",
                at0, atHalf, atEdge, beyond);

    CHECK(at0 > 0.05f, "the light lights anything at all (the test is not vacuous)");
    CHECK(beyond < 0.005f,
          "NOTHING is lit past the authored range (it used to reach 14.1x further)");
    CHECK(atEdge < at0 * 0.25f,
          "the light has genuinely faded out by the range circle, not stopped abruptly");
    CHECK(atHalf > 0.0f && atHalf < at0,
          "...and falls off smoothly on the way there");

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
    l.intensity = 1.6f;            // see the note in the point case: no saturation
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
