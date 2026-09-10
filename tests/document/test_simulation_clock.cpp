// document.simulation_clock — iris::SimulationClock, the ONE fixed-step clock
// (ENGINEERING_DEBT_SPEC A4.2). Pure arithmetic: no engine, no display, no Qt
// event loop. The properties asserted here are exactly the ones the scripted
// determinism suite (scripting.e2e.fixed_clock) relies on to compare document
// state across frame rates, so a regression shows up here first and cheaply.

#include "irisgl/document/scenegraph/simulationclock.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

using iris::SimulationClock;

/// Hands the clock `frames` frames of `dt` (as a FLOAT, the way every host
/// hands it: QElapsedTimer seconds or editor.frame's float(dt)).
static int run(SimulationClock &c, int frames, float dt)
{
    int steps = 0;
    for (int i = 0; i < frames; ++i) steps += c.advance(double(dt));
    return steps;
}

static bool sameBits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

int main()
{
    std::printf("SimulationClock: %d Hz, step %.17g s, catch-up %d steps (%.6f s)\n",
                SimulationClock::kStepHz, SimulationClock::kStepSeconds,
                SimulationClock::kMaxStepsPerAdvance, SimulationClock::kMaxAdvanceSeconds);
    CHECK(SimulationClock::kStepHz == 60, "the grid is 60 Hz (Bullet's default fixed step; every gate's dt)");

    // ---- 1. The grid: whole steps, exact time ---------------------------------
    {
        SimulationClock c;
        CHECK(c.advance(1.0f / 60.0f) == 1, "a 1/60 s frame buys exactly one step");
        CHECK(c.frameSteps() == 1 && std::fabs(c.frameSeconds() - 1.0 / 60.0) < 1e-12,
              "frameSteps / frameSeconds report what the last frame bought");
        CHECK(c.alpha() < 1e-6, "…and carries (almost) nothing: the float 1/60 is 1e-9 above the grid");
        CHECK(c.advance(1.0f / 30.0f) == 2, "a 1/30 s frame buys exactly two steps");
        CHECK(c.advance(0.0f) == 0 && c.frameSteps() == 0 && c.frameSeconds() == 0.0,
              "a zero frame buys nothing");
        CHECK(c.advance(-5.0f) == 0, "a negative dt counts as zero");
        CHECK(c.steps() == 3 && sameBits(c.time(), 3.0 * SimulationClock::kStepSeconds),
              "time() is steps x the grid, bit-exactly, never a float running sum");
    }

    // ---- 2. Rate independence: same seconds, same steps, same time bits ------
    // 2 s delivered as 120 x 1/60, 60 x 1/30, 240 x 1/120, 288 x 1/144, and as
    // 33 ms + 17 ms alternation (a Debug session), all end on step 120 with
    // bit-identical time(). This is the property that makes a physics drop
    // reproducible across frame rates.
    {
        SimulationClock a, b, d, e;
        const int sa = run(a, 120, 1.0f / 60.0f);
        const int sb = run(b, 60, 1.0f / 30.0f);
        const int sd = run(d, 240, 1.0f / 120.0f);
        const int se = run(e, 288, 1.0f / 144.0f);
        std::printf("    2 s as 60/30/120/144 Hz frames: %d / %d / %d / %d steps\n", sa, sb, sd, se);
        CHECK(sa == 120 && sb == 120 && sd == 120 && se == 120,
              "2 s buys 120 steps at 60, 30, 120 and 144 Hz frame delivery alike");
        CHECK(sameBits(a.time(), b.time()) && sameBits(a.time(), d.time()) && sameBits(a.time(), e.time()),
              "…and the four clocks read the same time, bit for bit");
        CHECK(a.alpha() < 1e-5 && b.alpha() < 1e-5 && d.alpha() < 1e-5 && e.alpha() < 1e-5,
              "…with only rounding noise carried (no phantom step is brewing)");

        // 144 Hz alternates 0 and 1 steps per frame — the "no interpolation"
        // policy's visible cost, stated as a fact rather than hidden.
        SimulationClock f;
        int zeros = 0, ones = 0, other = 0;
        for (int i = 0; i < 288; ++i) {
            const int s = f.advance(1.0 / 144.0);
            if (s == 0) ++zeros; else if (s == 1) ++ones; else ++other;
        }
        std::printf("    144 Hz frames: %d bought 0 steps, %d bought 1, %d other\n", zeros, ones, other);
        CHECK(ones == 120 && zeros == 168 && other == 0,
              "a 144 Hz panel alternates zero- and one-step frames (12 frames per 5 steps)");

        // A 30 fps Debug session's integer-ish wall: 33/34 ms alternation.
        SimulationClock g;
        int sg = 0;
        for (int i = 0; i < 60; ++i) sg += g.advance(i % 2 ? 0.034 : 0.033);   // 2.01 s
        CHECK(sg == 120, "60 frames of 33/34 ms (2.01 s) buy 120 steps, the 10 ms left carried");
        CHECK(std::fabs(g.alpha() - 0.6) < 1e-6, "…and alpha reports the carried 10 ms as 0.6 of a step");
    }

    // ---- 3. Long-run stability: no drift, no phantom step -----------------
    {
        SimulationClock c;
        const int steps = run(c, 216000, 1.0f / 60.0f);   // an hour at 60 fps
        CHECK(steps == 216000, "an hour of float 1/60 frames buys exactly 216000 steps (no float drift)");
        // The float 1/60 is 8.7e-10 s above the double grid; an hour of them
        // carries 1.9e-4 s = 1.1% of a step. A phantom step would take ~90 h.
        std::printf("    after an hour the accumulator carries %.3f of a step\n", c.alpha());
        CHECK(c.alpha() < 0.05, "…and the carried float rounding (1.1%% of a step per hour) is far below a step");
        SimulationClock h;
        const int steps30 = run(h, 108000, 1.0f / 30.0f);
        CHECK(steps30 == 216000 && sameBits(c.time(), h.time()),
              "the same hour at 30 fps lands on the same step and the same time bits");
    }

    // ---- 4. The catch-up bound: a stall runs 8 steps and DROPS the rest ------
    {
        SimulationClock c;
        CHECK(c.advance(1.0) == SimulationClock::kMaxStepsPerAdvance,
              "a 1 s stall buys the bound (8 steps), not 60");
        CHECK(c.alpha() == 0.0, "…and the rest is dropped, not carried into the next frame");
        CHECK(c.advance(1.0f / 60.0f) == 1, "the frame after a stall is an ordinary frame");
        // The SCRIPTED bound sits one step UNDER the catch-up bound (A4.2 review
        // S2): the drop is on the accumulator, so a dt at the full bound plus a
        // carried fraction would silently lose the fraction.
        CHECK(std::fabs(SimulationClock::kMaxAdvanceSeconds - 7.0 / 60.0) < 1e-12,
              "kMaxAdvanceSeconds is 7 steps (what the verbs refuse above)");
        // Exactly the scripted bound is not a stall.
        SimulationClock d;
        CHECK(d.advance(SimulationClock::kMaxAdvanceSeconds) == 7 && d.alpha() == 0.0,
              "a frame of exactly the scripted bound buys 7 steps cleanly");
        // …and with a carried half step it buys 7 and KEEPS the carry — nothing
        // a scripted frame under the bound asks for is ever dropped.
        SimulationClock e;
        CHECK(e.advance(0.5 / 60.0) == 0 && e.alpha() > 0.49 && e.alpha() < 0.51,
              "half a step is carried");
        CHECK(e.advance(SimulationClock::kMaxAdvanceSeconds) == 7 && e.alpha() > 0.49 && e.alpha() < 0.51,
              "the scripted bound on top of a carried half step buys 7 and keeps the half (no drop)");
        CHECK(e.advance(0.5 / 60.0) == 1 && e.alpha() < 1e-6,
              "the carried half plus another half is exactly the 8th step");
    }

    // ---- 5. reset ---------------------------------------------------------------
    {
        SimulationClock c;
        run(c, 10, 1.0f / 60.0f);
        c.advance(0.01);
        c.reset();
        CHECK(c.steps() == 0 && c.time() == 0.0 && c.alpha() == 0.0 && c.frameSteps() == 0,
              "reset returns to t = 0 with nothing carried");
    }

    std::printf(failures ? "document.simulation_clock: %d FAILED\n" : "document.simulation_clock: all ok\n", failures);
    return failures ? 1 : 0;
}
