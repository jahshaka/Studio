// THE SCENE'S CLOCK (ONE-FRAME-1 stage 1, V2 plan 2.1): the pure value type every engine scene keeps
// (irisgl/engine/src/SceneClock.h), driven directly — one binary, one row per proof, CPU only (no engine, no display,
// no card). The integration (the drain, the join, which scenes a frame draws, the particles, the meter) is the card
// fixtures' (frame.clock_integration, frame.meter_time).
//
// THE LAW (Types.h FrameInputScene::stepSerial): a scene's time is its DOCUMENT's steps. A part carries the document
// clock's lifetime serial; the steps it moved since the last part are owed, ONCE; a frame that DRAWS the scene consumes
// them, at most FrameInputScene::kMaxStepsPerFrame (the document's catch-up bound), dropping the rest; a frame that does
// not draw it hands it 0 and leaves its steps.
//
//   --clock-once        frame.clock_once: a serial's steps apply once (an equal serial again 0; a merged hand-over's
//                       newest serial carries both ticks), an empty frame 0, a reassert re-primes, a lower serial
//                       re-primes (never a wrap), the harness steps only a never-stamped scene, for good.
//                       RED ON THE BASE 273b8e638 / irisgl 4576b132: the engine's clock was one persisting frame delay;
//                       an empty hand-over advanced it again by the last delta (+0.016667 s, measured at this row's first
//                       form through Ogre's ControllerManager elapsed time).
//   --two-scenes        frame.two_scenes: two scenes' clocks are independent (an editor at serial 36,000 and a preview at
//                       50 in one hand-over: 1 step and 2 steps); steps a join's drain counted are consumed by the next
//                       drawn frame, once. RED on any single-cursor design (the base's one delay).
//   --particle-scale    frame.particle_scale: the particles' time is the consumed steps x period x particleTimeScale (0.25
//                       and 4, exact to float); the scene's own time is unscaled. RED ON THE BASE: the scale multiplied
//                       the one delay the meter read too.
//   --drawn-only        frame.clock_drawn_only (the stage-1 audit's bug 1): a frame that does not draw a scene keeps its
//                       owed steps (a thumbnail's frame never eats the editor's) and hands it 0; 36,000 owed steps arrive
//                       as 8 (the excess dropped and counted, never replayed). RED ON irisgl bba6409b, by its code:
//                       beginFrameClock consumed every scene's owed steps every frame, drawn or not, unbounded (36,000
//                       owed: a 600-s particle step on restore).
#include <cstdio>
#include <string>

#include "SceneClock.h"

using jahshaka::engine::FrameInputScene;
using jahshaka::engine::detail::SceneClock;

static int gFailures = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++gFailures;
}

static const double kPeriod = double(1.0f / 60.0f);   // the period as it crosses the boundary (a float)

static FrameInputScene part(unsigned long long serial, float particleScale = 1.0f, bool reassert = false)
{
    FrameInputScene p;
    p.stepSerial = serial;
    p.stepPeriod = 1.0f / 60.0f;
    p.particleTimeScale = particleScale;
    p.reassert = reassert;
    return p;
}
static double steps(const SceneClock &c) { return c.dt / c.period; }

static void clockOnce()
{
    SceneClock c;
    c.take(part(10));
    c.beginFrame(true);
    check(c.dt == 0.0, "the first serial primes the cursor: 0 steps (the document's past is not the scene's)");
    c.take(part(13));
    c.beginFrame(true);
    std::printf("      serial 10 -> 13: %.6f steps\n", steps(c));
    check(c.dt == 3.0 * kPeriod, "a serial that moved 3 advances the scene 3 steps, exactly");
    c.beginFrame(true);
    check(c.dt == 0.0, "an EMPTY hand-over (a frame with no part) advances the scene 0");
    c.take(part(13));
    c.beginFrame(true);
    check(c.dt == 0.0, "the same serial again (a second sync of the same steps) advances 0");
    c.take(part(15));   // two host ticks merged into one hand-over: the newest serial (15 over 14) is the one taken
    c.beginFrame(true);
    check(c.dt == 2.0 * kPeriod, "a merged hand-over's newest serial carries both ticks' steps");
    c.take(part(5, 1.0f, true));
    c.beginFrame(true);
    check(c.dt == 0.0, "a reassert re-primes on the incoming serial: 0, never a jump");
    c.take(part(6));
    c.beginFrame(true);
    check(c.dt == kPeriod, "...and the next step advances one step");
    c.take(part(3));
    c.beginFrame(true);
    check(c.dt == 0.0 && c.newest == 3, "a lower serial without a reassert re-primes too (0 steps, never a wrap)");

    SceneClock harness;
    harness.harnessStep(1);
    harness.beginFrame(true);
    check(harness.dt == 1.0 / 60.0, "the harness steps a scene no host ever stamped");
    harness.take(part(100));
    harness.harnessStep(1);
    harness.beginFrame(true);
    check(harness.dt == 0.0 && harness.hostStamped, "a scene a host stamped is never stepped by the harness again");
}

