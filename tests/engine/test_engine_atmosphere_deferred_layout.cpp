// sky.atmosphere_deferred_layout — A VIEW DRAWN WHILE THE ATMOSPHERE's TABLES ARE DEFERRED IS VALID VULKAN AND SAMPLES A
// DEFINED PLACEHOLDER (SHADER-SYSTEM-1 7.0; the train's red, 2026-11-23: app.engine_selftest_validation.default/.fixture,
// 10 x VUID-vkCmdDraw-imageLayout-00344 each, in the startup gate's asynchronous warm view). An asynchronous scene's
// table jobs are deferred while their permutations compile (R2 (c): a frame that does not owe its picture), and
// JahAtmosphere::runJob issues a job's barriers BEFORE its dispatch (the dispatch decides the deferral), so a deferred
// SkyView or Aerial job had already moved its table to the UAV layout; update() then skipped the hand-over, and the sky
// quad (skyViewLut) and every PBS colour pass (jahAtmoAerial) sampled a GENERAL image as a texture. THE FIX
// (OgreAtmosphere.cpp): the chain stops at its first deferred job; the tables are handed to the samplers after every
// call, built or deferred; at birth every table holds a defined placeholder (a black sky, no aerial perspective).
// THE SCRIPT: no shader cache (the table jobs' permutations cold in the process), an offscreen Live view compiling in
// the background, every frame on the Streaming pace, the service held kServiceSlowMs a job (the test door
// shaders.serviceSlow) so the tables stay owed for some frames; an atmosphere sky and a sun high behind the camera; the
// view looks at the sky above the horizon. Under the Khronos validation layer (the registration's environment; the
// layer's presence proved by enginetest::validationProof).
// THE LAWS:
//  (0) the layer is LIVE on the device (else a "0 errors" proves nothing);
//  (1) CONTROL: the tables' rebuild was deferred at least once (AtmosphereStatus::deferredUpdates > 0) and at least one
//      frame was read before the first SkyView build landed - else the row never met the case;
//  (2) THE PLACEHOLDER: every frame read before the first SkyView build landed shows the sky pixel at exact codes
//      (0,0,0) - the black placeholder, never undefined memory;
//  (3) THE TABLES LAND: once the background compiler holds nothing, within kLandFrames the SkyView table was built
//      (skyViewBuilds > 0), no table is owed (completeness term 64 clear), and the sky pixel is lit (above 0);
//  (4) NO VALIDATION ERROR in the whole run: the registration fails on "Validation Error" in the output (the app
//      validation rows' rule, tests/app/engine_selftest.sh; no engine fixture counted the layer's errors in process).
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Pictures.h"
#include "jahshaka/engine/Published.h"
#include "jahshaka/engine/Shaders.h"
#include "jahshaka/engine/Sky.h"
#include "../support/enginetesthelpers.h"
#include "../support/validationproof.h"
#include <algorithm>
#include <cstdio>
#include <string>
using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

/// The door's hold on each background compile (ms): an injected slowness whose only job is to keep the tables owed for
/// more than one frame; law (1)'s control proves the case was met in every run.
static constexpr double kServiceSlowMs = 20.0;
/// Term 64 of PublishedFrame::Completeness: a compute dispatch deferred.
static constexpr unsigned kTermDeferred = 64u;
/// The landing's bound once the compiler is empty: the retry runs the chain at the next frame's update and the published
/// status is a frame late - 1 + 1, and the frames in flight before the readback sees it (Ogre's Vulkan VaoManager: 3).
static constexpr int kLandFrames = 5;
/// The hang guard of the wait for the compiler (never the law's bar).
static constexpr int kFrameCap = 3000;
/// The view and the sky pixel read: the image's centre column, an eighth of the way down (above the horizon).
static constexpr unsigned kSize = 64u, kSkyX = kSize / 2u, kSkyY = kSize / 8u;

static bool skyPixel(View *view, unsigned char out[3])
{
    Image img;
    if (!view->readPixels(img) || img.width != kSize || img.height != kSize) return false;
    const size_t i = (size_t(kSkyY) * img.width + kSkyX) * 4u;
    out[0] = img.rgba[i]; out[1] = img.rgba[i + 1]; out[2] = img.rgba[i + 2];
    return true;
}

