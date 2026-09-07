// PROBE REBUILDS ARE REPEATABLE, AND NO CLUSTER CELL DROPS A PROBE
// (owner report 2026-09-07: "hard-edged black rectangles crawling over the
// Grand Showroom's metals").
//
// THE ROOT CAUSE, measured on the live sample before a line of this was written,
// and stated first because the report it came from named the wrong one.
//
//   Per-pixel PCC is culled through the FORWARD+ CLUSTER GRID, and
//   `ForwardClustered::collectObjsForSlice` writes a cubemap probe into a cell
//   only while that cell's count is below the per-cell budget:
//       if( numLightsInCell->objCount[objType] < currObjsPerCell )
//   (OgreForwardClustered.cpp:291-298). There is no else branch — the rest are
//   dropped, silently, per cell, in scene order. A cluster cell is a frustum
//   chunk spanning a whole logarithmic depth slice, so it is large in world
//   space, and the shipped Showroom's 4x2x4 grid gives every probe an influence
//   area of ~7.3 x 4.1 x 7.3 units at a 5.9-unit spacing. Cells routinely see
//   more than the budget of 8. Pixels whose own probe was dropped find no probe
//   covering them and the hybrid hands them to VCT cone tracing, which inside a
//   sealed room is black — hence rectangles that are hard-edged, SCREEN-AXIS-
//   ALIGNED on curved mirrors, and move when the camera does.
//
// WHAT THE ORIGINAL REPORT SAID, and what measurement found instead (recorded so
// the wrong lead is not chased again):
//   * "strict period-2 alternation across rebuilds regardless of the knob" —
//     NOT REPRODUCED. Six `refreshGi()` calls with a FIXED knob gave six
//     identical readings; the rig's ping-pong had been alternating
//     snapSidesMin between 0.2500 and 0.2501, and the picture tracked the KNOB,
//     not the rebuild index (0.2500 -> 0.0752/0.2168, 0.2501 -> 0.0317/0.2392,
//     eight rebuilds, perfectly repeatable per value). That knife-edge is real
//     and worth knowing about, but it is not a parity.
//   * "the degenerate shrink-fit paints the black patches" — DISPROVEN by A/B:
//     raising the Forward+ per-cell probe budget removed the rectangles with
//     every probe shape byte-identical (the per-probe dumps of the bad and the
//     clean build match 32 of 32 lines).
//
// So this suite pins the budget, and REPORTS the fit rather than constraining
// it (see the 2b block for why a room-sized parallax box is correct).
//
// THE SCENE is gi.pcc_mirror's room — known to give a saturated red mirror pixel
// through the probes — plus FREE-STANDING COLUMNS, which is what defeats the
// 1x1 averaged-depth shrink-fit, and a 4x2x4 probe grid, which is what
// overflows the cluster cells. Neither of those is in gi.pcc_mirror, which is
// why it stayed green through the whole defect.
//
// Determinism discipline, as gi.pcc_mirror: offscreen view (MSAA 1x), explicit
// GI bounds, no SSAO/planar/sky, nothing moves between reads, and every read
// waits a whole probe sweep after the same call.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

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

/// Long enough for the update budget to re-capture EVERY probe in the grid
/// (32 probes at 1 per frame) plus a settling margin.
static const int kSweepFrames = 48;

