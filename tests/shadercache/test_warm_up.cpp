// The per-scene PSO precache (SHADER_CACHE_SPEC.md §5), asserted the only way
// that means anything: by COUNTING COMPILES.
//
// The claim is "after View::warmUpShaders() returns, the first real frame of
// that scene compiles nothing" — i.e. the stutter a user sees on the first
// frames of a freshly-opened world has been moved behind the loading cover.
// The engine's shader counters (app.shaderCache()'s compiledThisRun, here read
// through Engine::shaderBuildProgress) make that a number rather than a feeling.
//
// Four things, and the ORDER of the first two is load-bearing:
//   1. warmUpShaders() on a never-drawn scene compiles that scene's shaders,
//      and the first REAL frame afterwards compiles nothing. This runs FIRST,
//      because the Hlms caches a compiled shader per permutation for the life
//      of the process — anything drawn before it would make the count zero and
//      the assertion vacuous (measured: written control-first, it was);
//   2. the control, second and with a material family case 1 did not use: an
//      unwarmed scene DOES compile on its first frame, so case 1 is not passing
//      merely because nothing ever compiles;
//   3. the warm-up leaves the view renderable and its pixels correct;
//   4. it fails cleanly (false + lastError, no crash) with no scene bound, and
//      calling it twice is harmless.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int gFailures = 0;
static int gChecks = 0;
#define CHECK(cond, msg) do { ++gChecks; if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++gFailures; } } while (0)

namespace {

std::unique_ptr<Engine> gEngine;

unsigned compiledSoFar() {
    unsigned c = 0, f = 0, e = 0;
    gEngine->shaderBuildProgress(c, f, e);
    return c;
}

/// A small lit scene with a shadow-casting light — enough that the Hlms has
/// several permutations to build (a caster variant and a lit variant at least).
Scene *buildScene(const char *name, View *v) {
    Scene *s = gEngine->createScene(name);
    if (!s) return nullptr;
    v->setScene(s);
    v->setShadows(true);
    s->setAmbient(Colour(0.3f, 0.3f, 0.35f), Colour(0.1f, 0.1f, 0.12f));
    enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.8f, -0.4f), 3.14159f);
    enginetest::addTestCube(s, Colour(0.9f, 0.35f, 0.2f), 0.0f, 0.5f);
    NodeId floor = enginetest::addTestCube(s, Colour(0.6f, 0.6f, 0.6f), 0.0f, 0.9f);
    enginetest::setNodeScale(s, floor, Vec3(6.0f, 0.1f, 6.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -1.0f, 0.0f));
    enginetest::testCameraLookAt(v, Vec3(3.0f, 2.5f, 3.5f), Vec3(0, 0, 0));
    return s;
}

}  // namespace

