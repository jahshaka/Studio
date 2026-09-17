// engine.frame_catch — A FRAME THAT THREW STILL CLOSES (lane FRAME-CATCH-1,
// 2026-09-18; the Fable read of VR-INPUT-1E-FIX, ledger §679).
//
// THE SUBJECT. `OgreEngine::renderOneFrame` wraps its body in
// `JAH_TRY { ... } JAH_CATCH(mLastError, )`, and that macro RETURNS — which is
// what makes it usable on a hundred bool-returning boundary calls and what made
// every line written after the catch dead code on the one path those lines were
// written for. Four things lived there: the OpenXR frame's close (with the eye
// swapchain images the frame had acquired), the render-loop monitor's record,
// the end of a session the runtime has taken away, and the device-lost latch.
// A frame that threw did none of them, with comments claiming the opposite.
//
// THE DESKTOP HALF IS HERE (no runtime, no headset — a plain offscreen engine
// on the rig's display):
//
//   1. THE MONITOR'S RECORD IS CLOSED BY THE FRAME THAT THREW. One record per
//      renderOneFrame, thrown or not, each with its own frame number. Before
//      the fix a thrown frame's record was left open and the NEXT frame's
//      `beginFrame` overwrote it — so N thrown frames meant N records that
//      never existed, and a capture that straddled one silently lost it.
//   2. THE FRAME AFTER A THROW IS A NORMAL FRAME: the view presents again and
//      the picture comes back. (The leak this stands in for is only visible in
//      a session, where the thing not released is a swapchain image the
//      runtime cannot re-hand — `vr.session` asserts that half against Monado.)
//   3. THE DEVICE-LOST LATCH IS REACHED FROM A THROWN FRAME. That is the whole
//      point of XID-2's latch: a real loss surfaces as VK_ERROR_DEVICE_LOST
//      thrown out of the frame's commit, i.e. as exactly this kind of frame.
//
// HOW A FRAME IS MADE TO THROW: `Engine::setFrameFault` (FrameFault in
// Types.h), the engine's own test hook — not an environment variable and not a
// global. It raises the fault inside the frame, after the render has been
// recorded and before anything closes, which is where a device loss lands; and
// `lastError()` afterwards carries the fault's own message, so every case below
// proves the fault really fired instead of passing vacuously.
//
// WHAT IT CANNOT PROVE, said plainly: a REAL `VK_ERROR_DEVICE_LOST`. Inducing
// one takes the box's GPU down with it (VOXMERGE-1 did it by accident and paid
// for it in Xids), so `FrameFault::ThrowDeviceLost` fakes the engine's own
// report and nothing else — the render system, the driver and any live session
// are untouched, which is why case 3 can still render a frame afterwards.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const unsigned kSize = 96;

/// Did the injected fault really fire? Its message names itself.
static bool faultFired(const Engine *e)
{
    return e->lastError().find("injected frame fault") != std::string::npos;
}

