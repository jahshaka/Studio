// SHADOW HYGIENE AND THE LIGHT-COUNT BUDGETS (LIGHTING_FIX fixes 6 and 7).
//
// FIX 6 — HELPERS OUT OF THE SHADOW ATLAS (F-D1, F-D2). The shadow node was
// created with `createShadowNodeWithSettings`' DEFAULT visibility mask, which is
// everything. Editor helpers — the ground grid, light range wires, light icons,
// camera bodies — were therefore rendered into the shadow atlas and cast
// shadows on the scene; worse, they counted towards
// `SceneManager::getCurrentCastersBox()`, which is what the PSSM setup fits its
// splits to, so a 100-unit range wire around a small scene threw most of the
// shadow map's resolution at empty air. Helpers carry kHelperBit INSTEAD OF
// kVisibleBit, so passing kVisibleBit as the mask removes them from both in one
// argument. This suite asserts the observable consequence: the shadows a scene
// renders must not depend on whether its helpers are visible.
//
// FIX 7 — THE LIGHT-COUNT BUDGETS (F-L1, F-L2). Ogre hardcodes the exact number
// of non-caster directional lights, and the exact COMBINATION of shadow-casting
// spot and point lights, into every PBS shader. In an editor, where the light
// list changes constantly, that means a full shader rebuild of the scene every
// time a light is added, removed, or has its cast-shadows box ticked.
// `setMaxNonCasterDirectionalLights(4)` and `setStaticBranchingLights(true)`
// replace both with static branching. Asserted with the shader-cache's own
// compile counter, which is the only thing that can see it: the picture is
// identical either way, and the cost is a stall.
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

static void render(Engine *e, int frames = 4)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

