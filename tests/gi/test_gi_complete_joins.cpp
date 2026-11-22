// gi.complete_joins — A COMPLETE FRAME's COMPUTE IS A FUNCTION OF THE FRAMES, NEVER OF THE SERVICE's WALL TIME
// (SHADER-SYSTEM-1 7a, R2 (c)). THE ONE ASYNC-SHADER RULE: a compile a scene's frames need may land a frame later only on a
// frame that does not owe the whole picture; a COMPLETE frame (the editor's outside a load, a screenshot's) waits for it.
// Six engine sites put compute off in an asynchronous scene on ANY pace - the GI's deferral (PhotonComponent::
// deferDispatches), the surface cache's relight and accumulation (requestAsync), the voxel commit (commitLightReady), the
// screen integrate (ScreenGiListener::dispatchRuns), the atmosphere and the convolution - and the fork's HlmsCompute::
// dispatch put off every dispatch of an asynchronous workspace (Atom's culls): the editor's Complete frames drew what the
// background compiler had got to. Now the pace reaches the fork in ONE place (HlmsCompute::_jahSetFrameOwesPicture at the
// frame head): on a frame that owes its picture requestAsync answers ready and dispatch() JOINS the request in flight
// (AsyncShaderStats::computeJoins) - every site inherits it (source.compute_pace_one_place).
// THE SCRIPT: a floor, a box, a shadowing sun, an atmosphere sky and the voxel GI at High with bounces, in a 64 x 64
// offscreen Live view on the rig's shared cache, every frame on the default COMPLETE pace, kFrames frames, twice in one
// process: R, the REFERENCE - a SYNCHRONOUS view (nothing is ever put off: every frame's picture is the one it owes);
// S, the SLOW SERVICE - an ASYNCHRONOUS view with every background compile held kServiceSlowMs before it runs (the test
// door shaders.serviceSlow: a loaded box or a cold driver, made on demand).
// THE SITES THE SCENE REACHES (the red run's deferred jobs, spikes/shader-system/ss7/r2c/red-LastTest.log): the GI's arm
// and record (VCT/*, Photon/*), Atom's culls in the workspace (Jahshaka/Cull*, HzbBuild), the screen integrate
// (Jahshaka/ScreenIntegrate), the convolution (its clones: joins), the atmosphere's tables. NOT REACHED: the voxel
// commit (a cascade scroll: the camera never moves) and the surface cache's relight (the red run's log names no relight
// job) - listed, not assumed; both ask the same fork function (requestAsync).
// THE LAWS: (1) S's last frame is R's last frame, exact display codes. RED before R2 (c): 2478 pixels differ (S's frames
// put 700 dispatches off). (2) S's frames put no compute dispatch off: the published computeDeferred did not move and no
// frame read term 64 (a compute dispatch deferred). RED before: 700, 7 frames.
// CONTROL (the zero-pass rule for (1)): S's frames joined at least one in-flight request (computeJoins > 0) - the door
// did make the service late, so (1) is not a run where everything had landed anyway (green: 112 joins).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include <cstdio>
#include <string>
using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

/// The frames each run draws: the arm's build is on frame 1 (a Complete frame builds it whole), its record every frame.
static constexpr int kFrames = 8;
/// The door's hold on each background compile (ms): longer than a frame, so a deferral would be seen.
static constexpr double kServiceSlowMs = 100.0;
/// Term 64 of PublishedFrame::Completeness: a compute dispatch deferred (Types.h).
static constexpr unsigned kTermDeferred = 64u;

struct Run { Image picture; bool ok = false; unsigned long long joins = 0, deferred = 0; int termFrames = 0; };

