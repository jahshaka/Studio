// gi.field_dependency — THE IRRADIANCE FIELD GENERATES ONLY WHEN ITS INPUTS CHANGED
// (ENGINE-UPGRADE-2 stage 2; the ASYNC spec's capture: IrradianceField/Gen ran on EVERY
// frame of a mover fixture at 4.9-5.7 ms).
//
// The field's work is filed by the monitor as cache work (`ifd.*` details: converge,
// replace, refine, follow). On a converged room with the sun:
//   A. THE CAMERA WALKS (1.4 m/s), nothing else moves: the field's inputs (the light in
//      the voxels, the geometry) did not change — only a follow of the cascade-0 window
//      may run (a scroll integrates the probes that entered), never a re-integration.
//   B. TWO MOVERS move (no cards, not voxelised), the camera still, the field converged:
//      the voxel light does not change with them — no field work at all.
//   A'. THE WALK'S SCROLLS integrate the entered planes and owe their refinements over
//      those planes only (PhotonIrradianceField's band): the work is bounded by the
//      entered probes x the target samples, never a whole-grid refinement per scroll
//      (RED ON THE BASE: 41,472 integrations in the walk and a whole-grid debt of
//      129,024 more over the next 126 frames).
// Counts per frame, never clocks.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
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

struct FieldWork {
    unsigned frames = 0, framesWithWork = 0;
    unsigned long long probes = 0;   ///< probe integrations (the work's units)
    double gpuMs = 0.0;              ///< ...and their GPU ms where timed
    std::map<std::string, unsigned> byDetail;   // frames carrying each detail
};

static FieldWork fieldWork(const std::vector<FrameRecord> &recs) {
    FieldWork w;
    for (const FrameRecord &r : recs) {
        ++w.frames;
        bool any = false;
        std::map<std::string, bool> seen;
        for (const CacheWork &c : r.cacheWork)
            if (c.detail.rfind("ifd.", 0) == 0) {
                any = true;
                seen[c.detail] = true;
                w.probes += c.units;
                if (c.gpuMs > 0.0f) w.gpuMs += c.gpuMs;
            }
        if (any) ++w.framesWithWork;
        for (const auto &kv : seen) ++w.byDetail[kv.first];
    }
    return w;
}

static void print(const char *tag, const FieldWork &w) {
    std::printf("   [%s] %u frames, %u with field work, %llu probe integrations, %.2f GPU ms:", tag, w.frames,
                w.framesWithWork, w.probes, w.gpuMs);
    for (const auto &kv : w.byDetail) std::printf(" %s x%u", kv.first.c_str(), kv.second);
    std::printf("\n");
}

