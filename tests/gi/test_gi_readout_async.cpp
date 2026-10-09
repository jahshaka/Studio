// gi.readout_async — THE VOXEL READ-BACK NEVER WAITS FOR THE DEVICE (VOXEL-READOUT-1).
//
// The defect: a cascade's voxel gather writes a small readout (the counts giStatus
// reports) and the feed read it back through Ogre's BufferPacked::readRequest. On
// Vulkan that ticket flushed the queue, and the flush waits for EVERYTHING
// submitted so far (the fence is parked on the frame slot being recorded and
// newCommandBuffer waits on it): 9.0 s / 4.1 s / 2.8 s frames on the UI thread on
// San Miguel's open. giStatus made it worse: it requested the readout on demand
// and mapped the ticket, i.e. a GPU wait between frames from a panel's timer.
//
// The fix (fork 9ff05ca5d): readRequest(..., accurateTracking = false) records the
// copy into the frame's own command buffer - no fence, no flush - and the ticket is
// taken once that frame has finished, polled at the frame head. giStatus returns the
// last LANDED reading and its gather serial.
//
// The rows, over 120 frames of a camera walk that keeps the cascades re-voxelising:
//  1. THE SENTRY: no read-back request submitted the queue (voxelReadoutFlushes, the
//     request's own before/after reading of Ogre's current command buffer) - 0.
//  2. giStatus never waits: its own cost stays under 1 ms on every frame.
//  3. The feed was ACTIVE and the readings LAND: gathers ran, the reading's gather
//     serial advanced, and once the walk stops every cascade's reading describes
//     its newest gather within kLandFrames frames (1 frame head to request it + the
//     3 frames in flight + slack).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const unsigned kSize = 128;
static const int kWalkFrames = 120;
static const int kLandFrames = 6;

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-readout-async-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("readout", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("readout");
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.3f, 0.3f, 0.3f));

    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.8f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.55f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(400.0f, 0.1f, 400.0f));
    // A row of cubes along the walk, so every re-centre gathers something.
    for (int i = 0; i < 24; ++i) {
        const NodeId n = enginetest::addTestCube(scene, Colour(0.2f + 0.03f * i, 0.5f, 0.7f), 0.0f, 0.7f);
        enginetest::setNodePosition(scene, n, Vec3(-3.0f + 0.25f * (i % 5), 0.5f, 10.0f - 4.0f * i));
        enginetest::setNodeScale(scene, n, Vec3(1.0f, 1.0f, 1.0f));
    }
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 12.0f), Vec3(0.0f, 0.5f, 0.0f));
    for (int i = 0; i < 2; ++i) e->renderOneFrame();

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;
    CHECK(scene->setGlobalIllumination(gi), "the High chain built");
    for (int i = 0; i < 8; ++i) e->renderOneFrame();
    GiStatus st0 = scene->giStatus();
    CHECK(!st0.cascades.empty(), "the chain has cascades");
    if (st0.cascades.empty()) { std::printf("\n%d CHECK(S) FAILED\n", failures + 1); return 1; }
    const size_t nc = st0.cascades.size();
    long long gathers0 = 0, reading0 = 0;
    for (const auto &c : st0.cascades) { gathers0 += c.voxelGathers; reading0 += c.voxelReadingGather; }

    // ---- THE WALK: 0.4 m a frame down the row, so cascade 0 re-centres often ----
    double worstMs = 0.0;
    int worstFrame = -1;
    long long flushes = 0, gathers = 0, readings = 0;
    for (int f = 0; f < kWalkFrames; ++f) {
        const float z = 12.0f - 0.4f * float(f);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, z), Vec3(0.0f, 0.5f, z - 12.0f));
        e->renderOneFrame();
        const auto t0 = std::chrono::steady_clock::now();
        const GiStatus st = scene->giStatus();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms > worstMs) { worstMs = ms; worstFrame = f; }
        flushes = gathers = readings = 0;
        for (const auto &c : st.cascades) {
            flushes += c.voxelReadoutFlushes;
            gathers += c.voxelGathers;
            readings += c.voxelReadingGather;
        }
    }
    std::printf("   %zu cascades; gathers %lld -> %lld, reading serials %lld -> %lld; read-back flushes %lld; "
                "worst giStatus %.3f ms (frame %d)\n",
                nc, gathers0, gathers, reading0, readings, flushes, worstMs, worstFrame);

    CHECK(flushes == 0, "THE SENTRY: no voxel read-back request submitted the queue mid-frame (0 flushes)");
    CHECK(worstMs <= 1.0, ("giStatus never waits: worst " + std::to_string(worstMs) +
                           " ms over the walk (<= 1 ms)").c_str());
    CHECK(gathers - gathers0 >= 8, "the feed was ACTIVE: the walk re-voxelised (>= 8 gathers)");
    CHECK(readings > reading0, "the readings LANDED during the walk (the gather serials advanced)");

    // ---- THE WALK STOPS: every reading catches its newest gather in frames ------
    // A re-centre still owed may gather once more; settle the queue first by
    // watching the gather count, then time the landing of the LAST gather.
    long long lastGathers = -1;
    for (int i = 0; i < 64; ++i) {
        long long g = 0;
        for (const auto &c : scene->giStatus().cascades) g += c.voxelGathers + (c.pending ? 1 : 0);
        if (g == lastGathers) break;
        lastGathers = g;
        e->renderOneFrame();
    }
    const int landed = enginetest::settleVoxelReadings(e, scene, 32);
    std::printf("   the newest gather's reading landed %d frame(s) after the walk's queue drained\n", landed);
    CHECK(landed >= 0 && landed <= kLandFrames,
          ("a reading lands within " + std::to_string(kLandFrames) + " frames of its gather (" +
           std::to_string(landed) + ")").c_str());
    long long finalFlushes = 0;
    for (const auto &c : scene->giStatus().cascades) finalFlushes += c.voxelReadoutFlushes;
    CHECK(finalFlushes == 0, "...and still no flush");

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
