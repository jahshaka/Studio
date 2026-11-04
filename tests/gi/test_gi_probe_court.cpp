// gi.probe_court — A PROBE THAT PHOTOGRAPHED A FLOOR AND A WALL IS KEPT
// (PHOTON-5 stage 1; SPECS/audits/PHOTON_AUDIT_2026-10-21.md F1, BUGS-8's
// "probe placement keeps no probe").
//
// On a tier that does not trace its reflections (Medium) the reflection-probe
// grid is placed in the scene's region and every probe is kept or dropped by
// what IT photographed: the placement reads one averaged depth ratio per cube
// face (1 = on the region's face, 2 = the encoding's saturation, i.e. sky).
// The rule this row guards counts the faces under saturation and keeps a probe
// that saw geometry in at least two directions; one that saw a single direction
// of geometry and sky elsewhere IS the sky, which stays the fallback where no
// probe covers a pixel (SKY-FALLBACK-1). How many a tier pays for is the VRAM
// budget's ranking, not this rule. The rule it replaced compared the
// box the six faces measured against the lit volume by VOLUME and dropped
// every probe with one sky-facing face — a court, an atrium, a street (Bistro
// Exterior at Low/Medium: probeCount 0, every shop window reflects the sky).
//
// Three fixtures at Medium, the region fitted automatically (no pin):
//   1. an open court: a 20 m floor and four 3 m walls, no roof   keeps >= 1
//   2. the same floor alone                                     PRINTED (18 at the base)
//   3. the court with a roof (a sealed room)                    18 of 18, as at the base
// The counts are printed. ONE SCENE AT A TIME.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static void addBox(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = enginetest::addTestCube(s, albedo, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
}

static const float kHalf = 10.0f;     // the court is 20 m across
static const float kWallH = 3.0f;     // its walls 3 m high
static const float kThick = 0.2f;

static void addFloor(Scene *s)
{
    addBox(s, Colour(0.45f, 0.45f, 0.45f), Vec3(0.0f, -0.1f, 0.0f),
           Vec3(2.0f * kHalf + 2.0f * kThick, 0.2f, 2.0f * kHalf + 2.0f * kThick));
}

static void addWalls(Scene *s)
{
    const Colour white(0.85f, 0.85f, 0.85f);
    const float y = kWallH * 0.5f, outer = 2.0f * kHalf + 2.0f * kThick;
    addBox(s, white, Vec3(-kHalf, y, 0.0f), Vec3(kThick, kWallH, outer));
    addBox(s, white, Vec3(+kHalf, y, 0.0f), Vec3(kThick, kWallH, outer));
    addBox(s, white, Vec3(0.0f, y, -kHalf), Vec3(outer, kWallH, kThick));
    addBox(s, white, Vec3(0.0f, y, +kHalf), Vec3(outer, kWallH, kThick));
}

static void addRoof(Scene *s)
{
    addBox(s, Colour(0.85f, 0.85f, 0.85f), Vec3(0.0f, kWallH + 0.1f, 0.0f),
           Vec3(2.0f * kHalf + 2.0f * kThick, 0.2f, 2.0f * kHalf + 2.0f * kThick));
}

struct Verdict { int probes = 0, dropped = 0, overBudget = 0; bool byRays = false; };

static Verdict measure(Engine *engine, Scene *s)
{
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;      // no rays: the grid is the reflection
    gi.bounces = true;
    gi.updateBudget = 0;                 // deterministic: no re-captures mid-read
    if (!s->setGlobalIllumination(gi))
        std::printf("   engine error: %s\n", engine->lastError().c_str());
    enginetest::frames(engine, 10);
    const GiStatus st = s->published().gi;
    Verdict v;
    v.probes = st.probeCount;
    v.dropped = st.probesDropped;
    v.overBudget = st.probesOverBudget;
    v.byRays = st.probeGridByRays;
    return v;
}

static void report(const char *name, const Verdict &v)
{
    std::printf("-- %s\n   probes=%d dropped=%d overBudget=%d byRays=%d\n", name, v.probes,
                v.dropped, v.overBudget, int(v.byRays));
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-probe-court-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    View *view = engine->createOffscreenView("court", 128, 128, Colour(0, 0, 0));

    auto scene = [&](const char *name, bool walls, bool roof) {
        Scene *s = engine->createScene(name);
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addFloor(s);
        if (walls) addWalls(s);
        if (roof) addRoof(s);
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 1.7f, 6.0f), Vec3(0.0f, 1.2f, 0.0f));
        const Verdict v = measure(engine.get(), s);
        engine->destroyScene(s);
        return v;
    };

    const Verdict court = scene("court", true, false);
    report("1. an open court: a 20 m floor, four 3 m walls, no roof", court);
    CHECK(!court.byRays, "1: Medium places a probe grid (no ray reflections)");
    CHECK(court.probes >= 1,
          "1: a probe in a court photographed the floor and the walls: at least one is kept");

    const Verdict flat = scene("flat", false, false);
    report("2. the same floor alone, open sky", flat);
    // PRINTED, NOT A LAW (the spec's amended stage 1): the instrument is one
    // averaged texel per face, so a horizontal face over a floor reads ~1.1
    // with or without a wall and a bare floor's probes count five faces of
    // geometry, like the court's (18 of 18 kept at the base too). Whatever the
    // rule keeps here, the VRAM budget ranks.
    std::printf("   (printed, not asserted: the bare floor keeps %d)\n", flat.probes);

    const Verdict sealed = scene("sealed", true, true);
    report("3. the court with a roof (a sealed room)", sealed);
    CHECK(sealed.probes == 18 && sealed.dropped == 0,
          "3: a sealed room keeps what it kept before the rule changed (18 of 18 of the shipped 3x2x3 grid)");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