// ---------------------------------------------------------------------------
// Fix 6: the same scene, rendered with a big editor helper visible and then
// hidden. Every pixel must match — the helper is not in the picture (it is
// behind the camera / out of frame), so the only way it could change anything
// is through the shadow atlas.
// ---------------------------------------------------------------------------
static void shadowHygieneCase(Engine *engine, View *view)
{
    std::printf("-- helpers must not reach the shadow atlas\n");
    Scene *s = engine->createScene("hygiene_shadow");
    view->setScene(s);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.02f, 0.02f, 0.03f));

    // Floor + a caster above it, lit by a directional light: a real shadow.
    const NodeId floor = enginetest::addTestCube(s, Colour(0.85f, 0.85f, 0.85f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    enginetest::setNodeScale(s, floor, Vec3(12.0f, 0.2f, 12.0f));
    const NodeId caster = enginetest::addTestCube(s, Colour(0.8f, 0.3f, 0.3f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, caster, Vec3(0.0f, 1.2f, 0.0f));
    enginetest::setNodeScale(s, caster, Vec3(1.4f, 1.4f, 1.4f));
    enginetest::addDirectionalLight(s, Vec3(0.45f, -1.0f, 0.0f), 4.0f);
    // Looking straight down, so the cast shadow lies beside the caster and a
    // horizontal row of floor pixels crosses it (the shape tests/engine's
    // shadows_darken_the_ground uses).
    enginetest::testCameraLookAt(view, Vec3(0.0f, 12.0f, 0.01f), Vec3(0.0f, 0.0f, 0.0f));
    // SHADOWS ARE OFF BY DEFAULT ON A VIEW. Without this the suite would be
    // comparing two unshadowed frames and passing for the wrong reason.
    view->setShadows(true);
    CHECK(view->shadows(), "the view renders shadows");

    // THE HELPER: an enormous flat plate far off to the side and well above the
    // camera, marked as an editor helper exactly as the mirror marks the ground
    // grid and the light wires. If it reached the shadow atlas it would both
    // cast onto the floor and blow the caster box out to 200 units.
    const NodeId helper = enginetest::addTestCube(s, Colour(1.0f, 1.0f, 1.0f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, helper, Vec3(0.0f, 30.0f, 0.0f));
    enginetest::setNodeScale(s, helper, Vec3(200.0f, 0.5f, 200.0f));
    s->setNodeHelper(helper, true);
    CHECK(s->nodeHelper(helper), "the plate is marked as an editor helper");

    render(engine, 6);
    Image withHelper; view->readPixels(withHelper);

    s->setNodeVisible(helper, false);
    render(engine, 6);
    Image withoutHelper; view->readPixels(withoutHelper);

    // Compare the whole frame, not one pixel: a shadow moving is a regional
    // change and a single probe can miss it.
    double worst = 0.0; int worstX = 0, worstY = 0;
    for (int y = 0; y < 128; y += 2) {
        for (int x = 0; x < 128; x += 2) {
            const Colour a = withHelper.at(x, y), b = withoutHelper.at(x, y);
            const double d = std::max(std::max(std::fabs(double(a.r - b.r)),
                                               std::fabs(double(a.g - b.g))),
                                      std::fabs(double(a.b - b.b)));
            if (d > worst) { worst = d; worstX = x; worstY = y; }
        }
    }
    std::printf("   worst per-channel difference: %.4f at (%d,%d)\n", worst, worstX, worstY);
    // The floor really is shadowed, or the comparison proves nothing: scan the
    // row the shadow falls across and look for contrast.
    float darkest = 1.0f, brightest = 0.0f;
    for (int x = 8; x < 120; ++x) {
        const Colour c = withoutHelper.at(x, 64);
        const float l = (c.r + c.g + c.b) / 3.0f;
        if (l < darkest) darkest = l;
        if (l > brightest) brightest = l;
    }
    std::printf("   floor row: darkest %.3f  brightest %.3f\n", darkest, brightest);
    CHECK(brightest - darkest > 0.10f, "the scene really does render a shadow (not vacuous)");
    CHECK(worst < 0.01f,
          "hiding the helper changes NOTHING: it was never in the shadow atlas (F-D1)");

    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// Fix 7: the light-list edits an editor makes constantly must not recompile
// shaders on a warm cache.
// ---------------------------------------------------------------------------
static void budgetCase(Engine *engine, View *view)
{
    std::printf("-- light-list edits must not recompile shaders\n");
    Scene *s = engine->createScene("hygiene_budget");
    view->setScene(s);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.02f, 0.02f, 0.03f));

    const NodeId floor = enginetest::addTestCube(s, Colour(0.85f, 0.85f, 0.85f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    enginetest::setNodeScale(s, floor, Vec3(12.0f, 0.2f, 12.0f));
    const NodeId cube = enginetest::addTestCube(s, Colour(0.8f, 0.3f, 0.3f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, cube, Vec3(0.0f, 1.0f, 0.0f));
    enginetest::addDirectionalLight(s, Vec3(0.35f, -1.0f, 0.25f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 7.0f), Vec3(0.0f, 0.5f, 0.0f));

    // TWO spots. One always casts; the other is the one toggled below. Same
    // edge as the directional case above and for the same upstream reason:
    // static branching pins the shadow-map light COUNT once at least one such
    // light exists, so the 1 -> 0 -> 1 round trip still changes the property.
    // An editor scene that has any shadowed spot at all is the case that
    // matters, and it is the one measured.
    const NodeId anchorSpot = s->createNode();
    {
        LightDesc a;
        a.type = LightType::Spot;
        a.intensity = 6.0f;
        a.range = 20.0f;
        a.spotAngleDegrees = 30.0f;
        a.castShadows = true;
        s->setNodeTransform(anchorSpot, Vec3(-2.0f, 6.0f, -2.0f), Quat(), Vec3(1, 1, 1));
        s->setLight(anchorSpot, a);
    }
    const NodeId spotNode = s->createNode();
    LightDesc spot;
    spot.type = LightType::Spot;
    spot.intensity = 8.0f;
    spot.range = 20.0f;
    spot.spotAngleDegrees = 35.0f;
    spot.castShadows = true;
    s->setNodeTransform(spotNode, Vec3(2.0f, 6.0f, 2.0f), Quat(), Vec3(1, 1, 1));
    s->setLight(spotNode, spot);

    // ONE non-caster directional to start with, and it is part of the WARM-UP,
    // not part of the measurement.
    //
    // THE HONEST EDGE, stated so nobody re-discovers it as a bug: the clamp
    // upstream applies is `if( mNumLightsLimit > 0 && numDirectional >
    // shadowCasterDirectional )` (OgreHlms.cpp:3546-3551), so it only kicks in
    // once a non-caster directional EXISTS. The 0 -> 1 transition (and the
    // N -> 0 one) therefore still changes the property and still compiles; every
    // count from 1 upwards is free. That is the editor case — a scene with fill
    // lights whose count is being fiddled with — and it is what is measured.
    const NodeId seed = s->createNode();
    {
        LightDesc d;
        d.type = LightType::Directional;
        d.intensity = 0.4f;
        d.castShadows = false;
        s->setNodeTransform(seed, Vec3(0, 3, 0), Quat(), Vec3(1, 1, 1));
        s->setLight(seed, d);
    }

    // WARM THE CACHE FIRST. The point is "no recompiles after the shaders
    // exist"; counting the first compile of a fresh scene would measure the
    // scene's existence, not the edits.
    render(engine, 8);
    const unsigned warm = engine->shaderCacheStats().compiledThisRun;
    std::printf("   shaders compiled warming up: %u\n", warm);

    // 1. ADD AND REMOVE NON-CASTER DIRECTIONAL LIGHTS (F-L1). Ogre's default
    //    hardcodes the count, so each of these was a full scene recompile.
    NodeId fills[3] = { 0, 0, 0 };
    for (int i = 0; i < 3; ++i) {
        fills[i] = s->createNode();
        LightDesc d;
        d.type = LightType::Directional;
        d.intensity = 0.4f;
        d.castShadows = false;
        s->setNodeTransform(fills[i], Vec3(0, 3, 0), Quat(), Vec3(1, 1, 1));
        s->setLight(fills[i], d);
        render(engine, 2);
    }
    for (int i = 0; i < 3; ++i) { s->removeNode(fills[i]); render(engine, 2); }
    const unsigned afterDirectionals = engine->shaderCacheStats().compiledThisRun;
    std::printf("   after adding and removing 3 directional lights: %u compiles\n",
                afterDirectionals - warm);
    CHECK(afterDirectionals == warm,
          "adding/removing non-caster directional lights compiles NOTHING (F-L1)");

    // 2. CHANGE THE SHADOW-CASTER MIX AT A CONSTANT COUNT (F-L2). This is what
    //    `setStaticBranchingLights(true)` actually buys, and the distinction
    //    matters enough to state precisely — the brief expected more:
    //
    //    Upstream keys PBS shaders on the COMBINATION of shadow-casting spot and
    //    point lights ("if you have 3 spot and 5 point and it changes to 4 and 4
    //    you'll get the next set of shaders", OgreHlms.h:718-726). Static
    //    branching collapses that: `setProperty( LightsPoint, 0 )`
    //    (OgreHlms.cpp:3673-3675), so the mix stops being part of the key.
    //
    //    What it does NOT collapse, A/B-MEASURED ON THIS PIN by this very
    //    suite (engine built with and without the two budget calls):
    //        4 spot<->point swaps, no budgets: 4 compiles
    //        4 spot<->point swaps, budgets:    2 compiles
    //        add+remove 3 directionals, no budgets: 6 compiles
    //        add+remove 3 directionals, budgets:    0 compiles
    //    So the directional half is exactly what upstream promises, and the
    //    shadow-caster half is a HALVING, not an elimination:
    //    `hlms_num_shadow_map_lights` is set to the live count unconditionally
    //    (OgreHlms.cpp:3270-3273) — only an extra FLAG is gated on static
    //    branching — and a point caster and a spot caster do not use the same
    //    shadow map in our atlas (DPSM vs focused), so the swap still moves
    //    `hlms_shadowmapN_*`. Turning a caster on or off likewise still
    //    compiles. Asserted at the measured value, not at the hoped-for zero.
    const NodeId secondNode = s->createNode();
    LightDesc second;
    second.type = LightType::Spot;
    second.intensity = 6.0f;
    second.range = 20.0f;
    second.spotAngleDegrees = 30.0f;
    second.castShadows = true;
    s->setNodeTransform(secondNode, Vec3(-2.0f, 6.0f, 2.0f), Quat(), Vec3(1, 1, 1));
    s->setLight(secondNode, second);
    render(engine, 4);
    const unsigned beforeMix = engine->shaderCacheStats().compiledThisRun;

    // Same number of shadow casters throughout; only the SPOT/POINT mix moves.
    for (int i = 0; i < 4; ++i) {
        second.type = (i % 2) ? LightType::Point : LightType::Spot;
        s->setLight(secondNode, second);
        render(engine, 3);
    }
    const unsigned afterMix = engine->shaderCacheStats().compiledThisRun;
    std::printf("   after 4 spot<->point swaps at a constant caster count: %u compiles\n",
                afterMix - beforeMix);
    CHECK(afterMix - beforeMix <= 2,
          "changing the shadow-caster MIX costs at most 2 compiles (4 without the budgets)");

    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// Fix 8's counter (F-F2): what `app.renderStats()` surfaces about Forward+.
// ---------------------------------------------------------------------------
static void forwardPlusCensusCase(Engine *engine, View *view)
{
    std::printf("-- the Forward+ light census\n");
    Scene *s = engine->createScene("hygiene_census");
    view->setScene(s);
    const NodeId floor = enginetest::addTestCube(s, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodeScale(s, floor, Vec3(8.0f, 0.2f, 8.0f));
    enginetest::addDirectionalLight(s, Vec3(0.2f, -1.0f, 0.3f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 5.0f, 6.0f), Vec3(0.0f, 0.0f, 0.0f));
    render(engine, 2);

    RenderStats st;
    CHECK(engine->renderStats(st), "renderStats reports");
    std::printf("   directional only: forwardPlusLights=%u budget=%u over=%u\n",
                st.forwardPlusLights, st.forwardPlusBudget, st.forwardPlusOverBudget);
    CHECK(st.forwardPlusLights == 0,
          "a directional light does NOT ride the clustered list (it is in the pass buffer)");
    CHECK(st.forwardPlusBudget == 96, "the budget reported is the one createScene passes");
    CHECK(st.forwardPlusOverBudget == 0, "zero over budget PROVES no light can have been dropped");

    for (int i = 0; i < 12; ++i) {
        const NodeId n = s->createNode();
        LightDesc d;
        d.type = LightType::Point;
        d.intensity = 1.0f;
        d.range = 3.0f;
        d.castShadows = false;
        s->setNodeTransform(n, Vec3(float(i) - 6.0f, 1.0f, 0.0f), Quat(), Vec3(1, 1, 1));
        s->setLight(n, d);
    }
    render(engine, 2);
    CHECK(engine->renderStats(st), "renderStats reports again");
    std::printf("   plus 12 point lights: forwardPlusLights=%u over=%u\n",
                st.forwardPlusLights, st.forwardPlusOverBudget);
    CHECK(st.forwardPlusLights == 12, "point lights are counted");
    CHECK(st.forwardPlusOverBudget == 0, "12 lights are comfortably inside a 96-light budget");

    view->setScene(nullptr);
    engine->destroyScene(s);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-light-hygiene-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("hygiene", 128, 128, Colour(0, 0, 0));
    shadowHygieneCase(engine.get(), view);
    budgetCase(engine.get(), view);
    forwardPlusCensusCase(engine.get(), view);

    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
