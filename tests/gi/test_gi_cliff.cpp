// THE GI-VOLUME CLIFF (LIGHTING_FIX fix 1) — the owner's regression, gated.
//
// WHAT WENT WRONG. `OgreScene::giItemBounds` used to drop every item whose
// largest world-AABB extent was more than 4x the MEDIAN of those extents, and
// only from four items upwards. Every part of that is a cliff:
//
//   * the four-item gate is one by construction — a scene's fourth mesh
//     changed the answer with nothing else changing (rig-measured on the
//     owner's scene: 3 cubes -> lit volume y +-17, 4 cubes -> y +-1.1);
//   * a DROP is binary, so an item goes from defining the volume to not
//     existing between one rebuild and the next;
//   * the MEDIAN is an order statistic, so one added object can move the
//     threshold by a factor of five and take other items with it.
//
// The user-visible symptom is a scene that goes dark, or loses its bounce,
// because an object was added — and "nudge a light and it fixes itself", which
// is the re-fit this suite's sibling case covers.
//
// WHAT THIS SUITE ASSERTS. Two independent statements, because the fix has two
// independent halves:
//
//   1. THE FRESH-SCENE TABLE. Build the same ground-plus-N-cubes scene from
//      scratch for N = 1..5 and print the resolved volume each time. No axis
//      may shrink by more than 2x between consecutive N, and every cube must be
//      inside the volume at every N. This is the pure heuristic, with no
//      history to lean on.
//   2. THE LIVE-SCENE TABLE — the owner's actual gesture. ONE scene, cubes
//      added one at a time with a rebuild between each. ONCE THE SCENE HAS
//      CONTENT the volume may only GROW: the hysteresis floor means an item the
//      lit volume already covered is never trimmed out of it, so adding an
//      object cannot take light away from anything.
//
//      The FIRST content object is the deliberate exception, and the exception
//      is the heuristic working rather than a residual cliff. A scene holding
//      nothing but a ground plane has no population to be an outlier against,
//      so the ground IS the scene and the volume is the ground; the moment a
//      cube appears the volume belongs to the cube. Arming the floor there
//      instead would pin the DEFAULT EDITOR SCENE's 1000-unit ground for the
//      rest of the session and every cube after it would be lit inside a
//      thousand-unit voxel volume — measured, and caught by
//      scripting.e2e.gi_bounds.
//
// Its own binary, like its siblings: GI binds process-wide HlmsPbs state.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

// The default editor scene's shape: a ground plane far bigger than anything on
// it. 200 units is what `app/content` ships.
static const float kGroundSpan = 200.0f;

