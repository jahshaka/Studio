// app.frame_step — A LOCKSTEP FRAME IS ONE GRID STEP (TEST-STREAMLINE-1 red (b); src/viewport/framestep.h). No Qt, no
// engine: the rule every lockstep door calls. LAW: n lockstep frames advance the document clock by exactly n steps
// whatever the frames' wall time; a scripted dt is >= 0 and within the per-frame bound; the timeline preview's wall time
// moves its cursor by whole steps only.
#include <cmath>
#include <cstdio>
#include "viewport/framestep.h"

using iris::SimulationClock;
static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

int main()
{
    // 1. editor.frame(37) during play: 37 steps, t = 37/60 exactly, whatever each frame's wall time was
    const double walls[] = { 0.0095, 0.027, 0.0012, 0.25 };   // a fast host frame, a Debug app frame, a tiny one, a stall
    bool exact = true;
    for (double wall : walls) {
        SimulationClock c;
        for (int i = 0; i < 37; ++i) { (void)wall; c.advance(framestep::lockstepSeconds(-1.0)); }
        exact = exact && c.steps() == 37u && c.time() == 37.0 * SimulationClock::kStepSeconds;
    }
    check(exact, "37 lockstep frames with no stated dt: 37 steps, t = 37/60, at every wall pace (9.5 ms, 27 ms, 1.2 ms, 250 ms)");
    // NEGATIVE CONTROL: the base's rule (the wall time each frame took) reads the box's pace
    SimulationClock fast, slow;
    for (int i = 0; i < 37; ++i) { fast.advance(0.0095); slow.advance(0.027); }
    check(fast.steps() != slow.steps() && fast.steps() != 37u,
          "negative control: the wall rule buys another count on a 9.5 ms and a 27 ms frame (paused_shot's 21 vs 57 steps)");
    // 2. a stated dt is honoured, in whole steps
    SimulationClock s;
    for (int i = 0; i < 60; ++i) s.advance(framestep::lockstepSeconds(1.0 / 30.0));
    check(s.steps() == 120u, "60 frames at a stated 1/30 s: 120 steps");
    // 3. the verbs' dt rule: no negative value (the deleted wall option), nothing above the per-frame bound
    check(!framestep::scriptedDtValid(-1.0) && framestep::scriptedDtValid(0.0) &&
              framestep::scriptedDtValid(SimulationClock::kStepSeconds) &&
              framestep::scriptedDtValid(SimulationClock::kMaxAdvanceSeconds) &&
              !framestep::scriptedDtValid(SimulationClock::kMaxAdvanceSeconds + 0.01),
          "a scripted dt: -1 refused, 0 / 1/60 / the bound accepted, past the bound refused");
    // 4. the timeline preview: wall ticks of any length move its cursor by WHOLE steps only
    SimulationClock preview;
    double cursor = 0.0;
    bool whole = true;
    const double ticks[] = { 0.0071, 0.0163, 0.0094, 0.0333, 0.0021, 0.0167, 0.051 };
    for (double t : ticks) {
        const int k = preview.advance(t);
        cursor += double(k) * SimulationClock::kStepSeconds;
        const double inSteps = cursor / SimulationClock::kStepSeconds;
        whole = whole && std::fabs(inSteps - std::round(inSteps)) < 1e-9;
    }
    check(whole && preview.steps() == 8u, "the preview's seven wall ticks (136.2 ms): the cursor at 8 whole steps, never between");
    std::printf("%s\n", gFail ? "app.frame_step: FAILED" : "app.frame_step: PASSED");
    return gFail ? 1 : 0;
}