/// How much of a picture is not its corner pixel — "something was drawn".
static double drawnFraction(const Image &img)
{
    if (img.rgba.size() < 4u) return 0.0;
    const unsigned char c0 = img.rgba[0], c1 = img.rgba[1], c2 = img.rgba[2];
    size_t n = 0;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4)
        if (img.rgba[i] != c0 || img.rgba[i + 1] != c1 || img.rgba[i + 2] != c2) ++n;
    return double(n) / double(img.rgba.size() / 4);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-engine-frame-catch-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("frame_catch", kSize, kSize, Colour(0.10f, 0.12f, 0.16f, 1.0f));
    if (!view) { std::printf("FAIL: offscreen view: %s\n", e->lastError().c_str()); return 1; }
    Scene *scene = e->createScene("frame_catch");
    if (!scene) { std::printf("FAIL: scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(scene);
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.55f), 3.14159f);
    const NodeId cube = enginetest::addTestCube(scene, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.5f);
    enginetest::setNodePosition(scene, cube, Vec3(0.0f, 0.0f, 0.0f));
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(1.6f, 1.4f, 2.2f), Vec3(0, 0, 0)));

    // A warm frame or two: the first frames of a process are the shader/PSO
    // compile storm, and nothing below is a timing assertion, but a picture is.
    for (int i = 0; i < 8; ++i) e->renderOneFrame();
    Image warm;
    CHECK(view->readPixels(warm) && drawnFraction(warm) > 0.02,
          "the fixture renders a picture before anything is faulted");

    // =====================================================================
    // CASE 0 — THE HOOK IS OFF UNLESS IT IS ARMED
    // =====================================================================
    {
        const std::string errBefore = e->lastError();
        e->setFrameFault(FrameFault::Throw, 0u);          // frames 0 = disarmed
        e->renderOneFrame();
        CHECK(e->lastError() == errBefore && !faultFired(e),
              "a fault armed for ZERO frames throws nothing at all");
        e->setFrameFault(FrameFault::None, 5u);           // kind None = disarmed
        e->renderOneFrame();
        CHECK(e->lastError() == errBefore && !faultFired(e),
              "...and neither does FrameFault::None with frames to spare");
    }

    // =====================================================================
    // CASE 1 — EVERY THROWN FRAME CLOSES THE MONITOR'S RECORD
    // =====================================================================
    // The strongest statement available on the desktop, and it is a COUNT, not
    // a time: the monitor records exactly one frame per renderOneFrame. A
    // thrown frame that does not close leaves `mCurrent` open, and the next
    // `beginFrame` throws it away — so before the fix this count was short by
    // one per throw, and the frame numbers had a hole in them.
    {
        e->setFrameMonitor(MonitorLevel::Review);
        std::vector<FrameRecord> drained;
        e->takeFrameRecords(drained);                     // start from a clean ring
        drained.clear();
        const unsigned long long rec0 = e->monitorStatus().framesRecorded;

        const unsigned kGood = 3u, kThrown = 4u;
        for (unsigned i = 0; i < kGood; ++i) e->renderOneFrame();
        e->setFrameFault(FrameFault::Throw, kThrown);
        unsigned fired = 0;
        for (unsigned i = 0; i < kThrown; ++i) {
            e->renderOneFrame();
            if (faultFired(e)) ++fired;
        }
        CHECK(fired == kThrown, "the fault fired on every frame it was armed for "
                                "(the case is not vacuous)");
        for (unsigned i = 0; i < kGood; ++i) e->renderOneFrame();

        const unsigned long long recorded = e->monitorStatus().framesRecorded - rec0;
        std::printf("   %u frames rendered (%u of them thrown) -> %llu monitor records\n",
                    kGood * 2u + kThrown, kThrown, recorded);
        CHECK(recorded == (unsigned long long)(kGood * 2u + kThrown),
              "ONE MONITOR RECORD PER FRAME, THROWN OR NOT (it was one short per throw)");
        CHECK(!e->monitorStatus().framesDropped,
              "...and the ring dropped none of them, so the count above is the ring's");

        // THE RING IS DRAINED AFTER THE MONITOR IS STOPPED, deliberately: a
        // record waits in the holding queue until its GPU samples arrive (two
        // frames late) or it ages out, and STOPPING is what flushes the tail
        // (MonitorStatus's note). Draining while running reads a few records
        // short of `framesRecorded` — which is a fact about GPU sampling and
        // not about this lane.
        e->setFrameMonitor(MonitorLevel::Off);
        drained.clear();
        e->takeFrameRecords(drained);
        unsigned long long last = 0ull;
        bool increasing = !drained.empty();
        for (const FrameRecord &r : drained) {
            if (last && r.frame <= last) increasing = false;
            last = r.frame;
        }
        std::printf("   the ring holds %zu records, frames", drained.size());
        for (const FrameRecord &r : drained) std::printf(" %llu", r.frame);
        std::printf("\n");
        CHECK(drained.size() == size_t(kGood * 2u + kThrown) && increasing,
              "...and every record carries its OWN frame number, strictly increasing "
              "(a frame left open shares the next one's)");
    }

    // =====================================================================
    // CASE 2 — THE FRAME AFTER A THROW IS AN ORDINARY FRAME
    // =====================================================================
    {
        const unsigned long long presented0 = view->framesPresented();
        e->setFrameFault(FrameFault::Throw, 2u);
        e->renderOneFrame();
        e->renderOneFrame();
        CHECK(view->framesPresented() == presented0,
              "a thrown frame does not claim to have presented (it did not get that far)");
        for (int i = 0; i < 3; ++i) e->renderOneFrame();
        CHECK(view->framesPresented() >= presented0 + 3ull,
              "THE VIEW PRESENTS AGAIN after the faults are spent");
        Image after;
        CHECK(view->readPixels(after) && drawnFraction(after) > 0.02,
              "...and the picture is back (nothing the throw left behind stops the frame)");
    }

    // =====================================================================
    // CASE 3 — THE DEVICE-LOST LATCH IS REACHED FROM A THROWN FRAME
    // =====================================================================
    // LAST, because the latch is one-way by design (a lost device never comes
    // back) and because it writes `lastError`.
    {
        CHECK(!e->deviceLost(), "the device is not lost before the fault (nothing faked it yet)");
        e->setFrameFault(FrameFault::ThrowDeviceLost, 1u);
        e->renderOneFrame();
        CHECK(faultFired(e) || e->deviceLost(), "the device-lost fault fired");
        CHECK(e->deviceLost(),
              "A DEVICE LOSS INSIDE A FRAME LATCHES: the latch lives after the catch, and "
              "until this lane a throw returned straight past it");
        CHECK(e->lastError() == "the GPU device was lost",
              "...and the engine says which fact it latched, not the fault's own message");
        // THE FAKE IS ONLY THE ENGINE'S OWN REPORT: nothing about the driver,
        // the render system or the pictures changed, which is why this frame
        // still draws. (A real loss stops the render system dead — patch 0072
        // — and that cannot be exercised without taking the box's GPU with it.)
        e->renderOneFrame();
        Image alive;
        CHECK(view->readPixels(alive) && drawnFraction(alive) > 0.02,
              "the FAKED loss faked nothing else: the render system is untouched");
    }

    e->destroyScene(scene);
    e->destroyView(view);
    std::printf("%s: %d failures\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
