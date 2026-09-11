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
            CHECK(span(b, 0) > 4.0f && span(b, 0) < 20.0f,
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
    escapeSignatureCase(engine.get(), view);

    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
