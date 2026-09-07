// SPIKE — CAMERA_LENS_SPEC §7 R1. NOT SHIPPED CODE, NOT A GATE.
//
// THE QUESTION, verbatim from the spec: does a `setNamedConstant` written from
// a per-View CompositorWorkspaceListener's `workspacePreUpdate` take effect for
// THAT workspace's passes in the SAME frame? Every per-camera tuning value in
// P3/P4/P5 (exposure, bloom threshold, AO parameters, film grade) rides that
// assumption, and it is the spec's one load-bearing unverified claim: the post
// chain's tuning lives in process-global MaterialManager singletons
// (POST_CHAIN_SPEC §7.4), pushed once a frame from "the first enabled view
// whose chain has effects" (OgreEngine::renderOneFrame), so two views can only
// differ if a later, per-workspace write survives to that workspace's passes.
//
// HOW IT MEASURES — and why it is shaped like this. The obvious experiment
// ("give two views different exposures and compare them") is WRONG, and the
// first version of this spike was: HDR auto-exposure is a TEMPORAL filter whose
// adaptation texture lives in each workspace's own node instance, so two views
// differ simply because their histories differ, and a handful of offscreen
// frames (~1 ms each of wall clock) barely moves either. Every comparison below
// is therefore WITHIN ONE VIEW, ACROSS TIME: settle both views on the same
// settings, change ONE view's, and ask whether that view moved and the other
// did not.
//
//   A. CONTROL — the process-global path still works: change the OWNING view's
//      bloom threshold and its frame must change.
//   B. THE DEFECT — change the NON-owning view's threshold with no listener.
//      It must NOT move: the owning view's globals are what both render with.
//   C. THE MECHANISM — arm the per-view listeners and repeat B. If the
//      non-owning view now moves (and the owner does not), a per-workspace
//      setNamedConstant lands for that workspace in the same frame: R1 PASSES
//      and P3 can be built on it. If it does not, P3's fallback is per-View
//      clones of the HDR/SSAO materials.
//   D. EXPOSURE, the actual P3 knob, over real wall-clock time, with the
//      adaptation window pinned (min == max is manual exposure — the verified
//      clamp) and seconds of settling on both sides.
//
// The bloom threshold is the primary probe precisely because it is INSTANT:
// one setNamedConstant into HDR/BrightPass_Start, no temporal filter, visible
// in the next frame. It is the same code path setExposure uses.
//
// TWO MODES. Run with no arguments, the views are OFFSCREEN (readPixels only
// works offscreen) with PostFxDesc::allowOffscreen — the determinism law's
// deliberate opt-in — and the spike asserts its own numbers. Run with
// --onscreen and it creates two MAPPED X windows with real swapchains, which is
// the shape §7 R1 actually names; on-screen views cannot readPixels, so that
// mode captures each window with xwd at each measurement point and leaves the
// files for the caller to measure. Both modes drive the identical listener.
//
// The default views are OFFSCREEN (readPixels only works offscreen) with
// PostFxDesc::allowOffscreen — the determinism law's deliberate opt-in. That is
// not a weaker probe of the question: an RTT workspace and a swapchain
// workspace are updated by the same CompositorManager2 loop, in the same frame,
// and the listener fires the same way for both. What it cannot see is anything
// specific to on-screen ORDERING.

#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <X11/Xlib.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace jahshaka::engine;

static int failures = 0;

#define CHECK_MSG(cond, ...)                                     \
    do {                                                         \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
    } while (0)

namespace {

/// Mean luminance of a frame, 0..255. The whole spike is one number per image.
double brightness(const Image &img)
{
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        sum += 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2];
        ++n;
    }
    return n ? sum / double(n) : 0.0;
}

PostFxDesc hdrDesc(float bloomThreshold, float exposure)
{
    PostFxDesc fx;
    fx.allowOffscreen = true;      // the only way an offscreen view gets a chain
    fx.hdr = true;
    fx.bloom = true;
    fx.bloomThreshold = bloomThreshold;
    fx.exposure = exposure;
    // Pinned window = manual exposure (the verified clamp), so phase D's
    // numbers are a target rather than a moving average.
    fx.exposureMin = exposure;
    fx.exposureMax = exposure;
    return fx;
}

}   // namespace