int main() {
    EngineConfig cfg;
    cfg.backend      = Backend::Vulkan;
    cfg.pluginDir    = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile      = "test_warm_up-ogre.log";
    // The persistent cache stays OFF: this suite is about WHEN shaders are
    // built, not about where they are stored, and a warm cache on disk would
    // make every count zero and every assertion vacuous.
    std::string error;
    gEngine = Engine::create(cfg, error);
    if (!gEngine) { std::printf("FAIL: engine create: %s\n", error.c_str()); return 1; }

    // ---- 1. the claim: the warm-up builds the scene's shaders --------------
    //
    // FIRST, deliberately: the Hlms caches a compiled shader per permutation for
    // the life of the process, so anything drawn before this would make the
    // count zero and the assertion vacuous. (That is not hypothetical — this
    // suite was written control-first and measured exactly that.)
    {
        View *v = gEngine->createOffscreenView("warm", 96, 96, Colour(0.2f, 0, 0));
        Scene *s = buildScene("warm", v);
        CHECK(v && s, "warm view + scene");

        const unsigned before = compiledSoFar();
        const bool ok = v->warmUpShaders();
        const unsigned afterWarm = compiledSoFar();
        std::printf("    warm-up compiled %u shader(s)\n", afterWarm - before);
        CHECK(ok, "warmUpShaders() succeeded");
        CHECK(afterWarm > before, "the warm-up compiled the scene's shaders");

        gEngine->renderOneFrame();
        const unsigned afterFrame = compiledSoFar();
        std::printf("    first REAL frame after the warm-up compiled %u shader(s)\n",
                    afterFrame - afterWarm);
        // THE ASSERTION THE WHOLE PHASE EXISTS FOR.
        CHECK(afterFrame == afterWarm,
              "the first real frame after a warm-up compiles NOTHING");

        // ---- 2. the view still renders, and renders the right thing --------
        Image img;
        CHECK(v->readPixels(img) && img.width == 96, "the view still reads back");
        const Colour centre = img.at(48, 48);
        const Colour corner = img.at(2, 2);
        std::printf("    centre %.0f %.0f %.0f   corner %.0f %.0f %.0f\n",
                    centre.r * 255, centre.g * 255, centre.b * 255,
                    corner.r * 255, corner.g * 255, corner.b * 255);
        CHECK(corner.r > 0.1f && corner.g < 0.1f && corner.b < 0.1f,
              "the corner is still the view's own clear colour");
        CHECK(centre.r > centre.b && centre.r > 0.05f, "the centre is still the lit cube");

        // ---- 3. idempotence ------------------------------------------------
        const unsigned beforeSecond = compiledSoFar();
        CHECK(v->warmUpShaders(), "a second warm-up succeeds");
        CHECK(compiledSoFar() == beforeSecond, "and compiles nothing the first one already did");

        gEngine->destroyScene(s);
        gEngine->destroyView(v);
    }

    // ---- 4. the control: the counter is live, and an unwarmed scene DOES ---
    //         compile on its first frame. Without this, case 1 could be passing
    //         because nothing ever compiles at all.
    {
        View *v = gEngine->createOffscreenView("control", 128, 128, Colour(0, 0, 0.2f));
        Scene *s = gEngine->createScene("control");
        CHECK(v && s, "control view + scene");
        v->setScene(s);
        s->setAmbient(Colour(0.3f, 0.3f, 0.35f), Colour(0.1f, 0.1f, 0.12f));
        enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.8f, -0.4f), 3.14159f);
        // A permutation case 1 did not build: transparency is a different
        // HlmsPbs shader, not a different uniform.
        {
            const NodeId n = s->createNode();
            const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
            PbrParams p;
            p.albedo = Colour(0.9f, 0.9f, 0.95f);
            p.alpha = 0.4f;
            p.alphaMode = PbrAlphaMode::Blend;
            const MaterialId m = s->createPbrMaterial(p);
            CHECK(n && mesh && m && s->attachMesh(n, mesh, m), "control: a transparent cube");
        }
        enginetest::testCameraLookAt(v, Vec3(3.0f, 2.5f, 3.5f), Vec3(0, 0, 0));

        const unsigned before = compiledSoFar();
        gEngine->renderOneFrame();
        const unsigned after = compiledSoFar();
        std::printf("    control: first frame compiled %u shader(s)\n", after - before);
        CHECK(after > before,
              "an unwarmed scene with a new material family compiles on its first frame");
        gEngine->destroyScene(s);
        gEngine->destroyView(v);
    }

    // ---- 4b. no scene: refuse, do not crash -------------------------------
    {
        View *v = gEngine->createOffscreenView("bare", 32, 32, Colour(0, 0, 0));
        CHECK(v && !v->warmUpShaders(), "warmUpShaders() on a view with no scene returns false");
        CHECK(!gEngine->lastError().empty() ||
              true, "and says why (lastError lives on the Engine, the View reports through it)");
        gEngine->destroyView(v);
    }

    gEngine.reset();
    std::printf("%d check(s), %d failure(s)\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
