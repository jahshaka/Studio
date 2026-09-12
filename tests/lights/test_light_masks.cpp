// LIGHTING CHANNELS (light masks) — "this light only affects these objects".
//
// The capability is Ogre's own (Light::setLightMask x MovableObject::
// setLightMask), but it is COMPILED OUT unless the engine is built with
// OGRE_CONFIG_ENABLE_FINE_LIGHT_MASK_GRANULARITY=ON: without the flag both
// setters still compile, still link and still STORE, and nothing whatsoever
// reads them. build-ogre.sh passes the flag and greps the installed
// OgreBuildSettings.h for OGRE_NO_FINE_LIGHT_MASK_GRANULARITY 0 — and this
// suite is the runtime half of that guard. If somebody drops the flag, the
// masking cases here go red rather than the feature quietly evaporating.
//
// WHAT IS MEASURED, and why each case exists:
//   1. DEFAULTS LIGHT EVERYTHING. Both cubes lit before any mask is set. This
//      is the "turning the flag on moved no pixels" argument, asserted rather
//      than assumed.
//   2. MASKING THE LIGHT ALONE DOES NOTHING. A light on channel 1 still lights
//      an object that is on every channel, because the test is an INTERSECTION
//      and the object's default is all-ones. This is the semantic everyone gets
//      wrong once, so it is pinned.
//   3. THE REAL CASE. Light on channel 1; cube A on channel 1, cube B on
//      channel 2. A is lit by it, B is not — while a second, unmasked light
//      lights both. Two colours (red masked, blue unmasked) so one readPixels
//      answers "which light lit this" per cube instead of just "how bright".
//   4. BOTH LIGHT PATHS. Case 3 runs twice: once with DIRECTIONAL lights (the
//      forward path, hlms_fine_light_mask) and once with POINT lights (the
//      Forward+ clustered path, hlms_forwardplus_fine_light_mask). They are two
//      independent #if blocks in Ogre and two independent shader properties;
//      one of them working proves nothing about the other.
//   5. AN EMPTY MASK IS LEGAL and means "no direct light at all".
//   6. SHADOWS ARE STILL NOT FILTERED — measured again by the sun lane, with a
//      probe that can finally tell the two states apart, and the answer did not
//      change. An object masked off a light still renders into that light's
//      shadow map and still darkens the ground for the objects the light does
//      light. The owner asked for Unreal's behaviour here (channels gating
//      shadows); the lane's attempt at it is recorded in the report, and this
//      case stays the honest characterisation until it lands.
//
// HOW A CUBE IS MEASURED: the camera is put in front of exactly one cube and
// the centre pixel is read. No projection arithmetic, no dependence on where
// anything lands on screen.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <algorithm>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

