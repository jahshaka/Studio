// Planar reflections, pixel-asserted (PLANAR_REFLECTIONS_SPEC.md §8).
//
// THE SCENE, and why it is shaped exactly like this:
//
//   * A flat mirror plate (a unit cube scaled to 12 x 0.2 x 12) with its top
//     face at y = 0. Metal, roughness 0 — a mirror, so what it shows IS the
//     reflection and nothing else.
//   * A bright red EMISSIVE cube floating above it. Emissive so its colour does
//     not depend on lighting, shadows or ambient: any red on the floor came
//     from the reflection.
//   * A camera low and in front, looking along the floor, so the cube's mirror
//     image lands on the floor in the LOWER half of the frame while the cube
//     itself is in the upper half. The assertions scan the lower half only.
//
// WHAT EACH ASSERTION PROVES
//
//   1. Reflector OFF: the lower half of the frame carries no red.
//      Reflector ON:  a red blob appears. That is a whole extra scene render
//      landing on the floor — the feature working end to end.
//   2. Move the emitter away, keep the reflector on: the red goes. This is what
//      makes assertion 1 a proof about the REFLECTION rather than about some
//      constant the flag happens to switch on.
//   3. THE MIRROR DOES NOT SELF-REFLECT. This is not a separate case, it is why
//      assertion 1 can pass at all: the reflection camera sits BELOW the plane
//      looking up, and the plate is thick, so its own slab lies directly
//      between that camera and the emissive cube. Red on the floor therefore
//      proves the plate is not painted into its own reflection.
//
//      WHAT DOES THE EXCLUDING (this comment claimed the wrong thing until
//      2026-09-06): NOT a visibility bit of ours. Ogre mirrors the reflection
//      camera about the actor plane, and because that camera isReflected() the
//      Hlms enables a GLOBAL CLIP PLANE at the reflection plane itself
//      (OgreHlms.cpp:3729-3737 + OgreHlmsPbs.cpp:2220-2223) while the scene
//      manager inverts vertex winding (OgreSceneManager.cpp:1369-1372). The
//      plate is CLIPPED, not masked. The `~kNoReflectBit` pass mask that used
//      to sit in OgrePlanar.cpp was a no-op (Ogre's visibility test is
//      any-bit-set, which cannot express exclusion) and is gone.
//   4. THE ONE CASE THE CLIP PLANE + WINDING DO NOT COVER — a TWO-SIDED
//      reflector, which really does fill its own reflection with itself — is
//      REFUSED at the arm, and the refusal is about the MATERIAL: the same
//      plate is accepted once the material is single-sided again.
//   5. Budget clamp: three reflectors, budget 2 => countActiveActors() == 2.
//   6. Teardown with a live reflector — the ASan copy of this suite is what
//      catches the dangling HlmsPbs::mPlanarReflections the component's own
//      destructor leaves behind.
//   7. A non-plate mesh is REFUSED with a message, not silently accepted.
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

static void render(Engine *e, int frames = 3)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

/// The strongest "this pixel is red and its neighbours are not" signal in the
/// bottom half of the frame: max over pixels of (r - max(g, b)). A neutral
/// floor scores ~0 whatever its brightness, so the measure is immune to
/// exposure, ambient and the floor's own albedo.
static float maxRedExcessLowerHalf(const Image &img, int *outX = nullptr, int *outY = nullptr)
{
    float best = 0.0f;
    for (unsigned y = img.height / 2; y < img.height; ++y) {
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const float e = c.r - (c.g > c.b ? c.g : c.b);
            if (e > best) { best = e; if (outX) *outX = int(x); if (outY) *outY = int(y); }
        }
    }
    return best;
}

static float measure(Engine *e, View *v, const char *what)
{
    render(e, 3);
    Image img;
    if (!v->readPixels(img)) { std::printf("FAIL: readPixels (%s)\n", what); ++failures; return 0.0f; }
    int x = -1, y = -1;
    const float r = maxRedExcessLowerHalf(img, &x, &y);
    std::printf("   %s: max red excess (lower half) = %.3f at (%d,%d)\n", what, r, x, y);
    return r;
}

