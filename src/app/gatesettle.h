#pragma once
// THE STARTUP GATE's SETTLE, ON PUBLISHED STATE (COLD-BOOT-1 stage 4). A pure function of what the engine publishes each
// frame (AsyncShaderStats), so its law is a CPU test (tests/app/test_gate_settle.cpp) and the gate never JOINS anything:
// it renders frames, pumps the splash's events between them, and reads this.
//
// WHY. The gate's scene used to compile in its frames: a compute job on its dispatch, a ray pipeline at its consumer's
// first call - and the reflection's two cold driver compiles were one join of 100 s on the render thread with the UI
// thread waiting on it (spikes/cold-boot-1/S1b: the longest UI block 104 s of a 154-s gate). Now the gate's scene is
// ASYNCHRONOUS for the whole gate (its permutations build on the background compiler, its ray pipelines on their
// builders, started at the gate's first frame: Engine::prepareRayPipelines) and a frame is cheap; what the gate waits for
// is the published state saying the work is done:
//   SETTLED (a frame's work is all there) - nothing on the background compiler (hlmsOutstanding 0), and since the frame
//            before: no compute dispatch deferred, no draw with a placeholder, no pending skip; with `needRays`, no ray
//            pipeline build in flight either (a world phase, whose frames run the reflection's consumers).
//   STALLED  (the hang guard, never a settle) - NOTHING HAS LANDED for kStallMs of the WALL: no compile finished
//            (completed), no shader counted built (compiled), no ray pipeline build finished (rayBuildsInFlight fell). A
//            deferral, a placeholder draw or a skipped draw is NOT movement: those counters rise on every frame of a gate
//            that is waiting, and they rose on every frame of the livelock that never landed anything (a convolution
//            whose requests were cancelled by their own try: spikes/cold-boot-1/S4arm, 48,363 frames) - a guard that
//            counted them could never fire. A driver compile publishes nothing while it runs (one cold rq_reflect:
//            55.5 s beside its twin), so no frame count can tell a long compile from a hang: the wall decides.
//   PAST THE CEILING - the gate has run kCeilingMs of the wall in all, landing or not: it ENDS, loudly (the caller's FAIL
//            line names what is still pending: describe()). A gate that cannot settle never spins.
// Either way the window answers throughout (the events are pumped): the editor opens with the work still landing in
// the background - never a freeze, never a splash that does not end.

#include <cstdio>
#include <string>

namespace gatesettle {

/// One frame's published reading (from Engine::published().shaders, after the frame).
struct Sample {
    unsigned hlmsOutstanding = 0;            ///< pending minus the ray builds: the background compiler's queue
    unsigned rayBuildsInFlight = 0;          ///< the ray pipeline builds started and not joined
    unsigned long long computeDeferred = 0;  ///< running total
    unsigned long long placeholderDraws = 0; ///< running total
    unsigned long long pendingSkips = 0;     ///< running total
    unsigned long long completed = 0;        ///< the background compiler's finished jobs (running total)
    unsigned compiled = 0;                   ///< shaders compiled + served (the splash's count)
};

/// The hang guard: this long with NOTHING LANDING ends a wait with the work still landing (see the header). Derived from
/// the longest single uninterruptible driver compile the gate starts - rq_reflect, 55.5 s cold beside its twin on this
/// class of card (spikes/cold-boot-1/S4arm) - times two for a slower CPU: nothing else lands while the last two run.
/// IT MUST STAY ABOVE THE LONGEST SINGLE PIPELINE BUILD: a build longer than it reads as a stall and opens the editor
/// with the work still landing. That number is measured by the cold gate's own log lines ("rayquery: compute pipelines
/// ... this one (<name>) <ms> ms", one per ray pipeline; spikes/cold-boot-1/S1b/tools/sum.py and S4arm/tools/sum4.py
/// read them) - re-read it whenever a ray shader grows (REFLECT-ESTIMATOR-1 owns rq_reflect's 55 s).
constexpr long long kStallMs = 120000;
/// The ceiling of the whole gate, landing or not: four times the old cold gate (154.2 s, spikes/cold-boot-1/S1b), which
/// serialised every one of its compiles.
constexpr long long kCeilingMs = 600000;

class Settle {
public:
    /// Feeds the reading after a frame and the wall clock (ms) at it. Returns settled(needRays) of that frame.
    bool feed(const Sample &s, long long nowMs, bool needRays)
    {
        const bool first = !mHave;
        // MOVEMENT IS A LANDING (the header's STALLED): never a deferral, a placeholder or a skip
        const bool landed = first || s.completed > mLast.completed || s.compiled > mLast.compiled ||
                            s.rayBuildsInFlight < mLast.rayBuildsInFlight;
        mQuietFrame = !first && s.computeDeferred == mLast.computeDeferred &&
                      s.placeholderDraws == mLast.placeholderDraws && s.pendingSkips == mLast.pendingSkips;
        if (first) mFirstMs = nowMs;
        if (landed) mLastMoveMs = nowMs;
        mFrameDeferred = first ? 0 : s.computeDeferred - mLast.computeDeferred;
        mFramePlaceholders = first ? 0 : s.placeholderDraws - mLast.placeholderDraws;
        mFrameSkips = first ? 0 : s.pendingSkips - mLast.pendingSkips;
        mLast = s;
        mHave = true;
        mNow = nowMs;
        return settled(needRays);
    }
    /// The gate has run kCeilingMs of the wall in all (the header's PAST THE CEILING).
    bool pastCeiling() const { return mHave && mNow - mFirstMs >= kCeilingMs; }
    /// THE GATE's WAITS END HERE, and only here: settled, stalled or past the ceiling (every gate loop's condition).
    bool waitOver(bool needRays) const { return settled(needRays) || stalled() || pastCeiling(); }
    /// The wait ended without settling: the caller's FAIL line.
    bool failed(bool needRays) const { return !settled(needRays) && (stalled() || pastCeiling()); }
    /// What is still pending, for the caller's FAIL line: the background compiler's queue, the ray builds in flight, and
    /// what the last frame deferred, drew with a placeholder or skipped; with the ms since anything landed.
    std::string describe() const
    {
        char b[256];
        std::snprintf(b, sizeof b,
                      "%u on the background compiler, %u ray pipeline build(s) in flight; the last frame deferred %llu "
                      "compute dispatch(es), drew %llu placeholder(s), skipped %llu draw(s); nothing landed for %lld ms",
                      mLast.hlmsOutstanding, mLast.rayBuildsInFlight, mFrameDeferred, mFramePlaceholders, mFrameSkips,
                      mNow - mLastMoveMs);
        return b;
    }
    /// The last frame's work is all there (the header's SETTLED).
    bool settled(bool needRays) const
    {
        return mHave && mQuietFrame && mLast.hlmsOutstanding == 0 && (!needRays || mLast.rayBuildsInFlight == 0);
    }
    /// Nothing has moved for kStallMs (the header's STALLED).
    bool stalled() const { return mHave && mNow - mLastMoveMs >= kStallMs; }
    const Sample &last() const { return mLast; }

private:
    Sample mLast;
    bool mHave = false, mQuietFrame = false;
    long long mLastMoveMs = 0, mNow = 0, mFirstMs = 0;
    unsigned long long mFrameDeferred = 0, mFramePlaceholders = 0, mFrameSkips = 0;
};

}  // namespace gatesettle