static NodeId box(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = enginetest::addTestCube(s, albedo, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

static NodeId addGround(Scene *s)
{
    return box(s, Colour(0.7f, 0.7f, 0.7f), Vec3(0.0f, -0.1f, 0.0f),
               Vec3(kGroundSpan, 0.2f, kGroundSpan));
}

static NodeId addCube(Scene *s, int i)
{
    // A row along X, 1.5 apart, so each added cube widens the content a little.
    return box(s, Colour(0.8f, 0.3f, 0.2f), Vec3(-3.0f + 1.5f * float(i), 0.5f, 0.0f),
               Vec3(1, 1, 1));
}

struct Box { Vec3 mn, mx; };

static Box solve(Scene *s)
{
    GiParams gi;
    gi.mode = GiMode::Vct;          // no probes: this suite is about the VOLUME
    gi.quality = GiQuality::Low;
    s->setGlobalIllumination(gi);
    const GiStatus st = s->giStatus();
    return Box{ st.boundsMin, st.boundsMax };
}

static void show(const char *label, int n, const Box &b)
{
    std::printf("   %-12s n=%d   x %8.2f..%8.2f   y %8.2f..%8.2f   z %8.2f..%8.2f\n",
                label, n, b.mn.x, b.mx.x, b.mn.y, b.mx.y, b.mn.z, b.mx.z);
}

static float span(const Box &b, int axis)
{
    switch (axis) {
    case 0:  return b.mx.x - b.mn.x;
    case 1:  return b.mx.y - b.mn.y;
    default: return b.mx.z - b.mn.z;
    }
}

static bool contains(const Box &b, const Vec3 &mn, const Vec3 &mx)
{
    return b.mn.x <= mn.x + 1e-3f && b.mn.y <= mn.y + 1e-3f && b.mn.z <= mn.z + 1e-3f &&
           b.mx.x >= mx.x - 1e-3f && b.mx.y >= mx.y - 1e-3f && b.mx.z >= mx.z - 1e-3f;
}

// ---------------------------------------------------------------------------
// 1. THE FRESH-SCENE TABLE. Each N is its own scene, so nothing carries over:
//    this measures the heuristic alone.
// ---------------------------------------------------------------------------
static void freshTable(Engine *engine, View *view)
{
    std::printf("-- fresh scenes: ground + N cubes, N = 1..5 (the cliff table)\n");
    std::vector<Box> table;
    for (int n = 1; n <= 5; ++n) {
        Scene *s = engine->createScene(std::string("cliff_fresh_") + char('0' + n));
        view->setScene(s);
        addGround(s);
        for (int i = 0; i < n; ++i) addCube(s, i);
        enginetest::addDirectionalLight(s, Vec3(0.2f, -1.0f, 0.3f), 4.0f);
        const Box b = solve(s);
        show("fresh", n, b);
        table.push_back(b);
        // Every cube must be lit, at every count. This is the assertion the old
        // heuristic could still pass, which is why it is not the only one.
        const Vec3 cubesMin(-3.0f - 0.5f, 0.0f, -0.5f);
        const Vec3 cubesMax(-3.0f + 1.5f * float(n - 1) + 0.5f, 1.0f, 0.5f);
        CHECK(contains(b, cubesMin, cubesMax), "every cube is inside the lit volume");
        GiParams off;
        s->setGlobalIllumination(off);
        view->setScene(nullptr);
        engine->destroyScene(s);
    }
    // THE CLIFF ASSERTION. 2x is generous — the regression was 15x in y and
    // over 30x in x and z — and it is a hard ceiling: an order-of-magnitude
    // collapse from one added object is exactly what must be impossible.
    for (size_t i = 1; i < table.size(); ++i) {
        bool ok = true;
        for (int ax = 0; ax < 3; ++ax) {
            const float prev = span(table[i - 1], ax), now = span(table[i], ax);
            if (prev > 1e-4f && now < prev * 0.5f) {
                std::printf("   axis %d collapsed %.2f -> %.2f between n=%zu and n=%zu\n",
                            ax, prev, now, i, i + 1);
                ok = false;
            }
        }
        CHECK(ok, "no axis collapses by more than 2x when one more cube exists");
    }
}

// ---------------------------------------------------------------------------
// 2. THE LIVE-SCENE TABLE — the gesture the owner actually made. One scene,
//    one cube added at a time. The hysteresis floor makes this monotone.
// ---------------------------------------------------------------------------
static void liveTable(Engine *engine, View *view)
{
    std::printf("-- one live scene: cubes added one at a time (the owner's gesture)\n");
    Scene *s = engine->createScene("cliff_live");
    view->setScene(s);
    addGround(s);
    enginetest::addDirectionalLight(s, Vec3(0.2f, -1.0f, 0.3f), 4.0f);
    Box prev = solve(s);
    show("live", 0, prev);
    // RE-PINNED BY SMOKE_FIX S14, and the re-pin is the owner's decision rather
    // than a tolerance slip. This used to read `span > 100` — "a scene that is
    // ONLY a ground plane keeps the whole ground, because there is no
    // population for an outlier to be an outlier against". That is still true
    // of the TRIM (the geometric mean of one extent is that extent, so the ramp
    // cannot fire), and it is exactly the hole the owner's smoke test found: a
    // new project, born Epic on a ground plane, voxelised a square kilometre at
    // 8.1 m per voxel. `GiParams::autoBoundsMax` now caps an AUTOMATIC fit at
    // 64 m centred on the content, so a 200 m ground resolves to the ceiling
    // plus its one-voxel margin. What this case still asserts — and it is the
    // property that mattered — is that the ground-only scene does NOT collapse
    // onto nothing: the volume is the ceiling, not a speck, and the table below
    // still starts from it.
    CHECK(span(prev, 0) > 60.0f && span(prev, 0) < 80.0f,
          "a scene that is ONLY a ground plane is lit out to the automatic ceiling (64 m + margin)");
    // From the FIRST CONTENT OBJECT onwards (see the header): n = 1 is where a
    // scene stops being scenery, and the comparison starts there.
    for (int n = 1; n <= 5; ++n) {
        const bool comparable = n > 1;
        addCube(s, n - 1);
        // A geometry change flags the caches; the flush is at frame time, like
        // every other edit. Two frames is what the sibling suites use.
        engine->renderOneFrame();
        engine->renderOneFrame();
        s->refreshGlobalIllumination();
        const Box b = solve(s);
        show("live", n, b);
        bool grew = true;
        for (int ax = 0; ax < 3; ++ax)
            if (span(b, ax) < span(prev, ax) * 0.5f) grew = false;
        if (comparable)
            CHECK(grew, "adding a cube to a LIVE scene never collapses the lit volume");
        else
            // THE WINDOW IS THE CONTENT'S OWN SIZE NOW, and that is the whole
            // statement of ENGINE-4 item 5: a SUPPORTING SLAB is clipped to
            // the content it supports (plus a small content-relative margin)
            // instead of being morphed back towards its own 200 m, so the
            // first content object's volume is that object's neighbourhood.
            // The same cube measured 6.25 m before the lane (the ground's
            // morph), 3.84 m when the clip still went to the trim core (which
            // carries the collapsed slab, hence the world origin in the answer)
            // and 2.12 m now — the cube, plus half its smallest extent of
            // floor on each side (the patch margin), plus the volume's
            // own one-voxel slack. Below 1 m would mean the cube is not even
            // covered; 4x is "the cube's neighbourhood, not the ground's".
            CHECK(span(b, 0) >= 1.0f && span(b, 0) < 4.0f,
                  "the FIRST content object moves the volume onto the content (by design)");
        prev = b;
    }
    GiParams off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// 3. THE HEURISTIC STILL WORKS. The whole point of trimming an oversized item
//    is that the voxel volume must not be spent on 40,000 square units of empty
//    ground. A fresh default-shaped scene must still hug its content.
// ---------------------------------------------------------------------------
static void stillTrimsCase(Engine *engine, View *view)
{
    std::printf("-- the trimming still happens (a fresh default-shaped scene)\n");
    Scene *s = engine->createScene("cliff_trim");
    view->setScene(s);
    addGround(s);
    for (int i = 0; i < 3; ++i) addCube(s, i);
    enginetest::addDirectionalLight(s, Vec3(0.2f, -1.0f, 0.3f), 4.0f);
    const Box b = solve(s);
    show("trim", 3, b);
    CHECK(span(b, 0) < 20.0f,
          "the lit volume hugs the primitives instead of the 200-unit ground");
    CHECK(span(b, 0) > 4.0f, "...without collapsing onto a single cube");
    GiParams off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// 3b. NO RATCHET (lane ENGINE-7 item 2). Re-fitting a scene NOBODY TOUCHED must
//     return the same volume. It did not: the hysteresis floor above keeps
//     whole anything the previous volume covered, and an outlier the first fit
//     TRIMMED sits inside the volume that trim produced — so the second fit
//     kept it whole and the volume grew to hold it. Measured on the shipped
//     Showroom 2, whose open solves twice: 48.14 m then 56.62 (+17.6%, 0.376 ->
//     0.442 m per voxel), and it stayed there for the session.
//
//     The floor stays for what it is for (case 2's live table: adding an object
//     may not take light away from what was already lit), and it is armed by a
//     CONTENT CHANGE. So this asserts both halves: unchanged content re-fits
//     identically, any number of times, and a MOVED wall still re-fits.
// ---------------------------------------------------------------------------
static void noRatchetCase(Engine *engine, View *view)
{
    std::printf("-- re-fitting an untouched scene returns the same volume\n");
    Scene *s = engine->createScene("cliff_ratchet");
    view->setScene(s);
    addGround(s);                       // the oversized item the trim acts on
    for (int i = 0; i < 3; ++i) addCube(s, i);
    enginetest::addDirectionalLight(s, Vec3(0.2f, -1.0f, 0.3f), 4.0f);
    const Box first = solve(s);
    show("ratchet", 0, first);
    bool same = true;
    for (int n = 1; n <= 5; ++n) {
        const Box again = solve(s);
        for (int ax = 0; ax < 3; ++ax)
            if (std::fabs(span(again, ax) - span(first, ax)) > 1e-3f) same = false;
        if (n == 5) show("ratchet", n, again);
    }
    CHECK(same, "five re-fits of an UNTOUCHED scene return the first fit, to the millimetre");

    // ...and the floor still does its job: move the ground (a real content
    // change) and the fit follows it rather than being pinned to the old one.
    const NodeId far = box(s, Colour(0.2f, 0.4f, 0.8f), Vec3(14.0f, 0.5f, 0.0f), Vec3(1, 1, 1));
    (void)far;
    const Box after = solve(s);
    show("ratchet+", 6, after);
    CHECK(span(after, 0) > span(first, 0) + 1.0f,
          "...but a CHANGE to the content still re-fits (the new cube is lit)");
    const Box held = solve(s);
    CHECK(std::fabs(span(held, 0) - span(after, 0)) < 1e-3f,
          "...and the fit it settles on is stable again straight away");
    GiParams off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// 3c. THE FLOOR SURVIVES THE SOLVE IT WAS ARMED BY (round-2 review, item 1).
//     Arming the hysteresis floor on the CONTENT CHANGE alone is not enough: a
//     re-solve with unchanged content (a light move, a material edit) would then
//     find no floor and re-derive the trimmed answer, so the volume oscillates —
//     grows on the frame the cube arrives, shrinks back on the next solve — and
//     the light the floor promised an already-lit object is taken away one solve
//     late. What the fit owes is "unchanged content returns the answer that was
//     ADOPTED for it", which is the memo giItemBounds keeps.
//
//     The scene needs an outlier the floor can actually act on: one that is
//     TRIMMED by the ramp and whose whole box is INSIDE the volume the previous
//     fit produced (Showroom 2's own shape — a room inside its own lit volume).
// ---------------------------------------------------------------------------
static void floorSurvivesCase(Engine *engine, View *view)
{
    std::printf("-- the floor holds across re-solves of unchanged content\n");
    Scene *s = engine->createScene("cliff_floor_holds");
    view->setScene(s);
    addGround(s);
    // The content: small props spread WIDE, which is what sets both the scale
    // the ramp measures against and the volume the floor remembers.
    for (int i = 0; i < 9; ++i)
        box(s, Colour(0.8f, 0.3f, 0.2f), Vec3(-18.0f + 4.5f * float(i), 0.5f, 0.0f), Vec3(1, 1, 1));
    // ...and some of it high up, so the volume is as TALL as the wall is: an
    // outlier that escapes in one axis is not covered, and the floor is about
    // the ones that are.
    for (int i = 0; i < 3; ++i)
        box(s, Colour(0.8f, 0.3f, 0.2f), Vec3(0.0f, 3.5f + 3.5f * float(i), -3.0f), Vec3(1, 1, 1));
    // The OUTLIER the floor is for: a room-sized wall, far bigger than the props
    // (so the ramp trims it) and small enough to sit INSIDE the volume they
    // produce (so the floor can keep it whole) — Showroom 2's own shape.
    box(s, Colour(0.6f, 0.6f, 0.6f), Vec3(0.0f, 4.0f, -10.0f), Vec3(24.0f, 8.0f, 0.6f));
    enginetest::addDirectionalLight(s, Vec3(0.2f, -1.0f, 0.3f), 4.0f);

    const Box first = solve(s);
    show("floor", 0, first);
    // A CONTENT CHANGE arms the floor: whatever the previous volume covered
    // whole stays whole, so this may only grow.
    box(s, Colour(0.2f, 0.4f, 0.8f), Vec3(3.0f, 0.5f, 3.0f), Vec3(1, 1, 1));
    const Box armed = solve(s);
    show("floor", 1, armed);
    CHECK(span(armed, 0) >= span(first, 0) - 1e-3f,
          "adding an object never shrinks the lit volume (the floor's own promise)");
    // ...AND THE SOLVES AFTER IT KEEP IT. Nothing changed, so nothing may move:
    // this is the oscillation the round-2 review found (48 -> 56 -> 48).
    bool held = true;
    for (int n = 2; n <= 4; ++n) {
        const Box again = solve(s);
        show("floor", n, again);
        for (int ax = 0; ax < 3; ++ax)
            if (std::fabs(span(again, ax) - span(armed, ax)) > 1e-3f) held = false;
    }
    CHECK(held, "three more re-solves of the SAME content return the volume it adopted");
    GiParams off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// 4. THE ESCAPE SIGNATURE (fix 2, engine half). `giEscapeSignature()` is what
//    lets the mirror debounce a re-fit; gi.coalesce drives the mirror half.
// ---------------------------------------------------------------------------
static void escapeSignatureCase(Engine *engine, View *view)
{
    std::printf("-- giEscapeSignature: 0 inside, changing while outside\n");
    Scene *s = engine->createScene("cliff_escape");
    view->setScene(s);
    for (int i = 0; i < 3; ++i) addCube(s, i);
    const NodeId flyer = addCube(s, 3);
    enginetest::addDirectionalLight(s, Vec3(0.2f, -1.0f, 0.3f), 4.0f);
    const Box b = solve(s);
    show("escape", 4, b);
    CHECK(s->giEscapeSignature() == 0ull,
          "everything is inside the freshly fitted volume: the signature is 0");

    enginetest::setNodePosition(s, flyer, Vec3(1.5f, 40.0f, 0.0f));
    engine->renderOneFrame();
    const unsigned long long a1 = s->giEscapeSignature();
    CHECK(a1 != 0ull, "a cube raised out of the volume makes the signature non-zero");

    enginetest::setNodePosition(s, flyer, Vec3(1.5f, 41.0f, 0.0f));
    engine->renderOneFrame();
    const unsigned long long a2 = s->giEscapeSignature();
    CHECK(a2 != a1, "...and it CHANGES while the cube keeps moving (that is the debounce)");

    engine->renderOneFrame();
    CHECK(s->giEscapeSignature() == a2, "...and holds still when the cube does");

    s->refreshGlobalIllumination();
    const Box after = solve(s);
    show("re-fit", 4, after);
    CHECK(after.mx.y > 40.0f, "a re-solve fits the volume around the escapee");
    CHECK(s->giEscapeSignature() == 0ull, "...and the signature is 0 again afterwards");

    GiParams off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    engine->destroyScene(s);
}


// ---------------------------------------------------------------------------
// 5. THE SUPPORTING SLAB is clipped to the CONTENT, and to nothing about
//    ITSELF (ENGINE-4 item 5, round-2 review F2). A ground plane is scenery:
//    the lit volume must cover what stands on it, not the acres it lies on —
//    and the answer must be the same whatever size that ground happens to be,
//    which is the property the first build of this did NOT have (the clip went
//    to a box derived from the trim core, and the core carries the collapsed
//    slab: the same crate on a 50 m ground resolved to 15 m of volume and on a
//    200 m one to 2 m).
// ---------------------------------------------------------------------------
static void slabPatchCase(Engine *engine, View *view)
{
    std::printf("-- the ground's size does not decide the volume; the content does\n");
    const float grounds[3] = { 50.0f, 100.0f, 200.0f };
    float spans[3] = { 0.0f, 0.0f, 0.0f };
    for (int g = 0; g < 3; ++g) {
        Scene *s = engine->createScene(("cliff_patch" + std::to_string(g)).c_str());
        view->setScene(s);
        // The same content every time: one 2 m crate at the origin.
        box(s, Colour(0.7f, 0.7f, 0.7f), Vec3(0.0f, -0.1f, 0.0f),
            Vec3(grounds[g], 0.2f, grounds[g]));
        box(s, Colour(0.8f, 0.3f, 0.2f), Vec3(0.0f, 1.0f, 0.0f), Vec3(2.0f, 2.0f, 2.0f));
        enginetest::addDirectionalLight(s, Vec3(0.2f, -1.0f, 0.3f), 4.0f);
        const Box b = solve(s);
        spans[g] = span(b, 0);
        std::printf("   ground %6.0f m -> volume %6.2f m   (x %.2f..%.2f)\n",
                    grounds[g], spans[g], b.mn.x, b.mx.x);
        engine->destroyScene(s);
    }
    const float worst = std::max(std::max(spans[0], spans[1]), spans[2]);
    const float best  = std::min(std::min(spans[0], spans[1]), spans[2]);
    CHECK(worst - best < 0.02f * worst,
          "the same crate resolves to the same volume on a 50, 100 and 200 m ground");
    // ...and it really is the crate's NEIGHBOURHOOD: the crate (2 m) plus half
    // its smallest extent of floor on each side, plus the volume's own
    // one-voxel slack — 4.25 m, where the ground's own extent is 50, 100 or
    // 200. The floor patch is what carries the bounce (measured on this scene
    // through the app: the crate's lit surface reads 83 of 255 with a volume
    // its own size, which is what GI OFF reads, and 155-162 once the patch
    // reaches a crate-height past it), so the bound below is "a few times the
    // crate", not "the crate".
    std::printf("   the 100 m ground's answer: %.2f m\n", spans[1]);
    CHECK(spans[1] > 2.0f && spans[1] < 8.0f,
          "and that volume is the crate's own neighbourhood, not the ground's");
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-cliff-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("cliff", 128, 128, Colour(0, 0, 0));

    freshTable(engine.get(), view);
    liveTable(engine.get(), view);
    stillTrimsCase(engine.get(), view);
    noRatchetCase(engine.get(), view);
    floorSurvivesCase(engine.get(), view);
    escapeSignatureCase(engine.get(), view);
    slabPatchCase(engine.get(), view);

    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
