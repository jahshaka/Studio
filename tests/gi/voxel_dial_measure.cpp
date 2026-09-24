// VOXEL-DIAL (PHOTON-VOXEL-4) — a TOOL, not a suite: the scene-fitted voxel volume's resolution
// as a DIAL against its cost, paired in one process (GiParams::testVoxelResolution). The subject
// is gi.ddgi_ambient's corner: at Low's 32 cells along the longest side (0.5625 m on its 18 m
// fixture) the field's wall foot reads 85.0 % against a derived bar of 73.2-83.9 %; at 64
// (0.28125 m) it reads 83.7 % against 76.1-84.0 %. This prints what the 64 costs.
//
// For each arm, alternating 32 / 64 / 32 / 64 ... (pairs, so drift lands on both): the GI is
// rebuilt at the arm's resolution with the field on (the corner case's parameters: Low, no
// bounce, the fixture's bounds) and the frames are counted until GI is at rest. Printed per
// arm: the whole-arm rebuild's GPU ms (`vct.rebuild`, ogre-patch 0027 timestamps), the light
// injections' GPU ms summed over the settle, the field builds' CPU ms, the settle in frames,
// and the steady frame's pass GPU ms (the pixel's cones) as a median over 30 frames at rest.
// Run it holding the GPU lock: scripts/gpu-exclusive.sh ./voxel_dial_measure [pairs]
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

struct Arm { double settleGpu = 0, settleFrameGpu = 0; double rebuildGpu = -1, rebuildMs = -1, lightGpu = 0, fieldMs = 0, frameGpu = 0; int settle = 0, lights = 0; };

static double median(std::vector<double> v)
{
    if (v.empty()) return -1;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int main(int argc, char **argv)
{
    const int pairs = argc > 1 ? std::max(1, std::atoi(argv[1])) : 4;
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "voxel-dial-measure-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("voxdial", 128, 128, Colour(0, 0, 0));
    Scene *s = e->createScene("voxdial");
    view->setScene(s);
    s->setAmbient(Colour(0.40f, 0.40f, 0.44f), Colour(0.10f, 0.10f, 0.12f));
    const auto slab = [&](const Vec3 &pos, const Vec3 &scale) {
        const NodeId n = enginetest::addTestCube(s, Colour(0.9f, 0.9f, 0.9f), 0.0f, 0.9f);
        enginetest::setNodePosition(s, n, pos);
        enginetest::setNodeScale(s, n, scale);
    };
    slab(Vec3(0.0f, -0.05f, 0.0f), Vec3(16.0f, 0.1f, 16.0f));
    slab(Vec3(0.0f, 2.0f, -2.0f), Vec3(12.0f, 4.0f, 0.4f));
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 7.0f), Vec3(0.0f, 0.0f, -1.0f));

    // warm-up: the permutations of both arms compiled before anything is timed
    for (unsigned res : { 32u, 64u }) {
        GiParams gi;
        gi.mode = GiMode::Vct; gi.quality = GiQuality::Low; gi.numBounces = 0; gi.ddgi = GiToggle::On;
        gi.testBoundsMin = Vec3(-9.0f, -1.5f, -9.0f); gi.testBoundsMax = Vec3(9.0f, 7.5f, 9.0f);
        gi.testVoxelResolution = res;
        s->setGlobalIllumination(gi);
        for (int n = 0; n < 4000 && !s->giStatus().giAtRest; ++n) e->renderOneFrame();
        for (int n = 0; n < 10; ++n) e->renderOneFrame();
    }

    std::vector<Arm> arms[2];
    for (int p = 0; p < pairs; ++p)
        for (int k = 0; k < 2; ++k) {
            const unsigned res = k ? 64u : 32u;
            GiParams gi;
            gi.mode = GiMode::Vct; gi.quality = GiQuality::Low; gi.numBounces = 0; gi.ddgi = GiToggle::On;
            gi.testBoundsMin = Vec3(-9.0f, -1.5f, -9.0f); gi.testBoundsMax = Vec3(9.0f, 7.5f, 9.0f);
            gi.testVoxelResolution = res;
            e->setFrameMonitor(MonitorLevel::Review);
            s->setGlobalIllumination(gi);
            Arm a;
            e->renderOneFrame();
            for (; a.settle < 4000 && !s->giStatus().giAtRest; ++a.settle) e->renderOneFrame();
            for (int n = 0; n < 8; ++n) e->renderOneFrame();   // the last timestamps come back
            std::vector<FrameRecord> recs;
            e->takeFrameRecords(recs);
            for (const FrameRecord &r : recs) {
                if (r.gpuMs >= 0) a.settleFrameGpu += r.gpuMs;
                for (const CacheWork &w : r.cacheWork) {
                    if (w.gpuMs >= 0) a.settleGpu += w.gpuMs;
                    if (w.detail == "vct.rebuild") { a.rebuildGpu = w.gpuMs; a.rebuildMs = w.ms; }
                    else if (w.detail.rfind("vct.light", 0) == 0 && w.gpuMs >= 0) { a.lightGpu += w.gpuMs; ++a.lights; }
                    else if (w.detail == "vct.field.build" && w.ms >= 0) a.fieldMs += w.ms;
                }
            }
            for (int n = 0; n < 30; ++n) e->renderOneFrame();
            e->takeFrameRecords(recs);
            std::vector<double> g;
            for (const FrameRecord &r : recs)
                if (r.gpuMs >= 0) g.push_back(r.gpuMs);
            a.frameGpu = median(g);
            e->setFrameMonitor(MonitorLevel::Off);
            GiVoxelVolume v;
            const bool haveV = s->giVoxelVolume(0, v) && v.available;
            std::printf("res %2u (%s): rebuild GPU %.3f ms (CPU %.2f ms) | light injections %d, GPU %.3f ms | field "
                        "builds CPU %.2f ms | settle %d frames | steady frame passes GPU %.3f ms | the settle's GI work GPU %.2f ms, its passes GPU %.2f ms\n",
                        res, haveV ? (std::to_string(v.width) + "x" + std::to_string(v.height) + "x" + std::to_string(v.depth)).c_str() : "?",
                        a.rebuildGpu, a.rebuildMs, a.lights, a.lightGpu, a.fieldMs, a.settle, a.frameGpu, a.settleGpu, a.settleFrameGpu);
            arms[k].push_back(a);
        }
    for (int k = 0; k < 2; ++k) {
        std::vector<double> rb, lg, fg, st, sg, sp;
        for (const Arm &a : arms[k]) {
            rb.push_back(a.rebuildGpu); lg.push_back(a.lightGpu); fg.push_back(a.frameGpu); st.push_back(a.settle);
            sg.push_back(a.settleGpu); sp.push_back(a.settleFrameGpu);
        }
        std::printf("MEDIAN res %s: rebuild GPU %.3f ms | injections GPU %.3f ms | settle %.0f frames | steady frame GPU %.3f ms"
                    " | settle GI work GPU %.2f ms, passes %.2f ms\n",
                    k ? "64" : "32", median(rb), median(lg), median(st), median(fg), median(sg), median(sp));
    }
    view->setScene(nullptr);
    e->destroyScene(s);
    e->destroyView(view);
    return 0;
}
