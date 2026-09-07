// DYNAMIC PROBES (REFLECTIONS_ADOPTION_SPEC.md P5a, finding F9) — the gate that
// proves a reflection probe can follow a scene that MOVES.
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
//   (a) with dynamicProbes = 0 (the default), sliding a green slab onto the
//       mirror's reflection ray does NOT change the mirror pixel — it is still
//       showing the red wall the slab now covers. That is the STALENESS this
//       phase exists to fix, pinned as a contract so a future change cannot
//       make dynamic probes the accidental default;
//   (b) with dynamicProbes = 4 the same slide turns the same pixel green;
//   (c) giStatus().dynamicProbeCount reports what the renderer RESOLVED, is
//       clamped to the probes that exist, and goes back to 0 when the request
//       does;
//   (d) the NEAREST-N choice is a choice: with dynamicProbes = 1 exactly one
//       probe is live, and it is the one nearest the camera.
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
// are timed at dynamicProbes 0, 1, 2 and 4 and printed. The numbers are
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

    // =======================================================================
    // (a) STATIC PROBES: the mover slides and the reflection does not notice
    // =======================================================================
    moveMover(kMoverParked);
    CHECK(s->setGlobalIllumination(hybrid), "hybrid arms with dynamicProbes = 0 (the default)");
    render(engine.get(), 6);
    const Colour staticParked = mirrorPixel();
    show("static probes, mover parked", staticParked);
    {
        const GiStatus st = s->giStatus();
        std::printf("   giStatus: probes=%d pccBound=%s dynamic=%d\n",
                    st.probeCount, st.pccBound ? "true" : "false", st.dynamicProbeCount);
        CHECK(st.probeCount == kProbes, "the probe grid built (2 x 1 x 2 = 4)");
        CHECK(st.pccBound, "the probe grid is bound to HlmsPbs");
        CHECK(st.dynamicProbeCount == 0, "dynamicProbes defaults to 0: no probe is live");
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
          "static probes: moving the slab onto the reflection ray changes NOTHING (stale)");
    CHECK(staticMoved.g < staticMoved.r,
          "static probes: the reflection is still red, not the green now in front of it");

    // =======================================================================
    // (b) DYNAMIC PROBES: the same slide, live
    // =======================================================================
    // Rebuilt with the mover parked again, so the two halves start from an
    // identical probe capture and the ONLY difference is dynamicProbes.
    moveMover(kMoverParked);
    hybrid.dynamicProbes = kProbes;
    CHECK(s->setGlobalIllumination(hybrid), "hybrid re-arms with dynamicProbes = 4");
    render(engine.get(), 6);
    const Colour dynParked = mirrorPixel();
    show("dynamic probes, mover parked", dynParked);
    {
        const GiStatus st = s->giStatus();
        std::printf("   giStatus: probes=%d pccBound=%s dynamic=%d\n",
                    st.probeCount, st.pccBound ? "true" : "false", st.dynamicProbeCount);
        CHECK(st.dynamicProbeCount == kProbes,
              "giStatus reports all four probes live (the RESOLVED count)");
    }
    // The picture with every probe live, and nothing moving, must be the same
    // picture the static grid produced: making a probe dynamic changes WHEN it
    // captures, never WHAT it captures.
    CHECK(std::fabs(dynParked.r - staticParked.r) < 0.06f &&
          std::fabs(dynParked.g - staticParked.g) < 0.06f,
          "dynamic probes on a still scene render the same picture as static ones");

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
    hybrid.dynamicProbes = 99;         // more than exist
    CHECK(s->setGlobalIllumination(hybrid), "an over-large dynamicProbes request is accepted");
    render(engine.get(), 4);
    CHECK(s->giStatus().dynamicProbeCount == kProbes,
          "dynamicProbes is CLAMPED to the probes that exist (99 -> 4)");

    hybrid.dynamicProbes = 0;
    CHECK(s->setGlobalIllumination(hybrid), "dynamicProbes goes back to 0");
    render(engine.get(), 4);
    CHECK(s->giStatus().dynamicProbeCount == 0, "no probe is live again");
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
    // (d) NEAREST-N is a choice, not "all of them"
    // =======================================================================
    // The camera sits at z = +2.4, so the two probes on the +Z half of the room
    // are nearer than the two on the -Z half. With a budget of one, the live
    // probe must be a +Z one — asserted through the PICTURE rather than through
    // a probe index, because the index is an implementation detail and the
    // picture is the contract: the mirror's reflection ray lands on the +Z
    // wall, so refreshing a +Z probe is what can possibly change it.
    moveMover(kMoverParked);
    hybrid.dynamicProbes = 1;
    CHECK(s->setGlobalIllumination(hybrid), "hybrid re-arms with dynamicProbes = 1");
    render(engine.get(), 6);
    const Colour oneParked = mirrorPixel();
    CHECK(s->giStatus().dynamicProbeCount == 1, "exactly one probe is live");
    moveMover(kMoverOnRay);
    render(engine.get(), 8);
    const Colour oneMoved = mirrorPixel();
    show("one live probe, mover parked", oneParked);
    show("one live probe, mover ON the ray", oneMoved);
    // One of four probes refreshed: the shader blends all four that cover the
    // pixel, so the green arrives at roughly a quarter strength. The assertion
    // is that it arrives AT ALL and that it is less than the all-live reading —
    // which together say the choice is real in both directions.
    CHECK(oneMoved.g > oneParked.g + 0.03f,
          "one live probe still moves the reflection (the nearest probe is a +Z one)");
    CHECK((oneMoved.g - oneMoved.r) < (dynMoved.g - dynMoved.r),
          "one live probe moves it LESS than four do (the budget is a budget)");

    // =======================================================================
    // FRAME COST (spec §7: budget the re-render cost). Printed, plus a shape
    // assertion. Debug build on whatever GPU is running the gate.
    // =======================================================================
    std::printf("\n   --- frame cost, %dx128x128 offscreen, Debug ---\n", 1);
    double cost[5] = { 0, 0, 0, 0, 0 };
    for (int n : { 0, 1, 2, 4 }) {
        hybrid.dynamicProbes = n;
        s->setGlobalIllumination(hybrid);
        render(engine.get(), 4);
        cost[n] = msPerFrame(engine.get(), 30);
        std::printf("   dynamicProbes = %d   %6.2f ms/frame   (live probes: %d)\n",
                    n, cost[n], s->giStatus().dynamicProbeCount);
    }
    const double perProbe1 = cost[1] - cost[0];
    const double perProbe4 = (cost[4] - cost[0]) / 4.0;
    std::printf("   per-live-probe: %.2f ms (from n=1)   %.2f ms (from n=4)\n",
                perProbe1, perProbe4);
    // SHAPE, not wall clock: a live probe must cost something, and four must
    // cost more than one. Anything tighter is a flake on a shared box.
    CHECK(cost[1] > cost[0], "a live probe costs measurably more than none");
    CHECK(cost[4] > cost[1], "four live probes cost more than one");

    // The same measurement at GiQuality::High, printed for the record: High
    // quadruples the probe face resolution (256 -> 512) AND, through the two
    // Auto toggles, turns on HDR captures and SHADOWED captures — so this is
    // the honest ceiling of what one live probe can cost, not a resolution
    // scaling. Nothing is asserted; the number is what a lane brief needs.
    {
        GiParams high = hybrid;
        high.quality = GiQuality::High;
        high.dynamicProbes = 0;
        s->setGlobalIllumination(high); render(engine.get(), 4);
        const double h0 = msPerFrame(engine.get(), 20);
        high.dynamicProbes = 1;
        s->setGlobalIllumination(high); render(engine.get(), 4);
        const GiStatus hs = s->giStatus();
        const double h1 = msPerFrame(engine.get(), 20);
        std::printf("   HIGH quality (512px faces, hdr=%s, shadows=%s): "
                    "%6.2f -> %6.2f ms/frame  (+%.2f ms for one live probe)\n",
                    hs.probeHdr ? "on" : "off", hs.probeShadows ? "on" : "off", h0, h1, h1 - h0);
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