static Run runOnce(bool slowAsync)
{
    Run r;
    // two engines in one process, one after the other: the real destroy between them (the SMAA picture row's way)
    EngineConfig cfg = enginetest::testEngineConfig(slowAsync ? "test-gi-complete-joins-s-ogre.log"
                                                              : "test-gi-complete-joins-r-ogre.log",
                                                    enginetest::EngineOverrides().realDestroy());
    std::string err;
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine (%s)\n", err.c_str()); return r; }
    Engine *e = engine.get();
    View *view = e->createOffscreenView("joins", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *scene = e->createScene("joins");
    if (!view || !scene) { std::printf("FAIL: view/scene (%s)\n", e->lastError().c_str()); return r; }
    view->setAsyncShaders(slowAsync);
    view->setScene(scene);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 5.0f), Vec3(0.0f, 0.5f, 0.0f));
    const NodeId floor = enginetest::addTestCube(scene, Colour(0.6f, 0.6f, 0.6f), 0.0f, 0.9f);
    const NodeId box = enginetest::addTestCube(scene, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.6f);
    bool ok = floor && box;
    if (ok) {
        enginetest::setNodePosition(scene, floor, Vec3(0.0f, -0.1f, 0.0f));
        enginetest::setNodeScale(scene, floor, Vec3(8.0f, 0.2f, 8.0f));
        enginetest::setNodePosition(scene, box, Vec3(0.0f, 0.5f, 0.0f));
    }
    ok = ok && enginetest::addDirectionalLight(scene, Vec3(0.3f, -1.0f, 0.2f), 2.0f) != 0;
    SkyDesc sky;
    sky.mode = SkyMode::Atmosphere;   // the atmosphere's tables and the convolution: their first pipelines
    ok = ok && scene->setSky(sky);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;
    ok = ok && scene->setGlobalIllumination(gi);
    if (!ok) { std::printf("FAIL: the scene (%s)\n", e->lastError().c_str()); return r; }
    // THE SITES THIS SCENE REACHES (declared, asserted on frame 1): the screen integrate (a Live offscreen view), the
    // cards' relight and card-to-card accumulation (the surface cache), the store; the voxel GI's arm and record.
    enginetest::declareRoutes(view, scene, enginetest::Routes().integrate().cards().store(),
                              slowAsync ? "gi.complete_joins S's view" : "gi.complete_joins R's view");
    if (slowAsync) e->setArm("shaders.serviceSlow", kServiceSlowMs);
    const AsyncShaderStats before = e->published().shaders;
    for (int f = 0; f < kFrames; ++f) {
        enginetest::frames(e, 1);   // the default pace: Complete
        if (scene->published().completeness.terms & kTermDeferred) ++r.termFrames;
    }
    r.ok = view->readPixels(r.picture) && !r.picture.rgba.empty();
    r.joins = e->published().shaders.computeJoins - before.computeJoins;
    r.deferred = e->published().shaders.computeDeferred - before.computeDeferred;
    if (slowAsync) e->setArm("shaders.serviceSlow", 0.0);
    enginetest::poseRegistry().erase(scene);
    engine.reset();
    return r;
}

int main()
{
    std::printf("== gi.complete_joins: a Complete frame's lighting arm is a function of the frames, never of the service\n");
    const Run ref = runOnce(false);
    const Run slow = runOnce(true);
    CHECK(ref.ok && slow.ok, "both runs drew and read their last frame");
    if (!ref.ok || !slow.ok) return 1;
    int differ = 0;
    for (size_t i = 0; i + 3 < ref.picture.rgba.size() && i + 3 < slow.picture.rgba.size(); i += 4)
        if (ref.picture.rgba[i] != slow.picture.rgba[i] || ref.picture.rgba[i + 1] != slow.picture.rgba[i + 1] ||
            ref.picture.rgba[i + 2] != slow.picture.rgba[i + 2])
            ++differ;
    std::printf("    S (asynchronous, the service held %.0f ms a job): %llu join(s), %llu compute dispatch(es) deferred, %d frame(s) "
                "read term 64; %d pixel(s) differ from R at frame %d\n", kServiceSlowMs, slow.joins, slow.deferred,
                slow.termFrames, differ, kFrames);
    CHECK(slow.joins > 0ull, "CONTROL: S's Complete frames joined %llu in-flight request(s) (above 0: the service was "
                             "late)", slow.joins);
    CHECK(slow.deferred == 0ull && slow.termFrames == 0, "(2) S's Complete frames deferred no compute dispatch (%llu) "
                                                         "and none read term 64 (%d)", slow.deferred, slow.termFrames);
    CHECK(differ == 0, "(1) S's frame %d is R's, exact codes (%d pixels differ)", kFrames, differ);
    if (failures) std::printf("gi.complete_joins: FAIL (%d)\n", failures);
    else std::printf("gi.complete_joins: PASS\n");
    return failures ? 1 : 0;
}