int main()
{
    std::printf("== sky.atmosphere_deferred_layout: a view drawn while the atmosphere's tables are deferred is valid "
                "and samples a defined placeholder\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-atmosphere-deferred-layout-ogre.log",
                                                    enginetest::EngineOverrides().shaderCacheOff());
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    View *view = e->createOffscreenView("atmo", kSize, kSize, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *scene = e->createScene("atmo");
    if (!view || !scene) { std::printf("FAIL: view/scene (%s)\n", e->lastError().c_str()); return 1; }
    CHECK(enginetest::validationProof(*e), "(0) the validation layer is live on the device");
    view->setAsyncShaders(true);
    view->setScene(scene);
    // the camera looks along -z at the sky above the horizon; the sun high and behind it (out of the picture)
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 1.6f, 3.0f), Vec3(0.0f, 2.6f, 0.0f)));
    bool ok = enginetest::addDirectionalLight(scene, Vec3(0.0f, -1.0f, -0.6f), 2.0f) != 0;
    SkyDesc sky;
    sky.mode = SkyMode::Atmosphere;
    ok = ok && scene->setSky(sky);
    CHECK(ok, "the scene built: a sun and the atmosphere sky (%s)", e->lastError().c_str());
    if (!ok) return 1;
    e->setArm("shaders.serviceSlow", kServiceSlowMs);

    int f = 0, emptyFrames = 0, placeholderFrames = 0, placeholderWrong = 0;
    unsigned char wrong[3] = { 0, 0, 0 };
    bool landed = false, readOk = true;
    for (; f < kFrameCap; ++f) {
        e->setFramePace(FramePace::Streaming);   // consumed by the next frame (the engine resets it to Complete after it)
        enginetest::frames(e, 1);
        const PublishedFrame &pub = scene->published();
        const AsyncShaderStats as = e->published().shaders;
        const unsigned outstanding = as.pending - std::min(as.pending, as.rayBuildsInFlight);
        if (pub.atmosphereStatus.skyViewBuilds == 0u) {
            unsigned char px[3];
            if (!skyPixel(view, px)) { readOk = false; break; }
            ++placeholderFrames;
            if (px[0] || px[1] || px[2]) {
                if (!placeholderWrong) { wrong[0] = px[0]; wrong[1] = px[1]; wrong[2] = px[2]; }
                ++placeholderWrong;
            }
        } else if (!(pub.completeness.terms & kTermDeferred) && pub.atmosphereStatus.on) {
            landed = true;
            break;
        }
        emptyFrames = outstanding == 0u ? emptyFrames + 1 : 0;
        if (emptyFrames > kLandFrames) break;   // law 3 has failed: nothing on the compiler, the tables still owed
    }
    e->setArm("shaders.serviceSlow", 0.0);
    const AtmosphereStatus a = scene->published().atmosphereStatus;
    std::printf("    %d frames: %u table rebuild(s) deferred, %d frame(s) read before the first SkyView build, "
                "SkyView built %u time(s), landed %d\n", f + 1, a.deferredUpdates, placeholderFrames, a.skyViewBuilds,
                landed ? 1 : 0);
    CHECK(readOk, "every placeholder frame read back");
    CHECK(a.deferredUpdates > 0u && placeholderFrames > 0,
          "(1) CONTROL: the tables' rebuild deferred %u time(s) and %d frame(s) were read before the first SkyView "
          "build (above 0 each: the case was met)", a.deferredUpdates, placeholderFrames);
    CHECK(placeholderWrong == 0, "(2) the placeholder: %d of %d frame(s) before the first build showed the sky pixel "
                                 "other than (0,0,0) (the first: %u,%u,%u)",
          placeholderWrong, placeholderFrames, wrong[0], wrong[1], wrong[2]);
    unsigned char lit[3] = { 0, 0, 0 };
    if (landed) {
        for (int i = 0; i < kLandFrames; ++i) enginetest::frames(e, 1);   // the default pace: the landed picture
        skyPixel(view, lit);
    }
    CHECK(landed && a.skyViewBuilds > 0u && (lit[0] || lit[1] || lit[2]),
          "(3) the tables landed within %d frames of the compiler emptying: SkyView built %u time(s), term 64 clear, "
          "the sky pixel lit (%u,%u,%u)", kLandFrames, a.skyViewBuilds, lit[0], lit[1], lit[2]);
    enginetest::poseRegistry().erase(scene);
    if (failures) std::printf("sky.atmosphere_deferred_layout: FAIL (%d)\n", failures);
    else std::printf("sky.atmosphere_deferred_layout: PASS\n");
    return failures ? 1 : 0;
}
