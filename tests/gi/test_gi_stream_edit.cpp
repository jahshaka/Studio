// gi.stream_edit — AN EDIT THAT ARRIVES WHILE THE ARM IS STILL BEING BUILT
// (SPECS/OPEN_COVER_SPEC.md §2 A; lane OPEN-COVER-2a, the fix round's item 1).
//
// WHAT THE LANE CHANGED, and the window it opened. A world's FIRST global
// illumination arm is the longest thing this engine does on the UI thread —
// 1,025 ms on Grand Showroom 2, of which the probe placement alone is 673 — and
// it used to happen inside ONE frame, drawn behind the loading cover. It is now
// built in STAGES, one per `FramePace::Streaming` driver frame, on frames the
// user can actually see: the cascade chain, then the probe grid's scout, its
// placement and its finish, then the irradiance field. So there is a window,
// three to five frames wide, in which the SCENE CAN BE EDITED while the machine
// is part way through — and every structural edit funnels through
// `invalidateGiCaches`.
//
// THE DEFECT THIS SUITE IS THE GATE ON. The first cut ENDED the build there: it
// set the stage machine to Idle, and its comment claimed that made the flush
// build from scratch. It did not. By then the CHAIN is live, so the flush takes
// `refreshCascadesFast` — which refuses only on a missing cascade 0 or the wrong
// mode — and `rebuildVct` never ran again. The scene was left with its cascades
// and NO probe grid, NO irradiance field and the sky back on every material,
// for the rest of its life, because a cube was added within 80 ms of a create.
// Measured over MCP on the real binary: `probeCount` 0 / `pccBound` false where
// the fix reads 2 / true.
//
// THE RULE. An invalidation REWINDS the machine to its first stage and the
// scout re-fits the box; it does not stop. The chain is not re-paid — it is not
// stale, because `invalidateGiCaches` marks every cascade's item set and the
// flush's own dirty path spends them one per frame (G1) — so an edit inside the
// window costs the probe stages again and nothing else.
//
// WHAT IS ASSERTED
//   1. NOTHING is built while the world is still arriving (`Scene::setLoading`);
//   2. one `Streaming` frame builds the CHAIN and parks — the arm is visibly
//      half done (no grid decision, no field), which is the window existing;
//   3. an edit in that window is in the finished arm: the probe REGION grew to
//      contain an object added outside the old one, the grid reached a decision
//      (kept or dropped, never neither) and the field is bound;
//   4. and it cost ONE chain build, not two — the rewind, not a from-scratch
//      rebuild;
//   5. and a `Complete` frame is never partial: one streaming frame spends ONE
//      stage, one Complete frame finishes the rest. That is the rule every pixel
//      suite and both selftest hashes rest on, and it is why a `--script` run
//      can never see a half-built arm — `editor.frame` is always Complete.
//
// OUT OF SCOPE, and stated so it is not read as covered: a HOST PUSH that
// changes the arm's shape (`setGlobalIllumination` — a quality change, a mode
// change) still builds the whole arm inside the call, as it always did. Only
// the FLUSH stages, which is the first build of a world; a deliberate settings
// change keeping its synchronous cost is this lane's scope line, not an
// oversight.
//
// Its own binary like every GI suite.
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
#define CHECK_MSG(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                               std::printf("\n"); ++failures; } \
                               else { std::printf("  ok: "); std::printf(__VA_ARGS__); \
                               std::printf("\n"); } } while (0)

static const unsigned kSize = 128;

/// One frame at a named pace. The pace is consumed per frame, so it is set per
/// frame — exactly as `EngineSceneViewport`'s driver tick does.
static void frame(Engine *e, FramePace pace)
{
    e->setNextFramePace(pace);
    e->renderOneFrame();
}

static void frames(Engine *e, FramePace pace, int n)
{
    for (int i = 0; i < n; ++i) frame(e, pace);
}

static NodeId box(Scene *s, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId node = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = Colour(0.8f, 0.8f, 0.8f);
    p.metalness = 0.0f;
    p.roughness = 0.6f;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::setNodePosition(s, node, pos);
    enginetest::setNodeScale(s, node, scale);
    return node;
}

