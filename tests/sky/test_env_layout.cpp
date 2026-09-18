// THE SKY'S REFLECTION CUBE IS HANDED OVER IN A SAMPLEABLE LAYOUT
// (lane ENGINE-SMALL-B, item ENVPROBE-LAYOUT-1, 2026-09-18) — headless,
// framework-free, links JahshakaEngine only (a reachable DISPLAY and a Vulkan
// driver or lavapipe, like tests/engine and tests/sky's transmittance row).
//
// THE DEFECT THIS SUITE IS THE GATE ON. The sky's own GPU capture is drawn by a
// compositor workspace of ours (`JahshakaSkyCaptureWorkspace`), convolved into
// the cubemap every PBR datablock samples by a second one
// (`JahshakaIblSpecularWorkspace`, a compute pass writing through a UAV), and
// then handed STRAIGHT to HlmsPbs — no later compositor pass ever names it. The
// pin's `CompositorPassIblSpecular::analyzeBarriers` leaves its output in
// `ResourceLayout::Uav`, i.e. `VK_IMAGE_LAYOUT_GENERAL`, and the Vulkan render
// system writes `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` into the descriptor
// whatever the image's real layout is — so every draw that sampled the sky's
// reflection sampled an image in the wrong layout:
// `VUID-vkCmdDraw-None-09600`, ten subresources of that one cube, on an
// ORDINARY editor run (measured 2026-09-18: a new project with one cube, 180
// frames and a shot = 10 reports, the layer's per-VUID limit). The driver read
// it correctly — no picture was ever wrong — which is why it stood: it is the
// class that works until a driver stops tolerating it. `OgreSky.cpp`'s
// `handOverForSampling` resolves the transition after every route that writes
// the cube.
//
// WHY A SUITE OF ITS OWN, AND WHY THE DEFAULT SCENE'S SELF-TEST CANNOT BE IT.
// `--engine-selftest` runs under the validation layer already
// (app.engine_selftest_validation) and it does NOT see this: its default scene
// is the matte ground and the sky, and patch 0028's probe gate means a material
// that cannot reflect never gets the env-probe permutation — no sampler, no
// error. The shape that bites needs a REFLECTIVE material in a scene with a
// sky, which is what this builds.
//
// WHAT IS ASSERTED
//   1. the mirror cube really does sample the sky's reflection — its pixels
//      carry the sky, and they MOVE when the sky changes. Without this the
//      layer half below could pass by accident on a build where the cube
//      stopped being bound at all;
//   2. the layout, by the Khronos validation layer: the ctest row carries
//      `VK_INSTANCE_LAYERS` and `FAIL_REGULAR_EXPRESSION "Validation Error"`,
//      the same shape shadow.cache_kinds uses. A sky CHANGE is exercised twice
//      because each change re-runs the capture and the convolution, and it is
//      the frames AFTER a convolution that sample the cube.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                               std::printf("\n"); ++failures; } \
                               else { std::printf("  ok: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const float kPi = 3.14159265358979323846f;

/// The analytic sky, whose capture and convolution are the subject. Its density
/// is the dial the two cases move: a denser atmosphere is a visibly different
/// environment, which is what makes case 1's "the reflection followed" real.
static SkyDesc atmosphereSky(float density, float elevationDeg)
{
    SkyDesc d;
    d.mode = SkyMode::Atmosphere;
    d.atmosphere.density = density;
    d.atmosphere.hasSun = true;
    const float e = elevationDeg * kPi / 180.0f;
    d.atmosphere.sunDir[0] = 0.0f;
    d.atmosphere.sunDir[1] = std::sin(e);
    d.atmosphere.sunDir[2] = -std::cos(e);
    return d;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-env-layout-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("env", 128, 128, Colour(0, 0, 0));
    Scene *s = engine->createScene("env");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);

    // A MIRROR CUBE, filling the centre of the frame: metal 1 at roughness 0.02
    // is the material that samples `texEnvProbeMap` at mip 0 — the permutation
    // patch 0028's probe gate lets through, and the one the matte default
    // ground never reaches.
    const NodeId cube = enginetest::addTestCube(s, Colour(1.0f, 1.0f, 1.0f), 1.0f, 0.02f);
    if (!cube) { std::printf("FAIL: the mirror cube\n"); return 1; }
    enginetest::setNodeScale(s, cube, Vec3(2.0f, 2.0f, 2.0f));
    enginetest::addDirectionalLight(s, Vec3(-0.4f, -1.0f, -0.5f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.6f, 3.6f), Vec3(0.0f, 0.0f, 0.0f));

    const auto frames = [&](int n) { for (int i = 0; i < n; ++i) engine->renderOneFrame(); };
    const auto centre = [&]() {
        Image img;
        if (!view->readPixels(img)) { std::printf("FAIL: readPixels\n"); ++failures; return Colour(0, 0, 0); }
        return img.at(64, 64);
    };

    // ---- 1. A CLEAR SKY, CAPTURED AND CONVOLVED ----------------------------
    if (!s->setSky(atmosphereSky(0.25f, 45.0f))) {
        std::printf("FAIL: the analytic sky applies: %s\n", engine->lastError().c_str());
        return 1;
    }
    // Four frames: the capture runs inside the frame that follows the sky
    // change and the convolution at the top of the next one, so the first frame
    // that SAMPLES the finished cube is the third.
    frames(4);
    const Colour clear = centre();
    std::printf("   clear sky   r=%.4f g=%.4f b=%.4f\n", clear.r, clear.g, clear.b);
    CHECK_MSG(clear.r + clear.g + clear.b > 0.02f,
              "the mirror cube is lit by the sky's reflection (sum %.4f)", clear.r + clear.g + clear.b);

    // ---- 2. THE SKY CHANGES, AND THE REFLECTION FOLLOWS --------------------
    // The second capture + convolution is the route a sky drag takes, and the
    // one that would leave a SECOND cube in the wrong layout. The pixel moving
    // is what proves the env probe is really the source: a build that stopped
    // binding the cube would read the same picture twice.
    if (!s->setSky(atmosphereSky(2.0f, 8.0f))) {
        std::printf("FAIL: the second sky applies: %s\n", engine->lastError().c_str());
        return 1;
    }
    frames(4);
    const Colour hazy = centre();
    std::printf("   hazy sky    r=%.4f g=%.4f b=%.4f\n", hazy.r, hazy.g, hazy.b);
    const float moved = std::fabs(hazy.r - clear.r) + std::fabs(hazy.g - clear.g) +
                        std::fabs(hazy.b - clear.b);
    CHECK_MSG(moved > 0.01f,
              "the reflection followed the sky change (|delta| %.4f over three channels)", moved);

    // ---- 3. AND IT KEEPS BEING SAMPLED ------------------------------------
    // Twenty more frames with nothing changing: the layout must hold for the
    // life of the cube, not merely for the frame after the barrier. (A layout
    // regression shows up here as the layer's report, not as a pixel.)
    frames(20);
    const Colour held = centre();
    CHECK_MSG(std::fabs(held.r - hazy.r) + std::fabs(held.g - hazy.g) +
                  std::fabs(held.b - hazy.b) < 1e-4f,
              "a still scene holds the picture (r=%.4f g=%.4f b=%.4f)", held.r, held.g, held.b);

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall sky env-layout cases passed\n", failures);
    return failures ? 1 : 0;
}
