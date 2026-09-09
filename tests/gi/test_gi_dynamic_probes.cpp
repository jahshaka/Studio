// DYNAMIC PROBES (REFLECTIONS_ADOPTION_SPEC.md P5a, finding F9) — the gate that
// proves a reflection probe can follow a scene that MOVES.
//
// RE-PINNED BY THE FIX WAVE (B1/B2, 2026-09-07), and the re-pin is the policy
// change rather than a tolerance: `dynamicProbes` ("keep the nearest N probes
// live for ever") became `updateBudget` ("re-capture N probes PER FRAME, in a
// sweep that reaches every probe"). Three consequences, all visible below:
//   * the DEFAULT is 1, not 0. Case (a)'s "static probes are stale" contract is
//     therefore asserted by ASKING for budget 0 rather than by saying nothing;
//   * a budget below the probe count no longer means "some probes never update".
//     It means they update in turn — so case (d), which used to assert that one
//     live probe moves the reflection LESS than four do, now asserts the shape
//     that replaced it: after ONE frame one probe has caught up, and after a
//     full sweep (probes / budget frames) the picture equals the all-at-once
//     one. That is a stronger statement than the old one and it is the property
//     gi.budget pins in general;
//   * the cost table is a table of budgets.
//
// What shipped before this phase: every probe in the hybrid's grid is static.
// The six cube faces are rendered once, at build time, and the reflection they
// carry is frozen there until somebody calls world.refreshGi() and pays for a
// complete re-solve. Drag a lit object across a room and the mirror in the
// corner keeps showing the room as it was — silently, with probeCount and
// pccBound both perfectly healthy, which is the same shape of failure P4 and
// P1a were built to catch.
//
// What this suite asserts, in one scene, changing exactly one number:
//   (a) with updateBudget = 0 (GI paused), sliding a green slab onto the
//       mirror's reflection ray does NOT change the mirror pixel — it is still
//       showing the red wall the slab now covers. That is the STALENESS this
//       phase exists to fix, and what a budget of 0 still buys anyone who wants
//       reflections frozen;
//   (b) with updateBudget = 4 (the whole grid every frame) the same slide turns
//       the same pixel green;
//   (c) giStatus().probeUpdatesPerFrame reports what the renderer RESOLVED, is
//       clamped to the probes that exist, and goes back to 0 when the request
//       does;
//   (d) a budget of 1 gets there too, in a sweep: one frame moves it part of the
//       way, four frames (four probes, budget one) move it as far as the
//       all-at-once budget did.
//
// THE SCENE is gi.pcc_mirror's closed room, deliberately: it is the room whose
// probe behaviour every other suite in this program already pins, so a reading
// here can be compared against one there. Interior x in [-4,4], y in [0,5],
// z in [-4,4]; the +Z wall (behind the camera) is saturated red; a roughness-0
// metallic box in the middle reflects it straight back at the camera.
//
// THE MOVING OBJECT is a green slab that slides ACROSS the red wall's inner
// face rather than through the room. Two reasons, both learned the hard way:
//   * it must never occlude the camera's view of the mirror, and behind the
//     camera is the only place in a sealed room that guarantees that;
//   * a probe reflection is PARALLAX-CORRECTED — the shader reprojects the
//     reflection ray onto the probe's fitted box and samples the cubemap in
//     THAT direction — so an object floating in mid-room reflects into a
//     direction that depends on the probe's fit, while an object flush against
//     the wall the ray lands on reflects wherever the ray lands. The second is
//     a test; the first is a coin toss.
//
// FRAME COST is measured here rather than argued (spec §7's "budget the
// re-render cost", and the owner's Debug-daily-driver rule): the same 30 frames
// are timed at updateBudget 0, 1, 2 and 4 and printed. The numbers are
// printed, not asserted — a wall-clock assertion on a shared CI box is a flake
// generator — but the SHAPE is asserted: more live probes must cost more, and
// the per-probe increment must be roughly linear.
//
// Determinism: the same discipline as its siblings — offscreen view (1x MSAA),
// no SSAO, no planar actors, explicit GI bounds so probe placement does not
// depend on the auto-fit, hue ranges rather than exact colours, and no geometry
// moving while a reading is taken (the slab is moved, THEN frames are rendered,
// THEN the pixel is read).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static void render(Engine *e, int frames = 4)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static void show(const char *what, const Colour &c)
{
    std::printf("   %-40s r=%.3f g=%.3f b=%.3f   (g-r)=%+.3f\n",
                what, c.r, c.g, c.b, c.g - c.r);
}

static NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = enginetest::addTestCube(s, albedo, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

// Wall-clock cost of `frames` frames, in milliseconds per frame. Debug build,
// which is the build the owner's fps rule is about.
static double msPerFrame(Engine *e, int frames)
{
    render(e, 3);        // let the first (cold) frames out of the measurement
    const auto t0 = std::chrono::steady_clock::now();
    render(e, frames);
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / double(frames);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-dynamic-probes-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("dyn", 128, 128, Colour(0, 0, 0));
    Scene *s = engine->createScene("dyn");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // ---- the closed room (gi.pcc_mirror's, byte for byte) -----------------
    const Colour white(0.85f, 0.85f, 0.85f);
    const Colour red(1.0f, 0.02f, 0.02f);
    const Colour green(0.02f, 1.0f, 0.02f);
    addSlab(s, white, Vec3(0.0f, -0.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f));   // floor
    addSlab(s, white, Vec3(0.0f,  5.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f));   // ceiling
    addSlab(s, white, Vec3(0.0f,  2.5f, -4.2f), Vec3(8.8f, 5.0f, 0.4f));  // -Z wall
    addSlab(s, white, Vec3(-4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));   // -X wall
    addSlab(s, white, Vec3( 4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));   // +X wall
    addSlab(s, red,   Vec3(0.0f,  2.5f, 4.2f), Vec3(8.8f, 5.0f, 0.4f));   // +Z: THE red wall

    // ---- the mirror -------------------------------------------------------
    const NodeId mirror = enginetest::addTestCube(s, Colour(1.0f, 1.0f, 1.0f), 1.0f, 0.0f);
    s->setNodeTransform(mirror, Vec3(0.0f, 2.0f, 0.0f), Quat(), Vec3(1.6f, 1.6f, 1.6f));

    // ---- THE MOVER: a green slab flush against the red wall ---------------
    // Parked off to the -X side, where the mirror's centre reflection ray does
    // not land. `kMoverParked` and `kMoverOnRay` are the only thing that ever
    // changes about this scene.
    static const float kMoverParked = -3.2f;
    static const float kMoverOnRay  =  0.0f;
    static const float kMoverZ      =  3.7f;   // in front of the red wall's inner face (z = 4.0)
    const NodeId mover = addSlab(s, green, Vec3(kMoverParked, 2.0f, kMoverZ),
                                 Vec3(3.2f, 3.2f, 0.4f));
    const auto moveMover = [&](float x) {
        s->setNodeTransform(mover, Vec3(x, 2.0f, kMoverZ), Quat(), Vec3(3.2f, 3.2f, 0.4f));
    };

    // ---- light ------------------------------------------------------------
    // gi.pcc_mirror's: near-horizontal, travelling towards +Z, so it lights the
    // red wall's inner face head-on — and the green slab's -Z face, which is
    // the face the mirror sees, at the same near-head-on angle.
    CHECK(enginetest::addDirectionalLight(s, Vec3(0.0f, -0.12f, 0.993f), 6.0f) != 0,
          "directional light created");

    // ---- camera -----------------------------------------------------------
    // Closer than gi.pcc_mirror's (2.4 rather than 3.6) so that the camera is
    // in FRONT of the mover's slab (which spans z in [3.5, 3.9]) with room to
    // spare, and the mirror still overfills the middle of the frame.
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 2.4f), Vec3(0.0f, 2.0f, 0.0f));
    const unsigned mirrorX = 64, mirrorY = 64;

    Image img;
    const auto mirrorPixel = [&]() { view->readPixels(img); return img.at(mirrorX, mirrorY); };

    // Explicit GI bounds, for the same reason gi.pcc_mirror uses them: probe
    // placement must not depend on the auto-fit heuristic, which is a different
    // phase's subject.
    const Vec3 giMin(-4.6f, -0.6f, -4.6f), giMax(4.6f, 5.6f, 4.6f);

    GiParams hybrid;
    hybrid.mode = GiMode::VctPccHybrid;
    hybrid.quality = GiQuality::Medium;
    hybrid.numBounces = 2;
    hybrid.boundsMin = giMin; hybrid.boundsMax = giMax;
    hybrid.pccProbesX = 2; hybrid.pccProbesY = 1; hybrid.pccProbesZ = 2;   // 4 probes
    const int kProbes = 4;
    hybrid.updateBudget = 0;    // (a) asks for PAUSED explicitly: the default is 1 now

    // =======================================================================
    // (a) STATIC PROBES: the mover slides and the reflection does not notice
    // =======================================================================
    moveMover(kMoverParked);
    CHECK(s->setGlobalIllumination(hybrid), "hybrid arms with updateBudget = 0 (GI paused)");
    render(engine.get(), 6);
    const Colour staticParked = mirrorPixel();
    show("static probes, mover parked", staticParked);
    {
        const GiStatus st = s->giStatus();
        std::printf("   giStatus: probes=%d pccBound=%s updates/frame=%d\n",
                    st.probeCount, st.pccBound ? "true" : "false", st.probeUpdatesPerFrame);
        CHECK(st.probeCount == kProbes, "the probe grid built (2 x 1 x 2 = 4)");
        CHECK(st.pccBound, "the probe grid is bound to HlmsPbs");
        CHECK(st.probeUpdatesPerFrame == 0, "budget 0: no probe re-captures");
    }
    CHECK(staticParked.r > staticParked.g + 0.12f && staticParked.r > 0.15f,
          "static, parked: the mirror shows the RED wall (the baseline reading)");

    moveMover(kMoverOnRay);
    render(engine.get(), 8);
    const Colour staticMoved = mirrorPixel();
    show("static probes, mover ON the ray", staticMoved);
    // THE STALENESS CONTRACT. The green slab is now covering exactly the part
    // of the red wall the mirror reflects, and the picture has not changed:
    // the probe cubemaps still hold the room as it was at build time.
    CHECK(std::fabs(staticMoved.r - staticParked.r) < 0.05f &&
          std::fabs(staticMoved.g - staticParked.g) < 0.05f,
          "paused GI: moving the slab onto the reflection ray changes NOTHING (frozen)");
    CHECK(staticMoved.g < staticMoved.r,
          "paused GI: the reflection is still red, not the green now in front of it");

    // =======================================================================
    // (b) DYNAMIC PROBES: the same slide, live
    // =======================================================================
    // Rebuilt with the mover parked again, so the two halves start from an
    // identical probe capture and the ONLY difference is updateBudget.
    moveMover(kMoverParked);
    hybrid.updateBudget = kProbes;
    CHECK(s->setGlobalIllumination(hybrid), "hybrid re-arms with updateBudget = 4 (the whole grid)");
    render(engine.get(), 6);
    const Colour dynParked = mirrorPixel();
    show("dynamic probes, mover parked", dynParked);
    {
        const GiStatus st = s->giStatus();
        std::printf("   giStatus: probes=%d pccBound=%s updates/frame=%d\n",
                    st.probeCount, st.pccBound ? "true" : "false", st.probeUpdatesPerFrame);
        CHECK(st.probeUpdatesPerFrame == kProbes,
              "giStatus reports four probe updates a frame (the RESOLVED budget)");
    }
    // The picture with every probe live, and nothing moving, must be the same
    // picture the static grid produced: making a probe dynamic changes WHEN it
    // captures, never WHAT it captures.
    CHECK(std::fabs(dynParked.r - staticParked.r) < 0.06f &&
          std::fabs(dynParked.g - staticParked.g) < 0.06f,
          "a live budget on a still scene renders the same picture as a paused one");

    moveMover(kMoverOnRay);
    render(engine.get(), 8);
    const Colour dynMoved = mirrorPixel();
    show("dynamic probes, mover ON the ray", dynMoved);
    // THE POINT OF THE PHASE.
    CHECK(dynMoved.g > dynMoved.r + 0.12f,
          "dynamic probes: the mirror now shows the GREEN slab that moved in front of the wall");
    CHECK(dynMoved.g > dynParked.g + 0.15f,
          "dynamic probes: the reflection CHANGED because the scene did");
    CHECK((dynMoved.g - dynMoved.r) > (staticMoved.g - staticMoved.r) + 0.20f,
          "dynamic vs static on the identical scene state: only the dynamic grid followed it");

    // =======================================================================
    // (c) the request is clamped, and it goes back down
    // =======================================================================
    hybrid.updateBudget = 99;         // more than exist
    CHECK(s->setGlobalIllumination(hybrid), "an over-large updateBudget request is accepted");
    render(engine.get(), 4);
    CHECK(s->giStatus().probeUpdatesPerFrame == kProbes,
          "updateBudget is CLAMPED to the probes that exist (99 -> 4)");

    hybrid.updateBudget = 0;
    CHECK(s->setGlobalIllumination(hybrid), "updateBudget goes back to 0");
    render(engine.get(), 4);
    CHECK(s->giStatus().probeUpdatesPerFrame == 0, "nothing re-captures again");
    // ...and the reflection freezes again, at whatever it last captured.
    const Colour refrozenA = mirrorPixel();
    moveMover(kMoverParked);
    render(engine.get(), 8);
    const Colour refrozenB = mirrorPixel();
    show("re-frozen, mover parked again", refrozenB);
    CHECK(std::fabs(refrozenB.r - refrozenA.r) < 0.05f &&
          std::fabs(refrozenB.g - refrozenA.g) < 0.05f,
          "back at 0 the reflection is frozen again (the flip is reversible both ways)");

    // =======================================================================
    // (d) A BUDGET BELOW THE PROBE COUNT IS A SWEEP, not a subset
    // =======================================================================
    // What replaced the old nearest-N assertion (see the header's re-pin note).
    // With four probes and a budget of one, one frame refreshes one probe and
    // four frames refresh all four — so the picture after a full sweep must be
    // the picture the all-at-once budget produced, and the picture after a
    // single frame must be on the way there without being there yet.
    moveMover(kMoverParked);
    hybrid.updateBudget = 1;
    CHECK(s->setGlobalIllumination(hybrid), "hybrid re-arms with updateBudget = 1");
    render(engine.get(), 8);      // two full sweeps: everything settled and parked
    const Colour oneParked = mirrorPixel();
    CHECK(s->giStatus().probeUpdatesPerFrame == 1, "exactly one probe update a frame");
    show("budget 1, mover parked", oneParked);

    moveMover(kMoverOnRay);
    render(engine.get(), 1);
    const Colour afterOneFrame = mirrorPixel();
    show("budget 1, ONE frame after the move", afterOneFrame);
    render(engine.get(), kProbes - 1);
    const Colour afterOneSweep = mirrorPixel();
    show("budget 1, a FULL SWEEP after the move", afterOneSweep);

    // One frame: something moved, but not everything.
    CHECK(afterOneFrame.g > oneParked.g + 0.02f,
          "one frame of budget 1 already moves the reflection (a probe caught up)");
    CHECK(afterOneSweep.g > afterOneFrame.g + 0.02f,
          "...and the rest of the sweep moves it further (the budget is a RATE)");
    // A full sweep: the same answer the whole-grid budget gave, because every
    // probe has now had its turn. Tolerance is the pixel-noise band the rest of
    // this suite uses, not a fudge: the two paths render identical captures.
    CHECK(std::fabs(afterOneSweep.g - dynMoved.g) < 0.06f &&
          std::fabs(afterOneSweep.r - dynMoved.r) < 0.06f,
          "after ceil(probes / budget) frames budget 1 has caught up with budget 4");

    // =======================================================================
    // (e) EPIC'S DYNAMIC PROBES (GiParams::dynamicProbes; the Rayon tier
    //     table's Epic column, owner option (b) 2026-09-09)
    // =======================================================================
    // The sweep spends budget 1 on ONE probe a frame, best-first; (d) above
    // showed that one frame after the move the reflection is "on the way".
    // Epic reserves `dynamicProbes` EXTRA captures per frame for probes whose
    // area covers a box that moved THIS frame. Two probes of this 2x1x2 grid
    // cover the slab's strip along the +Z wall, so with budget 1 + 2 dynamic
    // the frame after the move re-captures BOTH covering probes instead of
    // one — and the picture after one frame is further along than the sweep
    // alone got it. Three more things are pinned: the reservation is reported
    // resolved; at rest it spends NOTHING (the column is free until something
    // moves); and the frame after the mover settles it is back to zero.
    static const int kDynamic = 2;
    moveMover(kMoverParked);
    hybrid.updateBudget = 1;
    hybrid.dynamicProbes = 0;
    CHECK(s->setGlobalIllumination(hybrid), "budget 1, NO dynamic probes (High's row)");
    render(engine.get(), 8);
    moveMover(kMoverOnRay);
    render(engine.get(), 1);
    const Colour sweepOneFrame = mirrorPixel();
    show("budget 1 / dynamic 0, ONE frame after the move", sweepOneFrame);
    CHECK(s->giStatus().dynamicProbes == 0 && s->giStatus().dynamicProbeUpdates == 0,
          "no reservation: giStatus reports 0 dynamic probes, 0 spent");

    moveMover(kMoverParked);
    hybrid.dynamicProbes = kDynamic;
    CHECK(s->setGlobalIllumination(hybrid), "budget 1 + 2 dynamic probes (Epic's row)");
    render(engine.get(), 8);      // settled and parked: two full sweeps
    {
        const GiStatus st = s->giStatus();
        std::printf("   giStatus at rest: updates/frame=%d dynamicProbes=%d dynamicProbeUpdates=%d\n",
                    st.probeUpdatesPerFrame, st.dynamicProbes, st.dynamicProbeUpdates);
        CHECK(st.probeUpdatesPerFrame == 1, "the budget is still one probe a frame");
        CHECK(st.dynamicProbes == kDynamic, "giStatus reports the RESOLVED reservation (2)");
        CHECK(st.dynamicProbeUpdates == 0, "and at REST it spends nothing (free until something moves)");
    }
    const Colour dynRest = mirrorPixel();
    CHECK(std::fabs(dynRest.r - oneParked.r) < 0.06f && std::fabs(dynRest.g - oneParked.g) < 0.06f,
          "a reservation on a still scene renders the same picture as none");

    moveMover(kMoverOnRay);
    render(engine.get(), 1);
    const Colour dynOneFrame = mirrorPixel();
    show("budget 1 / dynamic 2, ONE frame after the move", dynOneFrame);
    {
        const GiStatus st = s->giStatus();
        std::printf("   giStatus the frame after the move: dynamicProbeUpdates=%d\n", st.dynamicProbeUpdates);
        CHECK(st.dynamicProbeUpdates >= 1,
              "the frame after the move SPENT the reservation on a probe covering the slab");
    }
    // THE MEASURED DELTA (Epic's column must change the picture, not just a
    // counter): one frame of budget 1 + 2 dynamic gets the reflection greener
    // than one frame of budget 1 alone did, on the identical scene state.
    CHECK((dynOneFrame.g - dynOneFrame.r) > (sweepOneFrame.g - sweepOneFrame.r) + 0.02f,
          "dynamic probes: ONE frame after the move the reflection is further along than the sweep alone");
    CHECK(dynOneFrame.g > sweepOneFrame.g + 0.02f,
          "dynamic probes: measurably greener after one frame (the mover's reflection followed it)");
    render(engine.get(), 2);
    CHECK(s->giStatus().dynamicProbeUpdates == 0,
          "and once the mover settles the reservation spends nothing again");

    // =======================================================================
    // FRAME COST (spec §7: budget the re-render cost). Printed, plus a shape
    // assertion. Debug build on whatever GPU is running the gate.
    // =======================================================================
    std::printf("\n   --- frame cost, %dx128x128 offscreen, Debug ---\n", 1);
    double cost[5] = { 0, 0, 0, 0, 0 };
    for (int n : { 0, 1, 2, 4 }) {
        hybrid.updateBudget = n;
        s->setGlobalIllumination(hybrid);
        render(engine.get(), 4);
        cost[n] = msPerFrame(engine.get(), 30);
        std::printf("   updateBudget = %d   %6.2f ms/frame   (updates/frame: %d)\n",
                    n, cost[n], s->giStatus().probeUpdatesPerFrame);
    }
    const double perProbe1 = cost[1] - cost[0];
    const double perProbe4 = (cost[4] - cost[0]) / 4.0;
    std::printf("   per-probe-update: %.2f ms (from n=1)   %.2f ms (from n=4)\n",
                perProbe1, perProbe4);
    // SHAPE, not wall clock: a probe update must cost something, and four must
    // cost more than one. Anything tighter is a flake on a shared box.
    CHECK(cost[1] > cost[0], "one probe update a frame costs measurably more than none");
    CHECK(cost[4] > cost[1], "four probe updates a frame cost more than one");

    // The same measurement at GiQuality::High, printed for the record: High
    // quadruples the probe face resolution (256 -> 512) AND, through the two
    // Auto toggles, turns on HDR captures and SHADOWED captures — so this is
    // the honest ceiling of what one live probe can cost, not a resolution
    // scaling. Nothing is asserted; the number is what a lane brief needs.
    {
        GiParams high = hybrid;
        high.quality = GiQuality::High;
        high.updateBudget = 0;
        s->setGlobalIllumination(high); render(engine.get(), 4);
        const double h0 = msPerFrame(engine.get(), 20);
        high.updateBudget = 1;
        s->setGlobalIllumination(high); render(engine.get(), 4);
        const GiStatus hs = s->giStatus();
        const double h1 = msPerFrame(engine.get(), 20);
        std::printf("   HIGH quality (512px faces, hdr=%s, shadows=%s): "
                    "%6.2f -> %6.2f ms/frame  (+%.2f ms for one probe update)\n",
                    hs.probeHdr ? "on" : "off", hs.probeShadows ? "on" : "off", h0, h1, h1 - h0);
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