// ---------------------------------------------------------------------------
// THE ON-SCREEN MODE (--onscreen). Two mapped X windows, two swapchains, one
// listener each; xwd captures instead of readPixels. Evidence, not assertions:
// the caller measures the PNGs.
namespace {

struct HostWindow {
    Display *display = nullptr;
    ::Window window = 0;
    bool open(Display *d, int x, int y, unsigned w, unsigned h) {
        display = d;
        const int screen = DefaultScreen(display);
        window = XCreateSimpleWindow(display, RootWindow(display, screen), x, y, w, h, 0,
                                     BlackPixel(display, screen), BlackPixel(display, screen));
        if (!window) return false;
        XMapWindow(display, window);      // MAPPED: xwd needs pixels on a screen
        XSync(display, False);
        return true;
    }
};

void capture(const HostWindow &w, const char *name)
{
    char cmd[512];
    std::snprintf(cmd, sizeof(cmd), "xwd -id 0x%lx -out %s.xwd 2>/dev/null",
                  (unsigned long)w.window, name);
    const int rc = std::system(cmd);
    std::printf("    captured %s.xwd (window 0x%lx, rc %d)\n", name, (unsigned long)w.window, rc);
}

}   // namespace

int onscreenMode()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display) { std::printf("FAIL: no X display\n"); return 1; }
    HostWindow wa, wb;
    if (!wa.open(display, 20, 20, 256, 256) || !wb.open(display, 320, 20, 256, 256)) {
        std::printf("FAIL: could not create the two windows\n");
        return 1;
    }

    EngineConfig cfg;
    cfg.backend = Backend::Vulkan;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "spike_perview_exposure_onscreen-ogre.log";
    cfg.display = static_cast<NativeDisplayHandle>(reinterpret_cast<unsigned long long>(display));

    std::string error;
    auto engine = Engine::create(cfg, error);
    if (!engine) { std::printf("FAIL: Engine::create — %s\n", error.c_str()); return 1; }

    View *a = engine->createView("onA", static_cast<NativeWindowHandle>(wa.window), 256, 256,
                                 Colour(0.10f, 0.10f, 0.12f));
    View *b = engine->createView("onB", static_cast<NativeWindowHandle>(wb.window), 256, 256,
                                 Colour(0.10f, 0.10f, 0.12f));
    if (!a || !b) { std::printf("FAIL: createView — %s\n", engine->lastError().c_str()); return 1; }

    Scene *scene = engine->createScene("spike-onscreen");
    if (!scene) { std::printf("FAIL: createScene\n"); return 1; }
    scene->setAmbient(Colour(0.35f, 0.35f, 0.40f), Colour(0.25f, 0.25f, 0.30f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -0.8f, -0.45f), 6.0f);
    enginetest::addTestCube(scene, Colour(0.90f, 0.85f, 0.80f), 0.0f, 0.35f);
    a->setScene(scene);
    b->setScene(scene);
    enginetest::testCameraLookAt(a, Vec3(2.2f, 1.6f, 2.6f), Vec3(0, 0, 0));
    enginetest::testCameraLookAt(b, Vec3(2.2f, 1.6f, 2.6f), Vec3(0, 0, 0));

    auto settle = [&](double seconds) {
        const int steps = int(seconds * 40.0);
        for (int i = 0; i < steps; ++i) {
            engine->renderOneFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    };

    // ON-SCREEN views take the post chain WITHOUT allowOffscreen — that flag is
    // the offscreen opt-in and nothing else.
    PostFxDesc same;
    same.hdr = true; same.bloom = true; same.bloomThreshold = 3.0f;
    same.exposure = 1.5f; same.exposureMin = 1.5f; same.exposureMax = 1.5f;
    a->setPostFx(same);
    b->setPostFx(same);
    settle(3.0);
    capture(wa, "onscreen-1-settled-A");
    capture(wb, "onscreen-1-settled-B");

    // The defect: B asks for a different threshold, with no listener.
    PostFxDesc bloomy = same; bloomy.bloomThreshold = 0.02f;
    b->setPostFx(bloomy);
    settle(0.5);
    capture(wa, "onscreen-2-nolistener-A");
    capture(wb, "onscreen-2-nolistener-B");

    // The mechanism.
    a->setPerViewGlobalsSpike(true);
    b->setPerViewGlobalsSpike(true);
    settle(0.5);
    capture(wa, "onscreen-3-listener-A");
    capture(wb, "onscreen-3-listener-B");

    // And per-view EXPOSURE, over real time.
    PostFxDesc dark = same;  dark.exposure = -1.5f; dark.exposureMin = -1.5f; dark.exposureMax = -1.5f;
    PostFxDesc bright = same; bright.exposure = 2.0f; bright.exposureMin = 2.0f; bright.exposureMax = 2.0f;
    a->setPostFx(dark);
    b->setPostFx(bright);
    settle(4.0);
    capture(wa, "onscreen-4-exposure-A");
    capture(wb, "onscreen-4-exposure-B");

    a->setPostFx(PostFxDesc());
    b->setPostFx(PostFxDesc());
    engine->destroyScene(scene);
    engine.reset();
    XDestroyWindow(display, wa.window);
    XDestroyWindow(display, wb.window);
    XCloseDisplay(display);
    std::printf("\nON-SCREEN CAPTURES WRITTEN — measure the .xwd files\n");
    return 0;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--onscreen") return onscreenMode();

    EngineConfig cfg;
    cfg.backend = Backend::Vulkan;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "spike_perview_exposure-ogre.log";

    std::string error;
    auto engine = Engine::create(cfg, error);
    if (!engine) { std::printf("FAIL: Engine::create — %s\n", error.c_str()); return 1; }

    // VIEWS BEFORE SCENES, always: a render target must exist before
    // createSceneManager or Ogre segfaults (the startup-order fact, encoded in
    // the API as createView -> createScene -> setScene).
    View *a = engine->createOffscreenView("A", 128, 128, Colour(0.10f, 0.10f, 0.12f));
    View *b = engine->createOffscreenView("B", 128, 128, Colour(0.10f, 0.10f, 0.12f));
    if (!a || !b) { std::printf("FAIL: createOffscreenView\n"); return 1; }

    Scene *scene = engine->createScene("spike");
    if (!scene) { std::printf("FAIL: createScene\n"); return 1; }
    scene->setAmbient(Colour(0.35f, 0.35f, 0.40f), Colour(0.25f, 0.25f, 0.30f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -0.8f, -0.45f), 6.0f);
    enginetest::addTestCube(scene, Colour(0.90f, 0.85f, 0.80f), 0.0f, 0.35f);
    a->setScene(scene);
    b->setScene(scene);
    enginetest::testCameraLookAt(a, Vec3(2.2f, 1.6f, 2.6f), Vec3(0, 0, 0));
    enginetest::testCameraLookAt(b, Vec3(2.2f, 1.6f, 2.6f), Vec3(0, 0, 0));

    auto render = [&](int n) { for (int i = 0; i < n; ++i) engine->renderOneFrame(); };
    // Frames with real time between them: HDR adaptation is charged in WALL
    // CLOCK (~75%/s), so a tight loop of 1 ms frames converges on nothing.
    auto settle = [&](double seconds) {
        const int steps = int(seconds * 40.0);
        for (int i = 0; i < steps; ++i) {
            engine->renderOneFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    };
    auto lumaOf = [&](View *v) { Image im; v->readPixels(im); return brightness(im); };

    const float kHighThreshold = 3.0f;    // almost nothing blooms
    const float kLowThreshold  = 0.02f;   // everything blooms
    // A WELL-EXPOSED frame, pinned. The first run of phases A-C was done at
    // exposure 0, where the scene tonemaps to 1.7/255 — nothing is bright
    // enough to cross ANY bloom threshold, so the control could not move and
    // the probe measured nothing. The threshold experiment only means something
    // on a frame that has highlights.
    const float kExposure = 1.5f;

    // ---- settle both views on IDENTICAL settings --------------------------
    a->setPostFx(hdrDesc(kHighThreshold, kExposure));
    b->setPostFx(hdrDesc(kHighThreshold, kExposure));
    settle(3.0);
    const double a0 = lumaOf(a), b0 = lumaOf(b);
    std::printf("    settled: A = %.2f luma, B = %.2f luma\n", a0, b0);

    // ---- A. CONTROL: the OWNING view's tuning still reaches its frame -----
    // View A is first in the engine's view list, so it is the one whose desc
    // the process-global loop pushes.
    a->setPostFx(hdrDesc(kLowThreshold, kExposure));
    render(3);
    const double aOwner = lumaOf(a);
    std::printf("    control: A's own threshold %.2f -> %.2f -> A = %.2f luma (was %.2f)\n",
                double(kHighThreshold), double(kLowThreshold), aOwner, a0);
    CHECK_MSG(std::fabs(aOwner - a0) > 2.0,
              "CONTROL: the bloom threshold is visible in pixels through the global path "
              "(%.2f -> %.2f)", a0, aOwner);
    a->setPostFx(hdrDesc(kHighThreshold, kExposure));
    render(3);

    // ---- B. THE DEFECT: the NON-owning view's tuning does not ------------
    b->setPostFx(hdrDesc(kLowThreshold, kExposure));
    render(3);
    const double bNoListener = lumaOf(b);
    std::printf("    defect: B's own threshold %.2f -> %.2f -> B = %.2f luma (was %.2f)\n",
                double(kHighThreshold), double(kLowThreshold), bNoListener, b0);
    CHECK_MSG(std::fabs(bNoListener - b0) < 1.0,
              "DEFECT REPRODUCED: a non-owning view's tuning changes NOTHING (%.2f -> %.2f) — "
              "the process-global tuning of POST_CHAIN_SPEC §7.4", b0, bNoListener);

    // ---- C. THE MECHANISM -------------------------------------------------
    a->setPerViewGlobalsSpike(true);
    b->setPerViewGlobalsSpike(true);
    render(3);
    const double aListener = lumaOf(a), bListener = lumaOf(b);
    std::printf("    listener: A = %.2f luma (settled %.2f, threshold still %.2f), "
                "B = %.2f luma (settled %.2f, threshold %.2f)\n",
                aListener, a0, double(kHighThreshold), bListener, b0, double(kLowThreshold));
    CHECK_MSG(std::fabs(bListener - b0) > 2.0,
              "R1: with a per-view listener, THIS view's own bloom threshold reaches THIS "
              "view's passes in the same frame (%.2f -> %.2f)", b0, bListener);
    CHECK_MSG(std::fabs(aListener - a0) < 1.0,
              "R1: …and the OTHER view, which asked for nothing, is unmoved (%.2f vs %.2f)",
              a0, aListener);
    CHECK_MSG(std::fabs(bListener - aListener) > 2.0,
              "R1: the two views therefore render with DIFFERENT tuning in one frame "
              "(%.2f vs %.2f)", aListener, bListener);

    // ---- D. EXPOSURE, the actual P3 knob, over real time ------------------
    a->setPostFx(hdrDesc(kHighThreshold, -1.5f));
    b->setPostFx(hdrDesc(kHighThreshold, +1.5f));
    settle(4.0);
    const double aDark = lumaOf(a), bBright = lumaOf(b);
    std::printf("    exposure: A (pinned -1.5) = %.2f luma, B (pinned +1.5) = %.2f luma\n",
                aDark, bBright);
    CHECK_MSG(bBright > aDark + 4.0,
              "R1: per-view EXPOSURE — two views converge on their OWN pinned exposures "
              "(%.2f vs %.2f)", aDark, bBright);

    // ---- E. and it goes back off ------------------------------------------
    a->setPerViewGlobalsSpike(false);
    b->setPerViewGlobalsSpike(false);
    settle(4.0);
    const double aOff = lumaOf(a), bOff = lumaOf(b);
    std::printf("    listeners off: A = %.2f luma, B = %.2f luma\n", aOff, bOff);
    CHECK_MSG(std::fabs(aOff - bOff) < 2.0,
              "…and removing the listeners puts both views back on ONE global exposure "
              "(%.2f vs %.2f)", aOff, bOff);

    a->setPostFx(PostFxDesc());
    b->setPostFx(PostFxDesc());
    engine->destroyScene(scene);
    engine.reset();

    std::printf(failures == 0 ? "\nSPIKE R1: PASS (0 failures)\n" : "\nSPIKE R1: FAIL (%d)\n",
                failures);
    return failures == 0 ? 0 : 1;
}