static GiParams hybridGi()
{
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;   // the tier every new project is born at
    gi.quality = GiQuality::Medium;
    gi.cascades = true;
    gi.ddgi = GiToggle::On;           // the field is the machine's LAST stage
    gi.updateBudget = 1;
    return gi;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-stream-edit-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("streamedit", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("streamedit");
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.3f, 0.3f, 0.3f));
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 4.0f, 10.0f),
                                                     Vec3(0.0f, 0.5f, 0.0f)));
    // A floor and a wall beside it: enough for the placement to keep some
    // candidates and drop others, which is the decision case 3 needs to exist.
    CHECK(box(scene, Vec3(0.0f, -0.1f, 0.0f), Vec3(10.0f, 0.2f, 10.0f)) != 0, "a floor");
    CHECK(box(scene, Vec3(0.0f, 2.0f, -5.0f), Vec3(10.0f, 4.0f, 0.2f)) != 0, "a wall behind it");
    // The camera has to be TRACKED before a camera-centred arm will build at
    // all (the chain waits for one), and the albedo wait must be over too.
    frames(e, FramePace::Complete, 3);

    // =====================================================================
    // CASE 1 — NOTHING IS BUILT WHILE THE WORLD IS STILL ARRIVING
    // =====================================================================
    std::printf("\n== case 1: the arm waits for the world to be on screen ==\n");
    scene->setLoading(true);
    CHECK(scene->isLoading(), "Scene::setLoading(true) sticks");
    scene->setGlobalIllumination(hybridGi());
    frames(e, FramePace::Streaming, 6);
    {
        const GiStatus st = scene->giStatus();
        CHECK_MSG(st.rebuilds == 0, "case 1: no arm built behind the cover (rebuilds %llu)",
                  (unsigned long long)st.rebuilds);
        CHECK(!st.vctBound, "case 1: nothing is bound either");
    }
    // ...and a COMPLETE frame during the load does not build it either: that is
    // the sticky flag's whole point (a warm-up target, a project tile).
    frames(e, FramePace::Complete, 2);
    CHECK_MSG(scene->giStatus().rebuilds == 0,
              "case 1: a Complete frame during the load builds nothing either (rebuilds %llu)",
              (unsigned long long)scene->giStatus().rebuilds);
    scene->setLoading(false);

    // =====================================================================
    // CASE 2 — ONE STREAMING FRAME BUILDS THE CHAIN AND PARKS
    // =====================================================================
    std::printf("\n== case 2: the window exists ==\n");
    frame(e, FramePace::Streaming);
    GiStatus mid = scene->giStatus();
    CHECK_MSG(mid.rebuilds == 1, "case 2: the chain is built (rebuilds %llu)",
              (unsigned long long)mid.rebuilds);
    CHECK(mid.vctBound, "case 2: ...and bound");
    CHECK_MSG(e->framePaceOwesWork(), "case 2: the engine says it still owes work");
    CHECK_MSG(mid.probeCount == 0 && mid.probesDropped == 0,
              "case 2: no probe decision yet (%d kept, %d dropped)",
              mid.probeCount, mid.probesDropped);
    CHECK(!mid.ifdBound, "case 2: no irradiance field yet");
    const Vec3 regionBefore = mid.probeRegionMax;

    // =====================================================================
    // CASE 3 — AN EDIT IN THE WINDOW IS IN THE FINISHED ARM
    // =====================================================================
    std::printf("\n== case 3: an edit inside the window ==\n");
    // OUTSIDE the floor, deliberately: the probe region is the scene's own
    // fitted box, so an object beyond it can only be inside the region the grid
    // is placed in if the box was RE-FITTED — which is what the rewind is for.
    CHECK(box(scene, Vec3(14.0f, 1.0f, 0.0f), Vec3(2.0f, 2.0f, 2.0f)) != 0,
          "a cube well outside the old box");
    // ONE streaming frame spends ONE stage — the machine must still owe the
    // rest, which is what "the world streams in" means.
    frame(e, FramePace::Streaming);
    CHECK(e->framePaceOwesWork(), "case 3: one streaming frame spends one stage, no more");
    // ...and then a COMPLETE frame finishes whatever is left, in itself. That is
    // the rule every pixel suite and both selftest hashes rest on, and it is why
    // a `--script` run can never see a half-built arm: `editor.frame` is always
    // Complete.
    frame(e, FramePace::Complete);
    CHECK(!e->framePaceOwesWork(),
          "case 3: ONE Complete frame owes nothing afterwards");

    const GiStatus st = scene->giStatus();
    CHECK_MSG(st.probeCount + st.probesDropped > 0,
              "case 3: the probe grid reached a DECISION (%d kept, %d dropped) — the defect "
              "left both at zero for the life of the scene",
              st.probeCount, st.probesDropped);
    CHECK(st.pccBound == (st.probeCount > 0),
          "case 3: the binding agrees with the grid it says it has");
    CHECK(st.ifdBound, "case 3: the irradiance field was built");
    CHECK_MSG(st.probeRegionMax.x > regionBefore.x + 1.0f,
              "case 3: the probe region was RE-FITTED around the new object "
              "(x %.2f -> %.2f)", regionBefore.x, st.probeRegionMax.x);
    CHECK_MSG(st.probeRegionMax.x > 12.0f,
              "case 3: ...and it reaches the cube at x = 14 (%.2f)", st.probeRegionMax.x);
    CHECK_MSG(st.rebuilds == 1,
              "case 3: the CHAIN was not re-paid for the edit (rebuilds %llu) — a rewind, "
              "not a from-scratch build", (unsigned long long)st.rebuilds);

    if (failures) { std::printf("\nFAILURES: %d\n", failures); return 1; }
    std::printf("\nall good\n");
    return 0;
}
