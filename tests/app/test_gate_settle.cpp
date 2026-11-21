// app.gate_settle (COLD-BOOT-1 stage 4; CPU, no engine) - THE STARTUP GATE's SETTLE ON PUBLISHED STATE
// (src/app/gatesettle.h says why). The laws:
//   (1) a frame with the background compiler busy is not settled; nor one in which a compute dispatch was deferred, a draw
//       took the placeholder or a draw was skipped (each a frame whose work did not all happen); the first frame never is;
//   (2) a quiet frame with nothing outstanding is settled - WITHOUT the ray builds when the phase does not need them, and
//       NOT while one is in flight when it does (a world phase: the reflection's consumers);
//   (3) the hang guard: nothing LANDING for kStallMs of the wall is a stall; a landing - a compile finished, a shader
//       counted built, a ray build finished - restarts it; a long driver compile with frames flowing and nothing landing
//       IS a stall only after kStallMs (never a frame count);
//   (4) THE LIVELOCK: deferrals, placeholders and skips rising every frame with nothing landing are no movement - the
//       gate's wait (waitOver) ends at kStallMs, and describe() names what is pending (RED on the first cut, which
//       counted them as movement: the startup gate spun 48,363 frames, spikes/cold-boot-1/S4arm);
//   (5) a ray build STARTING (the in-flight count rising) is no landing;
//   (6) the ceiling: a gate that lands something every frame and never settles still ends, at kCeilingMs.
#include "app/gatesettle.h"

#include <cstdio>

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } } while (0)

using gatesettle::Sample;
using gatesettle::Settle;

int main()
{
    Sample s;
    {
        Settle g;
        CHECK(!g.feed(s, 0, false), "(1) the first frame is never settled (no frame before it to compare)");
        CHECK(g.feed(s, 10, false), "(2) a quiet frame with nothing outstanding is settled");
        Sample busy = s; busy.hlmsOutstanding = 3;
        CHECK(!g.feed(busy, 20, false), "(1) the background compiler busy: not settled");
        Sample landed = busy; landed.hlmsOutstanding = 0; landed.completed = 3;
        CHECK(g.feed(landed, 30, false), "(2) its jobs landed and the frame quiet: settled");
        Sample deferred = landed; deferred.computeDeferred = 1;
        CHECK(!g.feed(deferred, 40, false), "(1) a compute dispatch deferred in the frame: not settled");
        CHECK(g.feed(deferred, 50, false), "(2) ...the next frame defers none: settled");
        Sample ph = deferred; ph.placeholderDraws = 7;
        CHECK(!g.feed(ph, 60, false), "(1) a draw with the placeholder in the frame: not settled");
        Sample skip = ph; skip.pendingSkips = 1;
        CHECK(!g.feed(skip, 70, false), "(1) a draw skipped in the frame: not settled");
        CHECK(g.feed(skip, 80, false), "(2) ...the next frame quiet: settled");
    }
    {
        Settle g;
        Sample ray = s; ray.rayBuildsInFlight = 2;
        g.feed(ray, 0, true);
        CHECK(!g.feed(ray, 10, true) && g.settled(false),
              "(2) a ray build in flight: not settled for a phase that needs the rays, settled for one that does not");
        Sample joined = ray; joined.rayBuildsInFlight = 0;
        CHECK(g.feed(joined, 20, true), "(2) the builds joined: settled for the rays' phase too");
    }
    {
        Settle g;
        Sample ray = s; ray.rayBuildsInFlight = 1;
        g.feed(ray, 0, true);
        long long t = 0;
        bool early = false;
        for (int f = 0; f < 10000; ++f) {   // frames flowing over a 50-s driver compile: nothing lands
            t += 5;
            g.feed(ray, t, true);
            if (g.stalled()) early = true;
        }
        CHECK(!early, "(3) a 50-s driver compile with 10,000 frames flowing and nothing landing is no stall");
        g.feed(ray, gatesettle::kStallMs - 1, true);
        CHECK(!g.stalled(), "(3) ...nor at kStallMs - 1 ms");
        g.feed(ray, gatesettle::kStallMs, true);
        CHECK(g.stalled(), "(3) nothing moving for kStallMs of the wall: stalled");
        Sample moved = ray; moved.completed = 1;
        g.feed(moved, gatesettle::kStallMs + 1, true);
        CHECK(!g.stalled(), "(3) a compile landing restarts the guard");
    }
    {
        // (4) THE LIVELOCK (spikes/cold-boot-1/S4arm): every frame defers its compute dispatches and draws placeholders,
        // one job sits on the compiler, and NOTHING EVER LANDS. The gate's wait (waitOver, the loop's one condition) must
        // end at kStallMs as a stall - counting the deferrals as movement it never ended (48,363 frames).
        Settle g;
        Sample live = s; live.hlmsOutstanding = 1;
        long long t = 0;
        int frames = 0;
        g.feed(live, t, true);
        while (!g.waitOver(true) && frames < 1000000) {   // the test's own guard: 5 ms frames, 5000 s
            t += 5;
            ++frames;
            live.computeDeferred += 12;
            live.placeholderDraws += 3;
            live.pendingSkips += 1;
            g.feed(live, t, true);
        }
        CHECK(g.stalled() && g.failed(true) && t == gatesettle::kStallMs,
              "(4) deferrals rising, nothing landing: the wait ENDS as a stall at kStallMs (%lld ms, %d frames)", t, frames);
        const std::string why = g.describe();
        CHECK(why.find("1 on the background compiler") != std::string::npos &&
                  why.find("deferred 12 compute") != std::string::npos,
              "(4) the FAIL line names what is pending: %s", why.c_str());
    }
    {
        // (5) A RAY BUILD STARTING IS NOT A LANDING: the in-flight count rising (a launch) never restarts the guard.
        Settle g;
        Sample ray = s; ray.hlmsOutstanding = 1;
        g.feed(ray, 0, true);
        ray.rayBuildsInFlight = 6;
        g.feed(ray, gatesettle::kStallMs, true);
        CHECK(g.stalled(), "(5) the ray builds launched, nothing landed for kStallMs: stalled");
    }
    {
        // (6) THE CEILING: something lands every frame but the gate never settles (a job always on the compiler): no
        // stall, and the wait still ends - at kCeilingMs, loudly.
        Settle g;
        Sample churn = s; churn.hlmsOutstanding = 1;
        long long t = 0;
        g.feed(churn, t, false);
        bool stalledOnce = false;
        while (!g.waitOver(false) && t < 10 * gatesettle::kCeilingMs) {
            t += 100;
            ++churn.completed;
            g.feed(churn, t, false);
            stalledOnce = stalledOnce || g.stalled();
        }
        CHECK(!stalledOnce && g.pastCeiling() && g.failed(false) && t == gatesettle::kCeilingMs,
              "(6) a landing every frame and never settled: no stall, the wait ends at the ceiling (%lld ms)", t);
    }
    std::printf(gFailures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}
