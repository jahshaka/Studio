// frame.stillness_monitor — A STILLNESS RUN LEAVES THE FRAME MONITOR AT THE LEVEL IT FOUND, IN THE APP's SHAPE (the render
// thread, the host's calls as records - src/bridge/enginehost.cpp), and its level is ONE TRUTH: Engine::frameMonitor()
// (the host's own record) equals the level the engine applied, before, during and after a run (LAB-SCRIPTS-1's feature
// end, the lead's ruling (b): Stillness switches the monitor AS A HOST DOES - setFrameMonitor, then an empty request).
// THE LAWS, on an offscreen view of an empty scene, the run's frames lockstep:
//   (1) the monitor OFF before a run: during it the host's record and the applied level are both Review; after it
//       both are Off - the run switched off what it switched on (red before the fix: switched from inside a request,
//       the host's record stayed Off, the restore compared Off with Review and left the engine recording);
//   (2) a SECOND run in the same session finishes too, the same before/after (the first fix's case: its records came);
//   (3) a HOST's capture running before a run (Review, the host's): after it, still Review - the run leaves it.
// CONTROL: each run's report holds its window (the run drew and read frames: a run that did nothing passes (1)).
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Stillness.h"
#include "../support/enginetesthelpers.h"
#include <cstdio>
#include <string>
using namespace jahshaka::engine;
static int failures = 0;
static int framesDrawn = 0;   // every frame this fixture draws (its row's FRAMES)
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

namespace {
const char *name(MonitorLevel l) { return l == MonitorLevel::Off ? "off" : "review"; }
// THE APPLIED LEVEL NOW: an empty request applies what the host sent and republishes (OgreEngine::request, U3 4b)
MonitorLevel applied(Engine *e)
{
    e->request([](Engine &) {});
    return e->published().monitor.level;
}
// ONE RUN: begin, its frames (the pose is not placed: an empty scene), the level read DURING it, finish
bool run(Engine *e, Scene *scene, View *view, const char *label, MonitorLevel &recordDuring, MonitorLevel &appliedDuring)
{
    StillnessSpec spec;
    spec.script = "teleport:0,2,3,0,0,0";
    spec.frames = 4;
    StillnessRun r(*e, *scene, *view);
    std::string err;
    if (!r.begin(spec, err)) { std::printf("FAIL: %s: begin refused: %s\n", label, err.c_str()); ++failures; return false; }
    Vec3 eye, target;
    bool first = true;
    while (r.nextPose(eye, target)) {
        enginetest::frames(e, 1);
        ++framesDrawn;
        if (first) { recordDuring = e->frameMonitor(); appliedDuring = applied(e); first = false; }
        if (!r.frameDone(err)) { std::printf("FAIL: %s: frameDone: %s\n", label, err.c_str()); ++failures; return false; }
    }
    StillnessReport rep;
    if (!r.finish(rep, err)) { std::printf("FAIL: %s: finish: %s\n", label, err.c_str()); ++failures; return false; }
    CHECK(!rep.windows.empty(), "CONTROL %s: the run's report holds its window (%zu)", label, rep.windows.size());
    return true;
}
}   // namespace

int main()
{
    std::printf("== frame.stillness_monitor: a stillness run leaves the frame monitor as it found it (render thread, host records)\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-stillness-monitor-ogre.log");
    cfg.renderThread = true;
    cfg.hostRecords = true;
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created on its render thread with host records (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    View *view = e->createOffscreenView("still", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *scene = e->createScene("still");
    if (!view || !scene) { std::printf("FAIL: view/scene\nframe.stillness_monitor: FAILED (1)\n"); return 1; }
    view->setScene(scene);
    enginetest::frames(e, 3);
    framesDrawn += 3;

    for (int i = 1; i <= 2; ++i) {   // (1) and (2): the monitor Off before, twice in one session
        const std::string label = "run " + std::to_string(i);
        CHECK(e->frameMonitor() == MonitorLevel::Off && applied(e) == MonitorLevel::Off, "%s: Off before", label.c_str());
        MonitorLevel rec = MonitorLevel::Off, app = MonitorLevel::Off;
        if (!run(e, scene, view, label.c_str(), rec, app)) continue;
        CHECK(rec == MonitorLevel::Review && app == MonitorLevel::Review,
              "%s: during it the host's record (%s) and the applied level (%s) are both review", label.c_str(), name(rec), name(app));
        const MonitorLevel after = applied(e);
        CHECK(e->frameMonitor() == MonitorLevel::Off && after == MonitorLevel::Off,
              "%s: after it both are off - the host's record %s, the applied level %s", label.c_str(), name(e->frameMonitor()),
              name(after));
    }
    // (3) THE HOST's capture before the run: left as it was
    e->setFrameMonitor(MonitorLevel::Review);
    CHECK(applied(e) == MonitorLevel::Review, "the host's capture: review before the run");
    MonitorLevel rec = MonitorLevel::Off, app = MonitorLevel::Off;
    if (run(e, scene, view, "beside the host's capture", rec, app)) {
        const MonitorLevel after = applied(e);
        CHECK(e->frameMonitor() == MonitorLevel::Review && after == MonitorLevel::Review,
              "beside the host's capture: after the run still review - the host's record %s, the applied level %s",
              name(e->frameMonitor()), name(after));
    }
    e->setFrameMonitor(MonitorLevel::Off);
    e->request([](Engine &) {});
    std::vector<MonitorRecord> drop;
    e->popMonitorRecords(drop);
    std::printf("frames drawn: %d\n", framesDrawn);
    std::printf("frame.stillness_monitor: %s\n", failures ? ("FAILED (" + std::to_string(failures) + ")").c_str() : "PASSED");
    return failures ? 1 : 0;
}