int main(int argc, char **argv)
{
    // --stills: the measurement of STILL objects moved every frame (by hand; not the row).
    const bool stillsArm = argc > 1 && std::string(argv[1]) == "--stills";
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-field-dependency-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();

    View *view = e->createOffscreenView("fielddep", kSize, kSize, Colour(0, 0, 0));
    Scene *s = e->createScene("fielddep");
    view->setScene(s);
    view->setOffscreenContract(OffscreenContract::Live);
    view->setShadows(true);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.04f, 0.04f, 0.04f));
    const auto slab = [&](const Vec3 &pos, const Vec3 &scale) {
        const NodeId n = enginetest::addTestCube(s, Colour(0.7f, 0.7f, 0.7f), 0.0f, 0.9f);
        enginetest::setNodePosition(s, n, pos);
        enginetest::setNodeScale(s, n, scale);
        return n;
    };
    slab(Vec3(0.0f, -0.05f, 0.0f), Vec3(12.0f, 0.1f, 12.0f));   // floor
    slab(Vec3(-6.0f, 2.0f, 0.0f), Vec3(0.2f, 4.0f, 12.0f));     // walls
    slab(Vec3(6.0f, 2.0f, 0.0f), Vec3(0.2f, 4.0f, 12.0f));
    slab(Vec3(0.0f, 2.0f, -6.0f), Vec3(12.0f, 4.0f, 0.2f));
    NodeId movers[2], stills[2];
    for (int i = 0; i < 2; ++i) {
        stills[i] = enginetest::addTestCube(s, Colour(0.2f, 0.5f, 0.8f), 0.0f, 0.6f);
        enginetest::setNodePosition(s, stills[i], Vec3(-2.5f + 5.0f * float(i), 0.5f, -3.0f));
        movers[i] = enginetest::addTestCube(s, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.6f);
        s->setNodeMovable(movers[i], true);
        enginetest::setNodePosition(s, movers[i], Vec3(-1.5f + 3.0f * float(i), 0.5f, 0.0f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::On;   // the field (the irradiance field's generation) is the subject
    s->setGlobalIllumination(gi);
    Vec3 cam(0.0f, 1.6f, 4.0f);
    enginetest::testCameraLookAt(view, cam, Vec3(0.0f, 1.0f, -4.0f));
    // CONVERGE: render until the field has no work for 30 frames running (bounded).
    e->setFrameMonitor(MonitorLevel::Review);
    int quiet = 0, settle = 0;
    unsigned settleWork = 0;
    for (; settle < 1200 && quiet < 30; ++settle) {
        e->renderOneFrame();
        std::vector<FrameRecord> recs;
        e->takeFrameRecords(recs);
        const FieldWork w = fieldWork(recs);
        settleWork += w.framesWithWork;
        quiet = (w.frames && !w.framesWithWork) ? quiet + int(w.frames) : (w.frames ? 0 : quiet);
    }
    std::printf("   converged after %d frames (quiet %d; %u frames with field work)\n", settle, quiet, settleWork);
    CHECK(quiet >= 30, "the field converges and goes quiet on a still scene");
    const GiStatus g0 = s->giStatus();
    std::printf("   the field: bound %d, %d probes, %d a frame\n", g0.ifdBound ? 1 : 0, g0.ifdProbes,
                g0.ifdProbesPerFrame);
    CHECK(g0.ifdBound && g0.ifdProbes > 0, "the scene's irradiance field is live (not vacuous)");

    const auto run = [&](const char *tag, int frames, bool walk, bool move, bool moveStills = false) {
        std::vector<FrameRecord> all;
        for (int f = 0; f < frames; ++f) {
            if (walk) {
                cam.z -= 1.4f / 60.0f;
                enginetest::testCameraLookAt(view, cam, Vec3(cam.x, 1.0f, cam.z - 8.0f));
            }
            if (move)
                for (int i = 0; i < 2; ++i)
                    enginetest::setNodePosition(s, movers[i], Vec3(-1.5f + 3.0f * float(i) + 0.5f * float((f / 6) % 2),
                                                                   0.5f, 0.02f * float(f)));
            if (moveStills)
                for (int i = 0; i < 2; ++i)
                    enginetest::setNodePosition(s, stills[i], Vec3(-2.5f + 5.0f * float(i) + 0.4f * std::sin(0.05f * float(f)),
                                                                   0.5f, -3.0f));
            e->renderOneFrame();
            std::vector<FrameRecord> recs;
            e->takeFrameRecords(recs);
            all.insert(all.end(), recs.begin(), recs.end());
        }
        for (int f = 0; f < 6; ++f) {
            e->renderOneFrame();
            std::vector<FrameRecord> recs;
            e->takeFrameRecords(recs);
            all.insert(all.end(), recs.begin(), recs.end());
        }
        const FieldWork w = fieldWork(all);
        print(tag, w);
        return w;
    };
    // B FIRST, on the converged field: two movers, the camera still.
    const FieldWork movers2 = run("two movers", 120, false, true);
    CHECK(movers2.framesWithWork == 0u,
          "MOVERS GENERATE NO FIELD: no field work on any frame (movers are not in the voxels the field reads)");
    // A: the camera walks; the field's window follows cascade 0 and scrolls.
    const GiStatus g0w = s->giStatus();
    const FieldWork walk = run("camera walks", 120, true, false);
    const GiStatus g1 = s->giStatus();
    const unsigned long long scrolls = g1.ifdScrolls - g0w.ifdScrolls;
    const unsigned long long bound = (unsigned long long)g1.ifdScrollProbes * g1.ifdTargetSamples * scrolls;
    std::printf("   follows %llu (scrolls %llu, replacements %llu); the last scroll entered %u probes, the "
                "target %u samples; refines owed %u at the end\n",
                g1.ifdFollows - g0w.ifdFollows, scrolls, g1.ifdReplacements - g0w.ifdReplacements,
                g1.ifdScrollProbes, g1.ifdTargetSamples, g1.ifdRefinesOwed);
    const unsigned walkReintegrations = walk.byDetail.count("ifd.converge") ? walk.byDetail.at("ifd.converge") : 0u;
    CHECK(scrolls > 0u && g1.ifdReplacements == g0w.ifdReplacements,
          "the walk scrolls the field's window (not vacuous; no re-placement)");
    CHECK(walkReintegrations == 0u,
          "A WALKING CAMERA RE-INTEGRATES NOTHING: no ifd.converge frame (the light and the geometry did not change)");
    CHECK(walk.probes <= bound,
          ("A SCROLL'S WORK IS WHAT IT TOUCHED: " + std::to_string(walk.probes) +
           " probe integrations <= the entered probes x the target samples (" + std::to_string(bound) +
           ") — never a whole-grid refinement").c_str());
    // C (--stills, a measurement, printed): STILL objects moved every frame — the voxels
    // re-built under them, a real change of the field's input; today's orchestration
    // re-integrates the WHOLE field per re-injection (OgreGi.cpp, stage 4's).
    if (stillsArm) {
        for (int i = 0; i < 300; ++i) e->renderOneFrame();
        { std::vector<FrameRecord> drop; e->takeFrameRecords(drop); }
        run("stills move", 120, false, false, true);
    }
    e->setFrameMonitor(MonitorLevel::Off);
    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