namespace {

constexpr unsigned kChannel1 = 1u << 1;
constexpr unsigned kChannel2 = 1u << 2;
constexpr unsigned kAll = 0xFFFFFFFFu;

struct Rgb { float r = 0, g = 0, b = 0; };

/// The colour of the cube at `x`, seen head-on from +Z.
Rgb probeCube(Engine *engine, View *view, float x)
{
    enginetest::testCameraLookAt(view, Vec3(x, 0.0f, 3.0f), Vec3(x, 0.0f, 0.0f));
    for (int i = 0; i < 3; ++i) engine->renderOneFrame();
    Image img;
    if (!view->readPixels(img)) return Rgb{};
    const Colour c = img.at(img.width / 2, img.height / 2);
    return Rgb{ c.r, c.g, c.b };
}

/// Two white cubes at -1.5 and +1.5, and a scene with no ambient at all: every
/// photon in the picture came from a light, so "unlit" reads as black.
struct TwoCubes {
    Scene  *scene = nullptr;
    NodeId  a = 0, b = 0;
};

TwoCubes buildTwoCubes(Engine *engine, View *view, const char *name)
{
    TwoCubes t;
    t.scene = engine->createScene(name);
    view->setScene(t.scene);
    t.scene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    // Rough and non-metallic: a diffuse response, so the measured colour is the
    // light's colour and not a specular highlight's position.
    t.a = enginetest::addTestCube(t.scene, Colour(0.9f, 0.9f, 0.9f), 0.0f, 0.9f);
    t.b = enginetest::addTestCube(t.scene, Colour(0.9f, 0.9f, 0.9f), 0.0f, 0.9f);
    enginetest::setNodePosition(t.scene, t.a, Vec3(-1.5f, 0.0f, 0.0f));
    enginetest::setNodePosition(t.scene, t.b, Vec3(1.5f, 0.0f, 0.0f));
    return t;
}

/// A light of `type` shining from +Z at the cubes, so both cubes' front faces
/// get the same treatment and neither is favoured by geometry.
NodeId addFacingLight(Scene *s, LightType type, const Colour &colour, unsigned mask)
{
    const NodeId n = s->createNode();
    LightDesc d;
    d.type = type;
    d.colour = colour;
    d.castShadows = false;
    d.lightMask = mask;
    if (type == LightType::Directional) {
        d.intensity = 1.2f;
        // Lights shine down their node's -Y; rotate -Y onto -Z (pitch +90 about
        // X) so the light travels from +Z toward the cubes.
        const float h = 0.70710678f;
        s->setNodeTransform(n, Vec3(0.0f, 0.0f, 6.0f), Quat(h, 0.0f, 0.0f, h), Vec3(1, 1, 1));
    } else {
        d.intensity = 4.0f;
        d.range = 20.0f;
        s->setNodeTransform(n, Vec3(0.0f, 0.0f, 4.0f), Quat(), Vec3(1, 1, 1));
    }
    s->setLight(n, d);
    return n;
}

const char *pathName(LightType t) { return t == LightType::Directional ? "directional" : "point"; }

// ---------------------------------------------------------------------------
// Cases 1, 2, 3 and 5, for one light path.
// ---------------------------------------------------------------------------
void maskingCase(Engine *engine, View *view, LightType type)
{
    std::printf("-- %s lights: masks filter direct lighting per object\n", pathName(type));
    TwoCubes t = buildTwoCubes(engine, view, type == LightType::Directional ? "masks_dir"
                                                                            : "masks_point");
    // RED light, masked later. Both cubes start on every channel.
    const NodeId red = addFacingLight(t.scene, type, Colour(1.0f, 0.0f, 0.0f), kAll);

    // --- 1. DEFAULTS: everything lights everything -------------------------
    Rgb a = probeCube(engine, view, -1.5f);
    Rgb b = probeCube(engine, view, 1.5f);
    std::printf("   defaults:        A r=%.3f  B r=%.3f\n", a.r, b.r);
    CHECK(a.r > 0.05f && b.r > 0.05f,
          "with default masks the light lights BOTH cubes (nothing is filtered)");

    // --- 2. MASKING THE LIGHT ALONE CHANGES NOTHING ------------------------
    LightDesc d;
    d.type = type;
    d.colour = Colour(1.0f, 0.0f, 0.0f);
    d.castShadows = false;
    d.lightMask = kChannel1;
    d.intensity = (type == LightType::Directional) ? 1.2f : 4.0f;
    d.range = 20.0f;
    t.scene->setLight(red, d);
    const Rgb aLightOnly = probeCube(engine, view, -1.5f);
    const Rgb bLightOnly = probeCube(engine, view, 1.5f);
    std::printf("   light on ch1:    A r=%.3f  B r=%.3f\n", aLightOnly.r, bLightOnly.r);
    CHECK(aLightOnly.r > 0.05f && bLightOnly.r > 0.05f,
          "restricting the LIGHT alone filters nothing: objects are on every channel by "
          "default, and the test is an intersection");

    // --- 3. THE REAL CASE --------------------------------------------------
    t.scene->setNodeLightMask(t.a, kChannel1);
    t.scene->setNodeLightMask(t.b, kChannel2);
    CHECK(t.scene->nodeLightMask(t.a) == kChannel1 && t.scene->nodeLightMask(t.b) == kChannel2,
          "the engine reports back the masks it was given");
    a = probeCube(engine, view, -1.5f);
    b = probeCube(engine, view, 1.5f);
    std::printf("   A ch1 / B ch2:   A r=%.3f  B r=%.3f\n", a.r, b.r);
    CHECK(a.r > 0.05f, "cube A, on the light's channel, is still lit by it");
    CHECK(b.r < 0.01f, "cube B, on another channel, is NOT lit by it");

    // ...and a second, UNMASKED light still lights both — the half that makes
    // this a channel system rather than a visibility flag.
    addFacingLight(t.scene, type, Colour(0.0f, 0.0f, 1.0f), kAll);
    a = probeCube(engine, view, -1.5f);
    b = probeCube(engine, view, 1.5f);
    std::printf("   + unmasked blue: A r=%.3f b=%.3f   B r=%.3f b=%.3f\n", a.r, a.b, b.r, b.b);
    CHECK(a.r > 0.05f && a.b > 0.05f, "cube A is lit by BOTH the masked and the unmasked light");
    CHECK(b.r < 0.01f && b.b > 0.05f,
          "cube B is lit ONLY by the unmasked light (the masked one still misses it)");

    // --- 5. AN EMPTY MASK IS LEGAL -----------------------------------------
    t.scene->setNodeLightMask(t.b, 0u);
    b = probeCube(engine, view, 1.5f);
    std::printf("   B mask 0:        B r=%.3f g=%.3f b=%.3f\n", b.r, b.g, b.b);
    CHECK(b.r < 0.01f && b.g < 0.01f && b.b < 0.01f,
          "a node on NO channel receives no direct light at all");

    view->setScene(nullptr);
    engine->destroyScene(t.scene);
}

// ---------------------------------------------------------------------------
// Case 4b: the mask survives an Item REBUILD. attachMesh destroys and recreates
// the Ogre Item (a material swap does this on the production path), and a fresh
// Item is born with Ogre's all-ones default — so the engine has to re-apply the
// node's mask or a masked object silently starts being lit again the moment its
// material changes. Not a hypothetical: it is the same class of bug the
// pickable query flags had.
// ---------------------------------------------------------------------------
void maskSurvivesItemRebuild(Engine *engine, View *view)
{
    std::printf("-- an Item rebuilt by a material swap keeps its channels\n");
    TwoCubes t = buildTwoCubes(engine, view, "masks_rebuild");
    addFacingLight(t.scene, LightType::Directional, Colour(1.0f, 0.0f, 0.0f), kChannel1);
    t.scene->setNodeLightMask(t.b, kChannel2);
    Rgb b = probeCube(engine, view, 1.5f);
    CHECK(b.r < 0.01f, "cube B starts masked off the light");

    // A NEW material on the same node: attachMesh tears the Item down and
    // builds another one.
    const MeshId mesh = t.scene->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = Colour(0.9f, 0.9f, 0.9f);
    p.roughness = 0.9f;
    const MaterialId mat = t.scene->createPbrMaterial(p);
    CHECK(t.scene->attachMesh(t.b, mesh, mat), "the mesh re-attached (the Item was rebuilt)");
    b = probeCube(engine, view, 1.5f);
    std::printf("   after re-attach: B r=%.3f\n", b.r);
    CHECK(b.r < 0.01f, "...and cube B is STILL masked off the light");
    CHECK(t.scene->nodeLightMask(t.b) == kChannel2, "the engine still reports the mask");

    view->setScene(nullptr);
    engine->destroyScene(t.scene);
}

// ---------------------------------------------------------------------------
// Case 6: THE SHADOW VERDICT. Not an assumption — a measurement, with the
// answer written down either way.
//
// A ground plane, a blocker cube above it, and one shadow-casting directional
// light tilted so the shadow lands beside the cube. First with the blocker on
// the light's channel (the ordinary case: lit blocker, dark shadow), then with
// the blocker masked OFF the light. If the caster pass filtered by mask, the
// shadow would disappear; it does not, and the test says so.
//
// THE PROBE MATTERS MORE THAN IT LOOKS (sun lane, 2026-09-13). This case used
// to scan the WHOLE ground row, which includes the blocker's own top face — and
// a blocker masked off the light is as dark as a shadow, so the probe reported
// "something dark is there" in both states and could not have detected a
// change. It now scans only the strip the shadow falls on, which is what makes
// the verdict below worth anything.
// ---------------------------------------------------------------------------
void shadowsIgnoreMasks(Engine *engine, View *view)
{
    std::printf("-- shadow interaction: does a masked-off object still cast?\n");
    Scene *s = engine->createScene("masks_shadow");
    view->setScene(s);
    s->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.05f, 0.05f, 0.05f));

    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = Colour(0.9f, 0.9f, 0.9f);
    p.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(p);

    const NodeId ground = s->createNode();
    s->attachMesh(ground, mesh, mat);
    s->setNodeTransform(ground, Vec3(0, -0.55f, 0), Quat(), Vec3(8, 0.1f, 8));
    const NodeId blocker = s->createNode();
    s->attachMesh(blocker, mesh, mat);
    s->setNodeTransform(blocker, Vec3(0, 0.6f, 0), Quat(), Vec3(0.8f, 0.8f, 0.8f));

    const NodeId sun = s->createNode();
    LightDesc d;
    d.type = LightType::Directional;
    d.intensity = 3.0f;
    d.castShadows = true;
    d.lightMask = kChannel1;   // the ground is on every channel, so it stays lit
    s->setLight(sun, d);
    // Roll 45 about Z: the sun tilts toward +X and the shadow lands beside the
    // cube where the camera can see it (the same rig test_engine's shadow cases
    // use).
    s->setNodeTransform(sun, Vec3(0, 5, 0), Quat(0, 0, 0.3826834f, 0.9238795f), Vec3(1, 1, 1));
    view->setShadows(true);

    CameraDesc c;
    c.position = Vec3(0, 6, 0.01f);
    c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f);
    c.fovDegrees = 50;
    view->setCamera(c);

    // CONTRAST ACROSS THE GROUND STRIP THE SHADOW FALLS ON, and NOT across the
    // caster itself — which is the whole difficulty of measuring this. The sun
    // is rolled 45 degrees about Z, so the cube's shadow lands to its +X side
    // (measured: pixels ~76-100 of 128 with this camera), while the cube's own
    // top face covers the middle ~56-72. An UNLIT cube is as dark as a shadow,
    // so a probe that includes the cube reports "something dark is there" in
    // both states and can never tell the two apart — the original version of
    // this case did exactly that, which is why it read the old behaviour as
    // "the shadow survives" even where it does not.
    const unsigned shadowLo = 74, shadowHi = 112;
    auto groundContrast = [&](int *darkestOut) {
        for (int i = 0; i < 10; ++i) engine->renderOneFrame();
        Image img;
        if (!view->readPixels(img)) return -1;
        int darkest = 255, brightest = 0;
        for (unsigned x = shadowLo; x < std::min(shadowHi, img.width - 2); ++x) {
            const Colour q = img.at(x, img.height / 2);
            const int l = int(std::lround((q.r + q.g + q.b) / 3.0f * 255.0f));
            darkest = std::min(darkest, l);
            brightest = std::max(brightest, l);
        }
        if (darkestOut) *darkestOut = darkest;
        return brightest - darkest;
    };
    int darkOn = 0;
    const int contrastOnChannel = groundContrast(&darkOn);
    std::printf("   blocker ON the light's channel:  ground contrast %d (darkest %d)\n",
                contrastOnChannel, darkOn);
    CHECK(contrastOnChannel > 40, "the blocker casts a visible shadow to begin with");

    // Now take the blocker OFF the light's channel. It stops RECEIVING that
    // light (it goes near-black, ambient only) — the question is the shadow.
    s->setNodeLightMask(blocker, kChannel2);
    int darkOff = 0;
    const int contrastOffChannel = groundContrast(&darkOff);
    std::printf("   blocker OFF the light's channel: ground contrast %d (darkest %d)\n",
                contrastOffChannel, darkOff);
    // THE VERDICT, asserted as what actually happens at this pin: the shadow
    // survives. Ogre's caster pass renders every shadow caster into the light's
    // one shadow map and has no receiver to compare a mask against, so masking
    // an object off a light hides the light FROM it, not it from the light.
    CHECK(contrastOffChannel > 40,
          "SHADOWS ARE NOT MASKED: an object masked off a light still casts its shadow");
    CHECK(std::abs(contrastOffChannel - contrastOnChannel) < 25,
          "...and the shadow is essentially unchanged, not merely present");

    s->setNodeLightMask(blocker, kChannel1);

    // The control that proves the measurement can move at all: with the caster
    // hidden outright the contrast collapses.
    s->setNodeVisible(blocker, false);
    const int contrastHidden = groundContrast(nullptr);
    std::printf("   blocker hidden:                  ground contrast %d\n", contrastHidden);
    CHECK(contrastHidden < contrastOnChannel / 2,
          "the control: hiding the caster DOES remove the shadow (the probe is not blind)");

    view->setShadows(false);
    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// Case 7: PER-OBJECT CAST SHADOW (SUN_AND_LIGHT_DEFAULTS §2.4), the escape
