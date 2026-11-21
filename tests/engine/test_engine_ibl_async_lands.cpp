// sky.ibl_async_lands — AN ASYNCHRONOUS SCENE's SKY CHANGE GETS ITS REFLECTION (COLD-BOOT-1, a defect in its own right).
// A sky change convolves the captured sky into the specular cube (OgreScene::convolvePendingIbl). In a scene that compiles
// in the background (OgreScene::compilesInBackground: an asynchronous view, a frame that does not owe the whole picture,
// the compiler running) a convolution whose permutation is not built yet is DEFERRED and retried the next frame. Before
// the fix every try made a NEW one-shot workspace and removed it in the same call: its ibl_specular pass clones a NEW job
// per mip (fork CompositorPassIblSpecular.cpp:230), the deferred dispatch submits a request keyed on that clone, and the
// removal destroys the clone (CompositorPassIblSpecular.cpp:171), whose destroyComputeJob CANCELS the request
// (HlmsCompute.cpp:417) - so no try ever completed one and the environment stayed pending for ever (the startup gate's
// livelock, spikes/cold-boot-1/S4arm: 48,363 clones, each deferred once). Now a deferred try keeps its workspace and the
// retry re-runs it: the same clones, so the requests land.
//
// THE SCRIPT: an offscreen view in asynchronous mode, NO persistent shader cache (the convolution's permutations are cold
// in this process), every frame on the Streaming pace (the one rule's "a frame that does not owe the whole picture"); one
// scene, an atmosphere sky. Frames until the environment has landed, or the law has failed.
// THE LAWS, IN FRAMES (the compile is wall-clock: the row waits on the published state, CODING_RULES § 5):
//  (1) the deferred path ran: iblConvolutionDeferrals > 0 (else the row proves nothing - the permutation was built);
//  (2) THE LANDING: once the background compiler has nothing outstanding, the environment lands within kLandFrames.
//      Derivation: a compile that finished is published at the next frame's top (Root::_updateAllRenderTargets ->
//      HlmsManager::_publishAsyncCompiles) and that frame's retry dispatches every mip (applyPendingIbl) - 1 frame; the
//      SH read the set also waits for is due lag + 1 frames after its capture (OgreScene::skyReadDue; lag = the dynamic
//      buffer multiplier, 3 on Vulkan) and the capture came before the convolution's first try; the swap is in the same
//      call (landEnvironmentIfComplete). So lag + 2 = 5 frames bound it. RED before the fix: the compiler is empty at
//      every frame's close (each request is cancelled inside the try that made it) and the environment never lands.
//  (3) no try after the landing: the deferral count stands still once the environment has landed.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include <algorithm>
#include <cstdio>
#include <string>
using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

/// The dynamic buffer multiplier of Ogre's Vulkan VaoManager (the frames in flight): the SH read's lag (skyReadDue).
static constexpr int kFramesInFlight = 3;
/// THE LANDING's bound (law 2's derivation, above).
static constexpr int kLandFrames = kFramesInFlight + 2;
/// The hang guard of the wait for the compiler (never the law's bar: law 2 is counted from the compiler's empty state).
static constexpr int kFrameCap = 3000;

int main()
{
    std::printf("== sky.ibl_async_lands: an asynchronous scene's sky change gets its reflection\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-ibl-async-lands-ogre.log",
                                                    enginetest::EngineOverrides().shaderCacheOff());
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    View *view = e->createOffscreenView("ibl", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *scene = e->createScene("ibl");
    if (!view || !scene) { std::printf("FAIL: view/scene (%s)\n", e->lastError().c_str()); return 1; }
    view->setAsyncShaders(true);
    view->setScene(scene);
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 1.6f, 3.0f), Vec3(0.0f, 1.6f, 0.0f)));
    SkyDesc sky;
    sky.mode = SkyMode::Atmosphere;
    CHECK(scene->setSky(sky), "the atmosphere sky set (%s)", e->lastError().c_str());

    const auto frame = [&]() {
        e->setFramePace(FramePace::Streaming);   // consumed by the next frame (the engine resets it to Complete after it)
        enginetest::frames(e, 1);
    };
    int f = 0, emptyFrames = 0;
    bool landed = false;
    unsigned long long maxOutstanding = 0;
    for (; f < kFrameCap; ++f) {
        frame();
        const AsyncShaderStats as = e->published().shaders;
        const unsigned outstanding = as.pending - std::min(as.pending, as.rayBuildsInFlight);
        if (outstanding > maxOutstanding) maxOutstanding = outstanding;
        const PublishedFrame &pub = scene->published();
        if (!pub.environmentPending) { landed = true; break; }
        emptyFrames = outstanding == 0 ? emptyFrames + 1 : 0;
        if (emptyFrames > kLandFrames) break;   // law 2 has failed: nothing on the compiler, the environment still owed
    }
    const unsigned long long deferrals = scene->published().iblConvolutionDeferrals;
    std::printf("    %d frames: landed %d, the convolution deferred %llu try(s), the compiler's queue at most %llu, "
                "frames with it empty and the environment owed %d\n",
                f + 1, landed ? 1 : 0, deferrals, maxOutstanding, emptyFrames);
    CHECK(deferrals > 0, "(1) the convolution deferred at least once: %llu try(s) (the cold permutation was on the "
                         "background compiler)", deferrals);
    CHECK(landed, "(2) the environment LANDED: within %d frames of the compiler's queue emptying (%d frames empty, "
                  "%d frames in all)", kLandFrames, emptyFrames, f + 1);
    if (landed) {
        for (int i = 0; i < kLandFrames; ++i) frame();
        const unsigned long long after = scene->published().iblConvolutionDeferrals;
        CHECK(after == deferrals && !scene->published().environmentPending,
              "(3) no try after the landing: the deferrals %llu -> %llu over %d frames, the environment landed",
              deferrals, after, kLandFrames);
    }
    std::printf(failures ? "sky.ibl_async_lands: FAIL (%d)\n" : "sky.ibl_async_lands: PASS\n", failures);
    return failures ? 1 : 0;
}
