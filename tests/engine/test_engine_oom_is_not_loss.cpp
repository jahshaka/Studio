// engine.oom_is_not_loss — AN OUT-OF-MEMORY IS NOT A DEVICE LOSS (lane FORK-OOM-1,
// 2026-09-27; SPECS/audits/GPU_LOSS_AUDIT_2026-09-27.md §A1).
//
// THE SUBJECT. Upstream Ogre-Next's `onVulkanFailure` (the function every
// failed `checkVkResult` calls) latched VK_ERROR_OUT_OF_HOST_MEMORY and
// VK_ERROR_OUT_OF_DEVICE_MEMORY into `VulkanDevice::mDeviceLostReason` exactly
// like VK_ERROR_DEVICE_LOST. So one refused 64 MB pool allocation on a busy
// card made `isDeviceLost()` true, the engine latched "THE GPU DEVICE WAS
// LOST", and the host ended the process — over a device Vulkan defines as
// still valid (the audit: 31 OOMs and zero DEVICE_LOSTs in one gate's log, ~47
// gate reds in a day, zero kernel faults at any of them). The fork's commit
// "OOM is not a device loss" latches VK_ERROR_DEVICE_LOST only.
//
// WHAT THIS PROVES, through the render system's REAL failure path
// (`FrameFault::VulkanOutOfDeviceMemory` calls `Ogre::onVulkanFailure` with the
// live device — the fork's own decision is what is exercised, nothing copied):
//   1. an OOM inside a frame reaches the host as `lastError()` saying "GPU out
//      of memory", and the device is NOT latched as lost;
//   2. the process carries on and the frames after it render the SAME bytes as
//      the settled picture before it;
//   2b. AN OOM INSIDE THE FRAME'S RECORDING IS TERMINAL (FORK-OOM-1's lead
//      call): Ogre's frame state is not whole after it (the MERGE tier on a
//      full card: 12 runs crashed in the NEXT frame), so the engine latches
//      GpuFault::OutOfMemoryInFrame, renders nothing more, and the host ends
//      the process with exit 1 and the honest text — never a signal, never
//      "device lost". Run in a CHILD process (this binary, `--in-record`),
//      because the arm ends its process.
//   3. THE NEGATIVE CONTROL: `FrameFault::VulkanDeviceLost` — the same call with
//      VK_ERROR_DEVICE_LOST — still latches, the engine says so, and (like
//      every host, devicelossend.h) the process ends WITHOUT an orderly
//      teardown. It REALLY latches the render system, which is why it is last
//      and why this is its own binary.
//
// The in-record door fires from Ogre's `frameRenderingQueued` (inside
// `Root::_updateAllRenderTargets`, before the swap) — the nearest point a
// frame listener reaches to an allocateVbo called from inside a pass.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const unsigned kSize = 96;

static bool has(const std::string &s, const char *what) { return s.find(what) != std::string::npos; }

/// Render until two consecutive pictures are byte-equal (trap 7: settle in
/// frames, read until the value stops moving). False if it never settles.
static bool settle(Engine *e, View *view, Image &out, int maxFrames = 240)
{
    Image prev;
    for (int i = 0; i < maxFrames; ++i) {
        e->renderOneFrame();
        Image cur;
        if (!view->readPixels(cur)) return false;
        if (i > 0 && cur.rgba == prev.rgba) { out = cur; return true; }
        prev = cur;
    }
    return false;
}

static void endLikeAHost(const char *why)
{
    std::printf("%s: %d failures (%s)\n", failures ? "FAIL" : "PASS", failures, why);
    std::fflush(nullptr);
    ::_exit(failures ? 1 : 0);   // NO DESTRUCTORS after a real latch — devicelossend.h's rule
}

