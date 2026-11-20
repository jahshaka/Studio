// THE SCENE'S CLOCK (ONE-FRAME-1 stage 1, V2 plan 2.1): one binary, one row per proof, on the NULL render system (no
// display, no card: Root::renderOneFrame walks a workspace-less render system, and the scenes' clocks still run).
//
// THE LAW (Types.h FrameInputScene::stepSerial): a scene's time is its DOCUMENT's steps. Its part carries the document
// clock's lifetime serial; a frame advances the scene by (serial - the last serial it saw) x stepPeriod — ONCE, and 0
// on a frame nobody stepped it. Documents are plural, so the cursor lives WITH THE SCENE.
//
//   --clock-once      frame.clock_once: a serial's steps apply once (a second sync of the same steps 0, two syncs of
//                     one frame merged count both), an empty hand-over advances 0, `reassert` re-primes (a new document
//                     on the scene: 0, never a jump), and the harness steps only the scenes no host ever stamped (a
//                     scene a host stamps later leaves its set for good).
//                     RED ON THE BASE 273b8e638 / irisgl 4576b132: the engine's clock was ONE persisting frame delay,
//                     and an empty hand-over advanced it again by the last record's delta (+0.016667 s, measured by
//                     this row's first form through Ogre's ControllerManager elapsed time).
//   --two-scenes      frame.two_scenes: two scenes with different serials in ONE hand-over each advance by their own
//                     steps (an editor at 36,000 and a preview at 50: neither reads the other's 35,950); steps a join's
//                     drain counted (a request between frames) are consumed by the next frame, once.
//                     RED on any single-cursor design (the base's one frame delay: both scenes advanced by the newest
//                     record's delta).
//   --particle-scale  frame.particle_scale: the time handed to a scene's particles (the fork's ParticleSystemManager2::
//                     setTimeSinceLast) is its steps x period x particleTimeScale, at 0.25 and at 4, exact to float;
//                     0 on an empty hand-over; and the scene's own time (what its meter, looks and clouds read) is
//                     steps x period whatever the scale. RED ON THE BASE: the scale multiplied the ONE frame delay
//                     that the meter read too (enginesceneviewport.cpp: simulated x particleTimeScale).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/EngineInspect.h"
#include "enginetesthelpers.h"

using namespace jahshaka::engine;

static int gFailures = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++gFailures;
}

static const double kPeriod = double(1.0f / 60.0f);   // the period as it crosses the boundary (a float)

/// One scene part stamped with its document's serial (what the mirror's emit writes).
static FrameInput stamp(Scene *s, unsigned long long serial, float particleScale = 1.0f, bool reassert = false)
{
    FrameInput in;
    FrameInputScene part;
    part.scene = s;
    part.stepSerial = serial;
    part.stepPeriod = 1.0f / 60.0f;
    part.particleTimeScale = particleScale;
    part.reassert = reassert;
    in.scenes.push_back(std::move(part));
    return in;
}
static double dt(Scene *s) { return inspect::scene(s)->frameSceneSeconds(); }
static double steps(Scene *s) { return dt(s) / kPeriod; }

static int clockOnce(Engine &e)
{
    Scene *a = e.createScene("stamped");
    Scene *b = e.createScene("harness");
    check(a && b, "two headless scenes");
    if (!a || !b) return 1;

    e.submitFrame(stamp(a, 10));
    enginetest::frozenFrames(&e, 1);
    check(dt(a) == 0.0, "the first serial primes the cursor: 0 steps (the document's past is not the scene's)");
    e.submitFrame(stamp(a, 13));
    enginetest::frozenFrames(&e, 1);
    std::printf("      serial 10 -> 13: %.6f steps\n", steps(a));
    check(dt(a) == 3.0 * kPeriod, "a serial that moved 3 advances the scene 3 steps, exactly");
    enginetest::frozenFrames(&e, 1);
    std::printf("      an empty hand-over: %.6f s\n", dt(a));
    check(dt(a) == 0.0, "an EMPTY hand-over advances the scene 0");
    e.submitFrame(stamp(a, 13));
    enginetest::frozenFrames(&e, 1);
    check(dt(a) == 0.0, "the same serial again (a second sync of the same steps) advances 0");
    e.submitFrame(stamp(a, 14));
    e.submitFrame(stamp(a, 15));   // two host ticks merged into one hand-over: the newest serial is the right one
    enginetest::frozenFrames(&e, 1);
    std::printf("      two syncs (14, 15) in one hand-over: %.6f steps\n", steps(a));
    check(dt(a) == 2.0 * kPeriod, "two ticks merged into one hand-over carry both steps");
    e.submitFrame(stamp(a, 5, 1.0f, true));   // a new document on the scene (the mirror's setSource)
    enginetest::frozenFrames(&e, 1);
    check(dt(a) == 0.0, "a reassert re-primes on the incoming document's serial: 0, never a jump");
    e.submitFrame(stamp(a, 6));
    enginetest::frozenFrames(&e, 1);
    check(dt(a) == kPeriod, "...and its next step advances one step");
    e.submitFrame(stamp(a, 3));   // a lower serial with no reassert: another clock stamped here (a host defect)
    enginetest::frozenFrames(&e, 1);
    check(dt(a) == 0.0, "a lower serial without a reassert re-primes too (0 steps, never a wrap)");

    // THE HARNESS's GRID: only the scenes no host ever stamped
    enginetest::frames(&e, 1);
    std::printf("      one harness frame: stamped %.6f steps, harness %.6f steps\n", steps(a), steps(b));
    // (b was never stamped, so its period is the grid's own 1/60 in double, not the float a part carries)
    check(dt(a) == 0.0 && dt(b) == 1.0 / 60.0, "the harness steps only the scene no host ever stamped");
    e.submitFrame(stamp(b, 100));   // a host stamps the harness's scene: it leaves the harness's set for good
    enginetest::frames(&e, 1);
    enginetest::frames(&e, 1);
    check(dt(b) == 0.0, "a scene a host stamped later is never stepped by the harness again (one source)");
    return 0;
}