/// A flat plate: a unit cube scaled thin on Y. Returns the node.
static NodeId addPlate(Scene *s, const Vec3 &pos, const Vec3 &scale,
                       const Colour &albedo, float metalness, float roughness)
{
    const NodeId n = enginetest::addTestCube(s, albedo, metalness, roughness);
    enginetest::setNodeScale(s, n, scale);
    enginetest::setNodePosition(s, n, pos);
    return n;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-planar-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("planar", 256, 256, Colour(0, 0, 0));
    Scene *s = engine->createScene("planar");
    view->setScene(s);
    // A little ambient so the mirror is not pitch black without the reflection;
    // neutral, so it contributes nothing to the red-excess measure.
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.10f, 0.10f, 0.10f));

    // The mirror: top face at y = 0, 0.2 thick (its underside is what would
    // block the reflection if self-exclusion were broken).
    const NodeId floor = addPlate(s, Vec3(0.0f, -0.1f, 0.0f), Vec3(12.0f, 0.2f, 12.0f),
                                  Colour(1.0f, 1.0f, 1.0f), 1.0f, 0.0f);

    // The emitter: bright red, emissive, floating above the mirror.
    const NodeId emitter = enginetest::addTestCube(s, Colour(0.05f, 0.05f, 0.05f), 0.0f, 0.5f);
    enginetest::setNodeScale(s, emitter, Vec3(1.5f, 1.5f, 1.5f));
    enginetest::setNodePosition(s, emitter, Vec3(0.0f, 2.2f, 0.0f));

    // Emissive is set through the material, so the cube needs its own.
    {
        PbrParams p;
        p.albedo = Colour(0.05f, 0.05f, 0.05f);
        p.emissive = Colour(3.0f, 0.0f, 0.0f);
        p.roughness = 0.5f;
        const MaterialId mat = s->createPbrMaterial(p);
        const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
        s->attachMesh(emitter, mesh, mat);
        enginetest::setNodeScale(s, emitter, Vec3(1.5f, 1.5f, 1.5f));
        enginetest::setNodePosition(s, emitter, Vec3(0.0f, 2.2f, 0.0f));
    }

    const NodeId lightNode = enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
    (void)lightNode;

    // Low and in front: the cube is up-frame, its mirror image is down-frame.
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.4f, 7.0f), Vec3(0.0f, 0.6f, 0.0f));

    // ---- 1. off -> on ------------------------------------------------------
    const float redOff = measure(engine.get(), view, "reflections off");
    CHECK(redOff < 0.06f, "no red on the floor before any reflector exists");

    PlanarReflectionParams pr;
    pr.budget = 2;
    pr.resolution = 512;
    pr.mipmaps = true;
    pr.shadows = false;
    pr.maxDistance = 4.0f;
    CHECK(s->setPlanarReflections(pr), "setPlanarReflections(budget 2) applies");
    CHECK(s->setPlanarReflections(pr), "setPlanarReflections is idempotent");
    CHECK(s->activePlanarReflectors() == 0, "no actors active before any node is a reflector");

    CHECK(s->setNodePlanarReflector(floor, true), "the floor plate accepts the reflector flag");
    CHECK(s->nodePlanarReflector(floor), "planarReflector reads back true");

    const float redOn = measure(engine.get(), view, "reflections on");
    CHECK(s->activePlanarReflectors() == 1, "exactly one actor rendered");
    CHECK(redOn > redOff + 0.10f, "the emitter is REFLECTED in the floor (red appears)");
    // Same statement, said as the self-exclusion assertion it also is: the
    // reflection camera is under the plate looking up, so any red at all proves
    // the plate is not in its own reflection pass.
    CHECK(redOn > 0.10f, "the mirror does not self-reflect (its underside would have hidden the emitter)");

    // ---- 2. move the emitter away: the reflection must go -------------------
    enginetest::setNodePosition(s, emitter, Vec3(0.0f, 2.2f, -400.0f));
    const float redAway = measure(engine.get(), view, "emitter moved away");
    CHECK(redAway < redOn - 0.10f, "moving the emitter removes the reflection (it really is a reflection)");
    enginetest::setNodePosition(s, emitter, Vec3(0.0f, 2.2f, 0.0f));
    const float redBack = measure(engine.get(), view, "emitter back");
    CHECK(redBack > redAway + 0.10f, "bringing it back restores the reflection");

    // ---- 3. turning the flag off restores the original pixels ---------------
    CHECK(s->setNodePlanarReflector(floor, false), "the reflector flag clears");
    CHECK(!s->nodePlanarReflector(floor), "planarReflector reads back false");
    const float redCleared = measure(engine.get(), view, "reflector cleared");
    CHECK(s->activePlanarReflectors() == 0, "no actors active once the flag is cleared");
    CHECK(redCleared < redOff + 0.06f, "clearing the flag restores the original pixels");

    // ---- 4. budget clamp ----------------------------------------------------
    // Three reflectors, budget 2. All three are in frame; only two may render.
    const NodeId plateB = addPlate(s, Vec3(-4.0f, 0.9f, -2.0f), Vec3(3.0f, 0.15f, 3.0f),
                                   Colour(1, 1, 1), 1.0f, 0.1f);
    const NodeId plateC = addPlate(s, Vec3(4.0f, 0.9f, -2.0f), Vec3(3.0f, 0.15f, 3.0f),
                                   Colour(1, 1, 1), 1.0f, 0.1f);
    CHECK(s->setNodePlanarReflector(floor, true), "reflector A armed");
    CHECK(s->setNodePlanarReflector(plateB, true), "reflector B armed");
    CHECK(s->setNodePlanarReflector(plateC, true), "reflector C armed");
    if (!s->nodePlanarReflector(plateB) || !s->nodePlanarReflector(plateC))
        std::printf("   arm failure: %s\n", engine->lastError().c_str());
    render(engine.get(), 3);
    const int active = s->activePlanarReflectors();
    std::printf("   active actors with 3 reflectors at budget 2: %d\n", active);
    CHECK(active == 2, "the budget caps how many planes render");

    // Raising the budget raises the cap (and recompiles shaders — the reason
    // this is a mode row and not a slider the user drags).
    pr.budget = 3;
    CHECK(s->setPlanarReflections(pr), "budget raised to 3");
    render(engine.get(), 3);
    std::printf("   active actors at budget 3: %d\n", s->activePlanarReflectors());
    CHECK(s->activePlanarReflectors() == 3, "all three planes render at budget 3");
    // The reflector flags survived the arm being rebuilt from scratch.
    CHECK(s->nodePlanarReflector(floor) && s->nodePlanarReflector(plateB) &&
          s->nodePlanarReflector(plateC), "reflector flags survive a budget change");

    // ---- 5. non-plate geometry is refused -----------------------------------
    const NodeId ball = enginetest::addTestCube(s, Colour(0.5f, 0.5f, 0.5f), 0.0f, 0.4f);
    enginetest::setNodeScale(s, ball, Vec3(1.0f, 1.0f, 1.0f));
    CHECK(!s->setNodePlanarReflector(ball, true), "a cube is refused as a reflector");
    CHECK(!engine->lastError().empty(), "the refusal says why");
    std::printf("   refusal message: %s\n", engine->lastError().c_str());
    CHECK(!s->nodePlanarReflector(ball), "the refused node did not become a reflector");

    // ---- 5b. a TWO-SIDED reflector is refused -------------------------------
    // The regression pin for the one real failure the dead visibility bit was
    // pretending to cover (defect-verifier, 2026-09-06): self-exclusion is the
    // reflected camera's clip plane plus INVERTED WINDING, and a CULL_NONE
    // material has no back faces to cull, so such a mirror paints its whole RTT
    // with itself. Nothing in Studio wires double-sidedness to a reflector
    // today — a glTF `doubleSided` import is the plausible trigger — so this
    // case is what keeps the guard from being deleted as "unreachable".
    //
    // Parked far below the floor so it can never move a pixel measured above,
    // and the material is switched in place so the ONLY difference between the
    // refusal and the acceptance is two-sidedness.
    const NodeId twoSided = s->createNode();
    CHECK(twoSided != 0, "a plate for the two-sided case");
    {
        PbrParams p;
        p.albedo = Colour(1.0f, 1.0f, 1.0f);
        p.metalness = 1.0f;
        p.roughness = 0.05f;
        p.twoSided = true;
        const MaterialId mat = s->createPbrMaterial(p);
        const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
        CHECK(mat && mesh && s->attachMesh(twoSided, mesh, mat), "…with a two-sided material");
        enginetest::setNodeScale(s, twoSided, Vec3(3.0f, 0.15f, 3.0f));
        enginetest::setNodePosition(s, twoSided, Vec3(0.0f, -60.0f, 0.0f));

        CHECK(!s->setNodePlanarReflector(twoSided, true),
              "a two-sided plate is REFUSED as a reflector");
        CHECK(engine->lastError().find("two-sided") != std::string::npos,
              "…and the refusal names two-sidedness as the reason");
        std::printf("   refusal message: %s\n", engine->lastError().c_str());
        CHECK(!s->nodePlanarReflector(twoSided), "the refused node did not become a reflector");

        // Same node, same geometry, single-sided material: accepted. This is
        // what makes the case above a statement about the MATERIAL.
        p.twoSided = false;
        CHECK(s->setPbrMaterial(mat, p), "the material is turned single-sided");
        CHECK(s->setNodePlanarReflector(twoSided, true),
              "…and the very same plate is then accepted");
        CHECK(s->setNodePlanarReflector(twoSided, false), "cleared again, leaving the scene as found");
    }

    // ---- 6. reflections in a shadowed pass build and render ------------------
    // The half-resolution shadow node is a separate definition; instantiating it
    // per slot is the expensive path, so at least prove it does not throw.
    view->setShadows(true);
    pr.budget = 1;
    pr.shadows = true;
    CHECK(s->setPlanarReflections(pr), "reflections with their own shadow node apply");
    render(engine.get(), 3);
    CHECK(engine->lastError().empty() || engine->lastError().find("shadow") == std::string::npos,
          "no error from the reflection shadow pass");

    // ---- 7. destroy with a live reflector (ASan is the real assertion) -------
    // Order under test: the arm must unbind itself from the process-wide HlmsPbs
    // and die before the SceneManager, and it must release every tracked
    // Renderable before the Items go.
    engine->destroyView(view);
    engine->destroyScene(s);
    render(engine.get(), 1);
    CHECK(true, "engine survives destroying a scene with live reflectors");

    std::printf(failures ? "FAILURES: %d\n" : "all planar-reflection checks passed\n", failures);
    return failures ? 1 : 0;
}