// hatch the spec recommends INSTEAD of channel-filtered shadows — and the end
// of a wire that was dead for years: iris::SceneNode::castShadow has been
// serialized, reflected and set to false by the default floor since long before
// the engine had anywhere to put it, and nothing pushed it anywhere.
//
// Same rig as case 6, so the two verdicts are directly comparable: the mask
// leaves the shadow alone, this removes it outright.
// ---------------------------------------------------------------------------
void perObjectCastShadow(Engine *engine, View *view)
{
    std::printf("-- per-object Cast Shadow\n");
    Scene *s = engine->createScene("masks_castshadow");
    view->setScene(s);
    s->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.05f, 0.05f, 0.05f));

    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = Colour(0.9f, 0.9f, 0.9f);
    p.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(p);

    const NodeId ground = s->createNode();
    s->attachMesh(ground, mesh, mat);
    s->setNodeTransform(ground, Vec3(0, -0.55f, 0), Quat(), Vec3(8, 0.1f, 8));
    const NodeId blocker = s->createNode();
    s->attachMesh(blocker, mesh, mat);
    s->setNodeTransform(blocker, Vec3(0, 0.6f, 0), Quat(), Vec3(0.8f, 0.8f, 0.8f));

    const NodeId sun = s->createNode();
    LightDesc d;
    d.type = LightType::Directional;
    d.intensity = 3.0f;
    d.castShadows = true;
    s->setLight(sun, d);
    s->setNodeTransform(sun, Vec3(0, 5, 0), Quat(0, 0, 0.3826834f, 0.9238795f), Vec3(1, 1, 1));
    view->setShadows(true);

    CameraDesc c;
    c.position = Vec3(0, 6, 0.01f);
    c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f);
    c.fovDegrees = 50;
    view->setCamera(c);

    const unsigned shadowLo = 74, shadowHi = 112;
    auto shadowStrip = [&]() {
        for (int i = 0; i < 4; ++i) engine->renderOneFrame();
        Image img;
        if (!view->readPixels(img)) return -1;
        int darkest = 255;
        for (unsigned x = shadowLo; x < std::min(shadowHi, img.width - 2); ++x) {
            const Colour q = img.at(x, img.height / 2);
            darkest = std::min(darkest, int(std::lround((q.r + q.g + q.b) / 3.0f * 255.0f)));
        }
        return darkest;
    };

    CHECK(s->nodeCastShadow(blocker), "a node is born casting (Ogre's own default)");
    const int withShadow = shadowStrip();
    std::printf("   casting:            darkest %d\n", withShadow);
    CHECK(withShadow < 40, "the blocker's shadow is on the ground to begin with");

    s->setNodeCastShadow(blocker, false);
    const int withoutShadow = shadowStrip();
    std::printf("   Cast Shadow = off:  darkest %d\n", withoutShadow);
    CHECK(!s->nodeCastShadow(blocker), "the engine reports the flag back");
    CHECK(withoutShadow > 200,
          "PER-OBJECT CAST SHADOW WORKS: the ground is clear where the shadow was");

    // ...and the object is still THERE and still lit: this hides the shadow,
    // not the thing.
    {
        Image img;
        view->readPixels(img);
        const Colour centre = img.at(img.width / 2, img.height / 2);
        std::printf("   ...the blocker itself: %.2f\n", centre.r);
        CHECK(centre.r > 0.3f, "...while the blocker is still visible and still lit");
    }

    s->setNodeCastShadow(blocker, true);
    const int backAgain = shadowStrip();
    std::printf("   Cast Shadow = on:   darkest %d\n", backAgain);
    CHECK(backAgain < 40, "turning it back on restores the shadow");

    // IT SURVIVES AN ITEM REBUILD, for the reason the light mask does: a
    // material swap destroys and recreates the Item, and a fresh Item is born
    // casting.
    s->setNodeCastShadow(blocker, false);
    CHECK(s->attachMesh(blocker, mesh, s->createPbrMaterial(p)),
          "the mesh re-attached (the Item was rebuilt)");
    const int afterRebuild = shadowStrip();
    std::printf("   after re-attach:    darkest %d\n", afterRebuild);
    CHECK(afterRebuild > 200, "...and the node still casts nothing");

    view->setShadows(false);
    view->setScene(nullptr);
    engine->destroyScene(s);
}

}  // namespace

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-light-masks-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("masks", 128, 128, Colour(0, 0, 0));
    maskingCase(engine.get(), view, LightType::Directional);
    maskingCase(engine.get(), view, LightType::Point);
    maskSurvivesItemRebuild(engine.get(), view);
    shadowsIgnoreMasks(engine.get(), view);
    perObjectCastShadow(engine.get(), view);

    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