static void twoScenes()
{
    SceneClock editor, preview;
    editor.take(part(36000));
    preview.take(part(50));
    editor.beginFrame(true);
    preview.beginFrame(true);
    editor.take(part(36001));
    preview.take(part(52));
    editor.beginFrame(true);
    preview.beginFrame(true);
    std::printf("      one hand-over: editor %.6f steps, preview %.6f steps\n", steps(editor), steps(preview));
    check(editor.dt == kPeriod, "the editor (serial 36,000 -> 36,001) advances 1 step");
    check(preview.dt == 2.0 * kPeriod, "the preview (serial 50 -> 52) advances 2 steps, in the same hand-over");
    preview.take(part(53));   // a join's drain between frames counts it
    preview.beginFrame(true);
    check(preview.dt == kPeriod, "steps a join's drain counted are consumed by the next frame");
    preview.beginFrame(true);
    check(preview.dt == 0.0, "...once: the frame after advances 0");
}

static void particleScale()
{
    SceneClock c;
    c.take(part(0));
    c.beginFrame(true);
    unsigned long long serial = 0;
    for (float scale : { 0.25f, 4.0f }) {
        serial += 3;
        c.take(part(serial, scale));
        c.beginFrame(true);
        const float want = float(3.0 * kPeriod * double(scale));
        std::printf("      scale %.2f, 3 steps: scene %.6f s, particles %.6f s (law %.6f)\n", double(scale), c.dt,
                    c.particleDt, double(want));
        check(float(c.particleDt) == want, "the particles advance steps x period x particleTimeScale, exact to float");
        check(c.dt == 3.0 * kPeriod, "the scene's own time is steps x period, unscaled");
        c.beginFrame(true);
        check(c.particleDt == 0.0, "an empty hand-over hands the particles 0");
    }
}

static void drawnOnly()
{
    SceneClock editor;
    editor.take(part(1000));
    editor.beginFrame(true);
    editor.take(part(1003));
    editor.beginFrame(false);   // a thumbnail's / a preview's / a Player shot's frame: the editor is not drawn
    std::printf("      an undrawn frame: dt %.6f, owed %llu\n", editor.dt, editor.owed);
    check(editor.dt == 0.0 && editor.particleDt == 0.0, "a frame that does not draw the scene hands it 0");
    check(editor.owed == 3, "...and leaves its 3 owed steps");
    editor.beginFrame(true);
    check(editor.dt == 3.0 * kPeriod, "the next frame that draws it consumes them");
    // ten minutes minimised: the 100-ms ticks stepped the document 36,000 times
    editor.take(part(1003 + 36000));
    const double before = editor.sceneSeconds();
    editor.beginFrame(true);
    std::printf("      36,000 owed steps: the frame takes %.6f steps, %llu dropped\n", steps(editor), editor.dropped);
    check(editor.dt == double(SceneClock::kMaxStepsPerFrame) * kPeriod,
          "36,000 owed steps arrive as the bound's 8 (the document's own catch-up)");
    check(editor.dropped == 36000 - SceneClock::kMaxStepsPerFrame, "...the excess dropped and counted, never replayed");
    check(editor.sceneSeconds() - before == editor.dt, "...and the scene's time moves by what it showed");
    editor.beginFrame(true);
    check(editor.dt == 0.0, "the frame after: nothing owed");
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    const char *row = mode == "--clock-once" ? "frame.clock_once"
                    : mode == "--two-scenes" ? "frame.two_scenes"
                    : mode == "--particle-scale" ? "frame.particle_scale"
                    : mode == "--drawn-only" ? "frame.clock_drawn_only" : nullptr;
    if (!row) {
        std::printf("usage: test_frame_clock --clock-once | --two-scenes | --particle-scale | --drawn-only\n");
        return 2;
    }
    if (mode == "--clock-once") clockOnce();
    else if (mode == "--two-scenes") twoScenes();
    else if (mode == "--particle-scale") particleScale();
    else drawnOnly();
    std::printf("%s: %s\n", row, gFailures == 0 ? "all checks passed" : "FAILURES");
    return gFailures == 0 ? 0 : 1;
}
