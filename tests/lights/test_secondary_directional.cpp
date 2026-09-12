// lights.secondary_directional — ONLY THE SUN CASTS
// (SPECS/SUN_AND_LIGHT_DEFAULTS_SPEC.md §3.3, owner decision Q1).
//
// Our shadow node declares exactly ONE directional slot: three PSSM splits at
// slot 0, and every focused slot accepts spot/point only. So with two
// shadow-casting directional lights Ogre filled that slot from its own
// castShadows-then-LIGHT-ID sort — engine creation order, which can flip across
// a reload or a mirror re-attach. One of the two suns cast, silently, and which
// one was luck. That is the honest severity of the complaint the owner quoted:
// it is not a cost problem here, it is a NON-DETERMINISM problem.
//
// The fix is one boolean on the description: the host resolves which
// directional is the sun (iris::Scene::sunLight — one resolver, asked by
// everything) and the engine pushes `castShadows = false` on every other one,
// leaving Ogre's sort a single candidate.
//
// WHAT IS MEASURED. A ground plane and a blocker cube, lit by two directional
// lights rolled OPPOSITE ways so each would throw its shadow to a different
// side — and coloured differently, RED and BLUE, which is what makes the
// measurement possible at all: the two lights are equally bright, so wherever
// one is blocked the OTHER still lights the ground and a brightness probe sees
// nothing. Per CHANNEL it is unmistakable: in the red light's shadow the ground
// keeps its blue and loses its red.
//   1. light A primary  -> the shadow is on A's side, and only there;
//   2. light B primary  -> the shadow swaps sides, in the same process, with
//      nothing recreated: the priority BINDS, it is not a start-up accident;
//   3. it is STABLE: pushing the same descriptions ten times over never moves
//      it (the creation-order artefact would);
//   4. the secondary still LIGHTS the scene — it is a secondary light, not a
//      disabled one.
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

namespace {

/// Roll about Z: +45 degrees throws the shadow to +X, -45 to -X.
Quat rollZ(float degrees)
{
    const float h = degrees * 3.14159265f / 180.0f * 0.5f;
    return Quat(0, 0, std::sin(h), std::cos(h));
}

/// The least red and the least blue found in a horizontal band of the ground.
struct Probe { int redL = 255, blueL = 255, redR = 255, blueR = 255; };

}   // namespace

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-secondary-directional-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("secondary", 128, 128, Colour(0, 0, 0));
    Scene *s = engine->createScene("secondary_directional");
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

    // A throws its shadow to +X, B to -X. Both cast as far as the DOCUMENT is
    // concerned; which one is allowed to is what this suite is about.
    const NodeId a = s->createNode();
    const NodeId b = s->createNode();
    s->setNodeTransform(a, Vec3(0, 5, 0), rollZ(45.0f), Vec3(1, 1, 1));
    s->setNodeTransform(b, Vec3(0, 5, 0), rollZ(-45.0f), Vec3(1, 1, 1));

    auto push = [&](bool aIsSun) {
        LightDesc d;
        d.type = LightType::Directional;
        d.intensity = 3.0f;
        d.castShadows = true;
        d.colour = Colour(1.0f, 0.0f, 0.0f);     // A is RED, and throws to +X
        d.primaryDirectional = aIsSun;
        d.forwardShadingPriority = aIsSun ? 0 : 1;
        s->setLight(a, d);
        d.colour = Colour(0.0f, 0.0f, 1.0f);     // B is BLUE, and throws to -X
        d.primaryDirectional = !aIsSun;
        d.forwardShadingPriority = aIsSun ? 1 : 0;
        s->setLight(b, d);
    };

    view->setShadows(true);
    CameraDesc c;
    c.position = Vec3(0, 6, 0.01f);
    c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f);
    c.fovDegrees = 50;
    view->setCamera(c);

    // The darkest pixel each SIDE of the caster, skipping the caster's own
    // footprint (a cube's top face is as dark as a shadow when it is unlit, and
    // a probe that cannot tell them apart proves nothing — lights.masks case 6
    // learned that the hard way).
    auto probe = [&]() {
        for (int i = 0; i < 6; ++i) engine->renderOneFrame();
        Probe out;
        Image img;
        if (!view->readPixels(img)) return out;
        const unsigned mid = img.width / 2;
        const auto q8 = [](float v) { return int(std::lround(std::min(1.0f, v) * 255.0f)); };
        for (unsigned x = 16; x < mid - 10; ++x) {
            const Colour q = img.at(x, img.height / 2);
            out.redL = std::min(out.redL, q8(q.r));
            out.blueL = std::min(out.blueL, q8(q.b));
        }
        for (unsigned x = mid + 10; x < img.width - 16; ++x) {
            const Colour q = img.at(x, img.height / 2);
            out.redR = std::min(out.redR, q8(q.r));
            out.blueR = std::min(out.blueR, q8(q.b));
        }
        return out;
    };

    // ---- 1. A is the sun --------------------------------------------------
    push(true);
    Probe withA = probe();
    std::printf("-- A (red) primary: +X side red %d blue %d | -X side red %d blue %d\n",
                withA.redR, withA.blueR, withA.redL, withA.blueL);
    CHECK(withA.redR < 60, "the SUN (red) casts: its own colour is missing on its shadow's side");
    CHECK(withA.blueL > 120,
          "...and the SECONDARY (blue) casts nothing: its colour is everywhere on the other side");

    // ---- 2. the priority swaps, in the same process -----------------------
    push(false);
    Probe withB = probe();
    std::printf("-- B (blue) primary: +X side red %d blue %d | -X side red %d blue %d\n",
                withB.redR, withB.blueR, withB.redL, withB.blueL);
    CHECK(withB.blueL < 60, "making the other light the sun moves the shadow to ITS side");
    CHECK(withB.redR > 120, "...and the old sun, now secondary, casts nothing");

    // ---- 3. it is stable, not a start-up accident -------------------------
    bool stable = true;
    for (int i = 0; i < 10; ++i) {
        push(false);
        const Probe again = probe();
        stable = stable && again.blueL < 60 && again.redR > 120;
    }
    CHECK(stable, "ten re-pushes never move it: the answer is the priority, not creation order");

    // ---- 4. a secondary still LIGHTS -------------------------------------
    // Switch the sun off entirely and keep only the secondary: the ground is
    // still lit, which is what "secondary light, not disabled light" means.
    {
        LightDesc off;
        off.type = LightType::Directional;
        off.intensity = 0.0f;
        off.castShadows = false;
        off.primaryDirectional = false;
        s->setLight(b, off);                 // b was the sun; silence it
        LightDesc only;
        only.type = LightType::Directional;
        only.intensity = 3.0f;
        only.colour = Colour(1.0f, 0.0f, 0.0f);
        only.castShadows = true;
        only.primaryDirectional = false;     // deliberately NOT the sun
        only.forwardShadingPriority = 1;
        s->setLight(a, only);
        for (int i = 0; i < 6; ++i) engine->renderOneFrame();
        Image img;
        view->readPixels(img);
        const Colour q = img.at(img.width / 2 + 30, img.height / 2);
        const int lit = int(std::lround(std::min(1.0f, q.r) * 255.0f));
        std::printf("-- only a SECONDARY directional: ground %d\n", lit);
        CHECK(lit > 120, "a secondary directional light still lights the scene fully");
    }

    view->setShadows(false);
    view->setScene(nullptr);
    engine->destroyScene(s);
    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
