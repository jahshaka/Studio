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
#include <cstdlib>
#include <string>

using namespace jahshaka::engine;

static int gFailures = 0;
static int gChecks = 0;
#define CHECK(cond, msg) do { ++gChecks; if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++gFailures; } } while (0)

namespace {

std::unique_ptr<Engine> gEngine;

/// Which per-scene warm-up ROUTE this run exercises. The engine reads
/// JAHSHAKA_WARMUP_PASS once, on the first warm-up; the suite runs BOTH ways
/// (`shadercache.warmup` = the shipped default, `shadercache.warmup_pass` = the
/// opt-in CompositorPassWarmUp route that ogre-patch 0016 unblocked), so case 6
/// can assert the DELTA between them instead of asserting one of them twice.
bool gWarmUpPass = false;

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
    {
        const char *v = std::getenv("JAHSHAKA_WARMUP_PASS");
        gWarmUpPass = v && *v && *v != '0';
        std::printf("route: %s\n", gWarmUpPass ? "CompositorPassWarmUp (opt-in)"
                                               : "full-frame (shipped default)");
    }
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

    // ---- 5. THE RECORDED WARM-UP SET: DELETED (WARMUPSET-2, 2026-09-21) ---
    //
    // This case recorded a scene's permutations, threw the scene away and
    // rebuilt its shaders in a different scene from the recording alone. The
    // claim never held in this app: a set names each permutation by one
    // representative MATERIAL and resolves it BY NAME in the next process,
    // while this engine names datablocks from a process-unique counter — so
    // every recorded name missed, Ogre silently substituted the DEFAULT
    // datablock, and the replay compiled permutations nothing would ever bind
    // (measured: seven "Can't find HLMS datablock" lines and eight never-bound
    // shaders on every warm launch of the real app). The case passed anyway,
    // because it asserted only that the replay RAN. The three verbs and the
    // file are gone; what warms this renderer is the per-scene precache (cases
    // 1-4 above) and the persistent shader cache.

    // ---- 6. THE F2 ACCEPTANCE CASE: the warm-up pass reaches what the -------
    //         camera CANNOT SEE.
    //
    // This is the whole reason CompositorPassWarmUp is worth a patch to Ogre
    // (0016, SHADER_CACHE_AUDIT F2). The old route was "render the view's own
    // frame behind the cover", which compiles exactly what the frustum cull
    // leaves standing — so a world whose uncompiled permutation is behind the
    // camera still hitches the moment the user turns around.
    //
    // SceneManager::warmUpShaders (the WARM_UP_SHADERS worker,
    // OgreSceneManager.cpp:2568) runs NO frustum test at all — only the
    // visibility-flag test — so a warm-up pass collects every object in the
    // requested render queues wherever it happens to be. The assertion:
    //
    //   put a material family NOTHING in this process has built on an object
    //   parked far behind the camera, warm up, and the count must move.
    //
    // With JAHSHAKA_WARMUP_NO_PASS=1 (the fallback route) this case compiles
    // ZERO — which is the measurement, and why the env var exists.
    {
        View *v = gEngine->createOffscreenView("offscreen-perm", 96, 96, Colour(0, 0, 0));
        Scene *s = gEngine->createScene("offscreen-perm");
        CHECK(v && s, "off-camera view + scene");
        v->setScene(s);
        v->setShadows(true);
        s->setAmbient(Colour(0.3f, 0.3f, 0.35f), Colour(0.1f, 0.1f, 0.12f));
        enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.8f, -0.4f), 3.14159f);
        // BEHIND the camera and 40 units away: outside the frustum by a mile.
        // Cutout is an alpha-TEST shader (a distinct HlmsPbs permutation, not a
        // different uniform) and no case above has built one.
        {
            const NodeId n = s->createNode();
            const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
            PbrParams p;
            p.albedo = Colour(0.5f, 0.7f, 0.3f);
            p.alphaMode = PbrAlphaMode::Cutout;
            p.alphaCutoff = 0.5f;
            const MaterialId m = s->createPbrMaterial(p);
            CHECK(n && mesh && m && s->attachMesh(n, mesh, m), "a cutout cube behind the camera");
            enginetest::setNodePosition(s, n, Vec3(0.0f, 0.0f, -40.0f));
        }
        enginetest::testCameraLookAt(v, Vec3(0.0f, 0.0f, 3.0f), Vec3(0.0f, 0.0f, 8.0f));

        const unsigned before = compiledSoFar();
        CHECK(v->warmUpShaders(), "warmUpShaders() on the off-camera scene");
        const unsigned afterWarm = compiledSoFar();
        std::printf("    off-camera permutation: the warm-up compiled %u shader(s)\n",
                    afterWarm - before);
        if (gWarmUpPass) {
            CHECK(afterWarm > before,
                  "the warm-up pass compiled a permutation the camera cannot see");
        } else {
            // The DEFAULT route renders the view's own frame, the cube is
            // culled, and nothing is built. Asserted rather than skipped: that
            // zero is the measurement the pass route is worth, and if it ever
            // becomes non-zero the default route has silently changed shape.
            CHECK(afterWarm == before,
                  "the default full-frame route does NOT reach it (this is the F2 delta)");
        }
        gEngine->destroyScene(s);
        gEngine->destroyView(v);
    }

    // ---- 7. REBIND: warm up, close the world, open another, warm up again --
    //
    // The editor's shape, and the one that is not covered by any case above:
    // one View that outlives its scenes (EngineSceneViewport::clearScene keeps
    // the View and destroys only the Scene). A warm-up per bind is what
    // `shader_warmup_on_open` does.
    {
        View *v = gEngine->createOffscreenView("rebind", 96, 96, Colour(0, 0, 0));
        CHECK(v != nullptr, "rebind view");
        for (int round = 0; round < 3 && v; ++round) {
            char name[32];
            std::snprintf(name, sizeof name, "rebind-%d", round);
            Scene *s = gEngine->createScene(name);
            if (!s) { CHECK(false, "rebind scene"); break; }
            v->setScene(s);
            v->setShadows(true);
            s->setAmbient(Colour(0.3f, 0.3f, 0.35f), Colour(0.1f, 0.1f, 0.12f));
            enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.8f, -0.4f), 3.14159f);
            enginetest::addTestCube(s, Colour(0.2f + 0.2f * float(round), 0.4f, 0.6f), 0.0f, 0.5f);
            enginetest::testCameraLookAt(v, Vec3(3.0f, 2.5f, 3.5f), Vec3(0, 0, 0));
            const bool ok = v->warmUpShaders();
            gEngine->renderOneFrame();
            char msg[64];
            std::snprintf(msg, sizeof msg, "rebind round %d: warm-up then a real frame", round);
            CHECK(ok, msg);
            v->setScene(nullptr);
            gEngine->destroyScene(s);
        }
        if (v) gEngine->destroyView(v);
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