static int twoScenes(Engine &e)
{
    Scene *editor = e.createScene("editor");
    Scene *preview = e.createScene("preview");
    check(editor && preview, "two headless scenes");
    if (!editor || !preview) return 1;
    e.submitFrame(stamp(editor, 36000));
    e.submitFrame(stamp(preview, 50));
    enginetest::frozenFrames(&e, 1);
    // ONE hand-over, two scenes' parts
    e.submitFrame(stamp(editor, 36001));
    e.submitFrame(stamp(preview, 52));
    enginetest::frozenFrames(&e, 1);
    std::printf("      one hand-over: editor %.6f steps, preview %.6f steps\n", steps(editor), steps(preview));
    check(dt(editor) == kPeriod, "the editor (serial 36,000 -> 36,001) advances 1 step");
    check(dt(preview) == 2.0 * kPeriod, "the preview (serial 50 -> 52) advances 2 steps, in the same hand-over");
    // A JOIN's DRAIN between frames counts the steps; the next frame consumes them, once
    e.submitFrame(stamp(preview, 53));
    e.request([](Engine &) {});   // a join: the pending record's scene parts land now (applyPendingEngineMessages)
    enginetest::frozenFrames(&e, 1);
    std::printf("      a step counted by a join's drain: the next frame %.6f steps\n", steps(preview));
    check(dt(preview) == kPeriod, "steps a join's drain counted are consumed by the next frame");
    check(dt(editor) == 0.0, "...and the other scene, not stepped, advances 0");
    enginetest::frozenFrames(&e, 1);
    check(dt(preview) == 0.0, "...once: the frame after advances 0");
    return 0;
}

static int particleScale(Engine &e)
{
    Scene *s = e.createScene("particles");
    check(s != nullptr, "a headless scene");
    if (!s) return 1;
    e.submitFrame(stamp(s, 0));
    enginetest::frozenFrames(&e, 1);
    for (float scale : { 0.25f, 4.0f }) {
        const unsigned k = 3;
        static unsigned long long serial = 0;
        serial += k;
        e.submitFrame(stamp(s, serial, scale));
        enginetest::frozenFrames(&e, 1);
        const double scene = inspect::scene(s)->frameSceneSeconds();
        const double particles = inspect::scene(s)->particleSeconds();
        const float want = float(double(k) * kPeriod * double(scale));
        std::printf("      scale %.2f, %u steps: scene %.6f s, particles %.6f s (law %.6f)\n", double(scale), k, scene,
                    particles, double(want));
        check(float(particles) == want, "the particles advance steps x period x particleTimeScale, exact to float");
        check(scene == double(k) * kPeriod, "the scene's own time (its meter, looks, clouds) is steps x period, unscaled");
        enginetest::frozenFrames(&e, 1);
        check(inspect::scene(s)->particleSeconds() == 0.0, "an empty hand-over hands the particles 0");
    }
    return 0;
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode != "--clock-once" && mode != "--two-scenes" && mode != "--particle-scale") {
        std::printf("usage: test_frame_clock --clock-once | --two-scenes | --particle-scale\n");
        return 2;
    }
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-frame-clock-ogre.log",
                                                              enginetest::EngineOverrides().headless()), err);
    check(engine != nullptr, "a headless engine");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    const int rc = mode == "--clock-once" ? clockOnce(*engine)
                 : mode == "--two-scenes" ? twoScenes(*engine) : particleScale(*engine);
    const char *row = mode == "--clock-once" ? "frame.clock_once"
                    : mode == "--two-scenes" ? "frame.two_scenes" : "frame.particle_scale";
    std::printf("%s: %s\n", row, (gFailures == 0 && rc == 0) ? "all checks passed" : "FAILURES");
    return (gFailures == 0 && rc == 0) ? 0 : 1;
}