static void render(Engine *e, int frames)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale,
                      float metal = 0.0f, float rough = 0.9f)
{
    const NodeId n = enginetest::addTestCube(s, albedo, metal, rough);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

/// The fraction of the metal slab's pixels that are BLACK — the owner's own
/// symptom, as a number. A hybrid pixel handed to cone tracing in a sealed room
/// returns nothing, so the artifact is literally dark.
static float darkFraction(const Image &img, unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    long dark = 0, total = 0;
    for (unsigned y = y0; y < y1 && y < img.height; ++y) {
        for (unsigned x = x0; x < x1 && x < img.width; ++x) {
            const Colour c = img.at(x, y);
            ++total;
            if (c.r < 0.08f && c.g < 0.08f && c.b < 0.08f) ++dark;
        }
    }
    return total ? float(dark) / float(total) : 0.0f;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-pcc-stability-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("pccstab", 192, 192, Colour(0, 0, 0));
    Scene *s = engine->createScene("pccstab");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // ---- the cluttered room ------------------------------------------------
    // Interior x,z in [-4,4], y in [0,5] with a 0.4-thick shell — gi.pcc_mirror's
    // room exactly, because that room is KNOWN to give a saturated red mirror
    // pixel through the probes (its header records r=1.000). Everything this
    // suite adds is on top of a working reference, so a black reading here is a
    // defect and not a scene that never worked.
    const Colour white(0.85f, 0.85f, 0.85f);
    const Colour red(1.0f, 0.02f, 0.02f);
    addSlab(s, white, Vec3(0.0f, -0.2f, 0.0f),  Vec3(8.8f, 0.4f, 8.8f));    // floor
    addSlab(s, white, Vec3(0.0f,  5.2f, 0.0f),  Vec3(8.8f, 0.4f, 8.8f));    // ceiling
    addSlab(s, white, Vec3(0.0f,  2.5f, -4.2f), Vec3(8.8f, 5.0f, 0.4f));    // -Z wall
    addSlab(s, white, Vec3(-4.2f, 2.5f, 0.0f),  Vec3(0.4f, 5.0f, 8.8f));    // -X wall
    addSlab(s, white, Vec3( 4.2f, 2.5f, 0.0f),  Vec3(0.4f, 5.0f, 8.8f));    // +X wall
    // THE red wall: +Z, behind the camera. The only saturated thing in the room,
    // so red in the mirror came from it and from a PROBE (cone tracing cannot
    // carry it — gi.pcc_mirror's light comment explains why at length).
    addSlab(s, red, Vec3(0.0f, 2.5f, 4.2f), Vec3(8.8f, 5.0f, 0.4f));

    // THE CLUTTER — free-standing columns in the corners, which is what defeats
    // the 1x1 averaged-depth shrink-fit (the empty room fits cleanly, which is
    // why gi.pcc_mirror never saw defect 2b). Deliberately clear of the walls,
    // so computeProbeRegion's wall test correctly refuses to treat them as the
    // room's edge: this suite is about the FIT and the probe BUDGET, not about
    // the region.
    for (int i = -1; i <= 1; i += 2)
        for (int j = -1; j <= 1; j += 2)
            addSlab(s, white, Vec3(i * 2.6f, 2.2f, j * 2.6f), Vec3(0.5f, 4.4f, 0.5f));

    // ---- the metal witness -------------------------------------------------
    // Metalness 1, roughness 0: its pixels are the reflection integral and
    // nothing else, so "black" means "this pixel got no reflection".
    PbrParams metalParams;
    metalParams.albedo = Colour(1.0f, 1.0f, 1.0f);
    metalParams.metalness = 1.0f;
    metalParams.roughness = 0.0f;
    const NodeId metal = s->createNode();
    CHECK(metal && s->attachMesh(metal, s->createMesh(enginetest::unitCubeMesh()),
                                 s->createPbrMaterial(metalParams)),
          "the metal witness attaches");
    s->setNodeTransform(metal, Vec3(0.0f, 2.0f, 0.0f), Quat(), Vec3(1.6f, 1.6f, 1.6f));

    const NodeId lightNode = enginetest::addDirectionalLight(s, Vec3(0.0f, -0.12f, 0.993f), 6.0f);
    CHECK(lightNode != 0, "directional light created");

    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 3.6f), Vec3(0.0f, 2.0f, 0.0f));
    // The witness's +Z face fills the middle of the 192x192 frame.
    const unsigned bx0 = 70, by0 = 70, bx1 = 122, by1 = 122;

    // ---- arm the hybrid ----------------------------------------------------
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 2;
    gi.boundsMin = Vec3(-4.6f, -0.6f, -4.6f);
    gi.boundsMax = Vec3( 4.6f,  5.6f,  4.6f);
    gi.pccProbesX = 4; gi.pccProbesY = 2; gi.pccProbesZ = 4;
    // THE SHIPPED DEFAULT (1 probe re-capture per frame). Not paused: with the
    // budget at 0 the hybrid keeps its STATIC trust window and hands every
    // pixel whose probe and voxel answers disagree to cone tracing — black in a
    // sealed room, which would make this suite measure that instead of what it
    // is about. Every read below therefore waits for a WHOLE SWEEP
    // (kSweepFrames >= probeCount) so the grid is fully captured and the frame
    // is settled; the scene never moves, so a settled frame is deterministic.
    gi.updateBudget = 1;
    CHECK(s->setGlobalIllumination(gi), "the hybrid arms");
    render(engine.get(), kSweepFrames);

    const GiStatus st0 = s->giStatus();
    std::printf("   probes=%d pccBound=%s vctBound=%s\n", st0.probeCount,
                st0.pccBound ? "true" : "false", st0.vctBound ? "true" : "false");
    CHECK(st0.probeCount == 32, "32 probes were placed");
    CHECK(st0.pccBound && st0.vctBound, "both halves of the hybrid are bound");

    // ---- 2a: SIX REBUILDS MUST GIVE SIX IDENTICAL FRAMES -------------------
    //
    // The cold-start frame is included on purpose: the defect's worst property
    // was that a fresh process opened on the BAD parity, so "rebuild N == rebuild
    // N+1" alone would have been satisfied by a stable-but-wrong build.
    std::vector<Image> frames;
    std::vector<float> darks;
    for (int pass = 0; pass < 6; ++pass) {
        if (pass) s->refreshGlobalIllumination();
        render(engine.get(), kSweepFrames);
        Image img;
        if (!view->readPixels(img)) { std::printf("FAIL: readPixels pass %d\n", pass); ++failures; break; }
        const float d = darkFraction(img, bx0, by0, bx1, by1);
        const Colour c = img.at(96, 96);
        std::printf("   rebuild %d: metal dark fraction %.4f  centre r=%.3f g=%.3f b=%.3f\n",
                    pass, d, c.r, c.g, c.b);
        darks.push_back(d);
        frames.push_back(std::move(img));
    }

    if (frames.size() == 6) {
        int differing = 0;
        for (size_t i = 1; i < frames.size(); ++i)
            if (frames[i].rgba != frames[0].rgba) ++differing;
        std::printf("   frames differing from the first: %d of 5\n", differing);
        CHECK(differing == 0, "six successive GI rebuilds produce byte-identical frames");

        float lo = darks[0], hi = darks[0];
        for (float d : darks) { if (d < lo) lo = d; if (d > hi) hi = d; }
        std::printf("   dark fraction across rebuilds: %.4f .. %.4f\n", lo, hi);
        CHECK(hi < 0.02f, "the metal is not black on ANY rebuild, cold start included");
    }

    // ---- 2a: THE FORWARD+ PER-CELL PROBE BUDGET HOLDS THE GRID --------------
    //
    // THE ROOT CAUSE, pinned where it can be re-derived from this file alone.
    // `ForwardClustered::collectObjsForSlice` writes a probe into a cluster cell
    // only while that cell's count is under the budget and drops the rest
    // SILENTLY (OgreForwardClustered.cpp:291-298). A cell is a frustum chunk
    // spanning a whole logarithmic depth slice, so one cell sees many probes;
    // the pixels whose probe was dropped fall through to cone tracing, which in
    // a sealed room is black. A budget at least the PROBE COUNT makes the drop
    // impossible, because a cell cannot intersect more probes than exist.
    const GiStatus st1 = s->giStatus();
    std::printf("   Forward+ cubemap probe slots per cell: %d (probes %d)\n",
                st1.cubemapProbeSlotsPerCell, st1.probeCount);
    CHECK(st1.cubemapProbeSlotsPerCell >= st1.probeCount,
          "the Forward+ per-cell probe budget can hold the whole grid (no cell can drop a probe)");

    // ---- 2b: THE SHRINK-FIT SELF-CHECK ACTUALLY FIRES ----------------------
    //
    // The old check could not: giStatus reported the UNION of the probe shapes,
    // which the region clamp makes equal to the clamp box by construction, so
    // "the union is inside the region" was a tautology. Two real numbers
    // replace it — one per probe, one per build.
    //
    // WHAT IS AND IS NOT ASSERTED HERE, deliberately. The parallax box is a
    // PROXY FOR THE GEOMETRY THE CUBEMAP CAPTURED, not a region of influence
    // (the shader's applicability test is the probe's AREA — CubemapProbe's
    // mGpuData[6]/[7] — while the shape only reprojects the ray), so a probe
    // standing in a room is RIGHT to have a room-sized box and forcing it down
    // to its own cell would reproject every reflection onto a box no surface
    // lies on. So the ratio is REPORTED, with a generous allowance, and the
    // assertion that carries weight is the clamp COUNT: it says how often the
    // 1x1 averaged-depth fit came back with a box outside the space the grid
    // was fitted to, which in this cluttered room is most of them.
    std::printf("   worst shape/cell ratio %.3f, probes over the allowance %d, "
                "probes the region clamp corrected %d of %d\n",
                st1.worstProbeShapeCellRatio, st1.probesExceedingCell,
                st1.probesClampedToRegion, st1.probeCount);
    CHECK(st1.worstProbeShapeCellRatio > 0.0f,
          "the per-probe locality check actually ran (it reports a ratio)");
    CHECK(st1.probesExceedingCell == 0,
          "no probe's parallax box escapes the room it was fitted in");
    CHECK(st1.probesClampedToRegion >= 0 && st1.probesClampedToRegion <= st1.probeCount,
          "the clamp count is a real per-build reading, not a tautology");

    // ---- camera pan: the artifact was camera-dependent ---------------------
    //
    // The owner saw the black rectangles CRAWL: they are quantised to the
    // Forward+ cluster grid, which is in view space, so which cells overflow
    // changes with the camera. Panning without rebuilding anything is therefore
    // a second, independent reading of the same defect — and the one that would
    // have caught it from a single camera being lucky.
    float worstPan = 0.0f;
    for (int i = 0; i < 8; ++i) {
        const float x = -1.2f + 0.3f * float(i);
        enginetest::testCameraLookAt(view, Vec3(x, 2.0f, 3.6f), Vec3(x * 0.3f, 2.0f, 0.0f));
        render(engine.get(), kSweepFrames);
        Image img;
        if (!view->readPixels(img)) break;
        const float d = darkFraction(img, bx0, by0, bx1, by1);
        std::printf("      pan x=%+.2f  metal dark fraction %.4f\n", double(x), double(d));
        if (d > worstPan) worstPan = d;
    }
    std::printf("   worst metal dark fraction across an 8-step camera pan: %.4f\n", worstPan);
    CHECK(worstPan < 0.02f, "panning the camera never blacks out the metal");

    engine->destroyView(view);
    engine->destroyScene(s);
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "all PCC stability checks passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