/// The child: boot, settle, an OOM INSIDE the recording, then end like a host.
static int inRecordChild()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-engine-oom-in-record-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("CHILD-FAIL: engine create: %s\n", err.c_str()); return 2; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("oom_rec", kSize, kSize, Colour(0.10f, 0.12f, 0.16f, 1.0f));
    Scene *scene = view ? e->createScene("oom_rec") : nullptr;
    if (!scene) { std::printf("CHILD-FAIL: fixture: %s\n", e->lastError().c_str()); return 2; }
    view->setScene(scene);
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.55f), 3.14159f);
    enginetest::addTestCube(scene, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.5f);
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(1.6f, 1.4f, 2.2f), Vec3(0, 0, 0)));
    for (int i = 0; i < 8; ++i) e->renderOneFrame();
    if (e->gpuFault() != GpuFault::None) { std::printf("CHILD-FAIL: a fault before the door\n"); return 2; }

    e->setFrameFault(FrameFault::VulkanOutOfDeviceMemoryInRecord, 1u);
    e->renderOneFrame();
    std::printf("CHILD gpuFault=%d deviceLost=%d\n", int(e->gpuFault()), int(e->deviceLost()));
    std::printf("CHILD lastError=%s\n", e->lastError().c_str());
    // A latched engine renders nothing: this call must return, not crash.
    const unsigned long long frames = view->framesPresented();
    e->renderOneFrame();
    std::printf("CHILD survived-next-frame=%d\n", view->framesPresented() == frames ? 1 : 0);
    // THE HOST'S END (src/viewport/devicelossend.h): exit 1 for an OOM, no teardown.
    std::fflush(nullptr);
    ::_exit(e->gpuFault() == GpuFault::OutOfMemoryInFrame ? 1 : 2);
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--in-record") == 0) return inRecordChild();

    // =====================================================================
    // CASE 2b — AN OOM INSIDE THE RECORDING ENDS THE PROCESS CLEANLY (child)
    // =====================================================================
    {
        const std::string cmd = std::string("'") + argv[0] + "' --in-record 2>&1";
        FILE *pipe = ::popen(cmd.c_str(), "r");
        std::string out;
        if (pipe) {
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, pipe)) > 0) out.append(buf, n);
        }
        const int status = pipe ? ::pclose(pipe) : -1;
        const bool exited = status != -1 && WIFEXITED(status);
        const int code = exited ? WEXITSTATUS(status) : -1;
        std::printf("child: exited=%d code=%d signalled=%d\n", int(exited), code,
                    int(status != -1 && WIFSIGNALED(status)));
        const size_t at = out.find("CHILD lastError=");
        std::printf("%s\n", at == std::string::npos ? "(no CHILD lastError line)"
                                                     : out.substr(at, out.find('\n', at) - at).c_str());
        if (!has(out, "VK_ERROR_") && has(out, "injected frame fault")) {
            std::printf("SKIP: this engine has no Vulkan render system linked; nothing to prove\n");
            return 77;
        }
        CHECK(exited && code == 1,
              "AN OOM INSIDE THE RECORDING ENDS THE PROCESS WITH EXIT 1 (not a signal, not a crash)");
        CHECK(has(out, "CHILD gpuFault=2 deviceLost=0"),
              "...latched as GpuFault::OutOfMemoryInFrame, and NOT as a device loss");
        CHECK(has(out, "CHILD lastError=GPU out of memory (VK_ERROR_OUT_OF_DEVICE_MEMORY; the device is NOT lost - ") &&
              has(out, "67108864-byte pool"),
              "...with the honest text first, naming the pool");
        CHECK(has(out, "CHILD survived-next-frame=1"),
              "...and a frame asked for after the latch returns without touching Ogre");
    }

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-engine-oom-is-not-loss-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("oom", kSize, kSize, Colour(0.10f, 0.12f, 0.16f, 1.0f));
    if (!view) { std::printf("FAIL: offscreen view: %s\n", e->lastError().c_str()); return 1; }
    Scene *scene = e->createScene("oom");
    if (!scene) { std::printf("FAIL: scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(scene);
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.55f), 3.14159f);
    const NodeId cube = enginetest::addTestCube(scene, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.5f);
    enginetest::setNodePosition(scene, cube, Vec3(0.0f, 0.0f, 0.0f));
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(1.6f, 1.4f, 2.2f), Vec3(0, 0, 0)));

    // ---- the control picture: settled, before anything is faulted --------
    Image control;
    CHECK(settle(e, view, control), "the fixture settles to one picture (two equal frames in a row)");
    CHECK(!e->deviceLost(), "the device is not lost before any fault");

    // =====================================================================
    // CASE 1 — AN OOM IN A FRAME: an honest error, no latch
    // =====================================================================
    e->setFrameFault(FrameFault::VulkanOutOfDeviceMemory, 1u);
    e->renderOneFrame();
    const std::string oomError = e->lastError();
    std::printf("lastError after the OOM frame: %s\n", oomError.c_str());
    if (!has(oomError, "VK_ERROR_") && has(oomError, "injected frame fault")) {
        // The engine was built without the Vulkan render system linked (no
        // JAH_RAY_QUERY — macOS): the Vulkan kinds degrade to a plain throw.
        std::printf("SKIP: this engine has no Vulkan render system linked; nothing to prove\n");
        e->destroyScene(scene);
        e->destroyView(view);
        return 77;
    }
    CHECK(has(oomError, "injected frame fault"), "the OOM fault really fired (its own text is there)");
    CHECK(has(oomError, "GPU out of memory"),
          "THE HOST READS 'GPU out of memory': lastError says what happened, first");
    CHECK(has(oomError, "VK_ERROR_OUT_OF_DEVICE_MEMORY") && has(oomError, "67108864-byte pool"),
          "...with the VkResult and the pool's size");
    CHECK(!has(oomError, "device was lost"), "...and does not claim the device was lost");
    CHECK(!e->deviceLost(),
          "AN OOM IS NOT A DEVICE LOSS: the render system did not latch it (the fork's onVulkanFailure)");

    // =====================================================================
    // CASE 2 — THE PROCESS CARRIES ON, AND THE PICTURE IS THE SAME
    // =====================================================================
    const unsigned long long presented0 = view->framesPresented();
    Image after;
    CHECK(settle(e, view, after), "the frames after the OOM render and settle");
    CHECK(view->framesPresented() > presented0, "the view presents again after the OOM frame");
    CHECK(after.rgba == control.rgba,
          "THE PICTURE AFTER THE OOM IS BYTE-EQUAL TO THE CONTROL (nothing the OOM left stops a frame)");
    CHECK(!e->deviceLost(), "...and nothing latched later either");

    // A second one is no different: the OOM is an ordinary, repeatable error.
    e->setFrameFault(FrameFault::VulkanOutOfDeviceMemory, 2u);
    e->renderOneFrame();
    e->renderOneFrame();
    CHECK(has(e->lastError(), "GPU out of memory") && !e->deviceLost(),
          "two more OOM frames: the same honest error, still no latch");
    Image again;
    CHECK(settle(e, view, again) && again.rgba == control.rgba,
          "...and the picture comes back byte-equal again");

    // =====================================================================
    // CASE 3 — THE NEGATIVE CONTROL: VK_ERROR_DEVICE_LOST STILL LATCHES
    // =====================================================================
    e->setFrameFault(FrameFault::VulkanDeviceLost, 1u);
    e->renderOneFrame();
    CHECK(e->deviceLost(),
          "A REAL VK_ERROR_DEVICE_LOST through the same path STILL LATCHES (the fork kept that half)");
    CHECK(e->lastError() == "the GPU device was lost", "...and the engine says which fact it latched");
    // The render system now vetoes every frame (it never recreates a lost
    // device); a host ends here, and so does this suite. (Not asserted through
    // `framesPresented`: the engine counts a presentation for every enabled
    // view after the frame body whether or not Root vetoed the frame.)
    endLikeAHost("ended like a host after the latch");
    return 0;
}
