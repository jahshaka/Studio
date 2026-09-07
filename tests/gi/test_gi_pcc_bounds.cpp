// GI BOUNDS AND PROBE PLACEMENT (REFLECTIONS_ADOPTION_SPEC.md §4 / P1a).
//
// Sibling of gi.pcc_mirror. That suite asks "does a probe reflection exist at
// all"; this one asks "is it placed WHERE THE SCENE IS", which is the question
// P4's finding 2 opened:
//
//     "thickening the room shell 0.4 -> 1.4 (with grown bounds) made the probe
//      reflection go BLACK while probeCount/pccBound stayed correct — probe
//      placement/shrink-fit loses the box"
//
// MEASURED ROOT CAUSE (this lane, 2026-09-07, RTX 4080S, Debug+ASan). It is not
// the box that gets lost, it is the ROOM. `PccPerPixelGridPlacement::
// setFullRegion` does not take a bounding box of the geometry; it takes the
// FREE SPACE the probes will live in — upstream's own sample hands it the
// interior cube's exact interior, half-size 0.5 for a 1x1x1 room, with no
// margin at all. We used to hand it the VOXEL volume: the geometry union plus
// 10% plus 0.5, or whatever a user typed into the bounds rows. With the room's
// geometry held byte-identical and ONLY that region changing:
//
//     region = geometry + 0.2   mirror pixel r = 0.251
//     region = geometry + 0.4                   0.063
//     region = geometry + 0.6                   0.000   <- "black"
//     region = geometry + 1.6                   0.000
//
// The chain: `buildEnd` shrink-fits each probe from ONE 1x1 averaged depth
// value per cube face, encoded as 0.5 * fDist / fApproxDist with fApproxDist
// measured to the REGION box; averaging that ratio over a 90-degree face is
// only well behaved while the region is close to the geometry. Once it is not,
// the fitted parallax boxes overshoot the room by many units (measured: a probe
// in a room spanning x in [-4,4] fitted to x in [-4.6, +7.7]), the PBS hybrid's
// `getPccVctBlendWeight` finds the probe's parallax hit and the VCT cone hit
// further apart than pccVctMinDistance, and hands the pixel to VCT — which in a
// sealed room has nothing to give, i.e. black. `probeCount` and `pccBound` stay
// perfectly healthy throughout, which is exactly why P4 could not see it.
//
// Two independent knobs were confirmed to move it, which is what identifies the
// mechanism rather than merely correlating with it: shrinking the probe region
// back onto the room restores it, and so does widening the PCC-vs-VCT trust
// window. The fix takes the first (computeProbeRegion), because it is the one
// that makes the probes CORRECT rather than merely trusted.
//
// A SECOND, INDEPENDENT DEFECT was measured on the way and is NOT fixed here
// (it is upstream shader code, and this tree is patches-only): with N probes
// covering a pixel, the hybrid path divides the probe reflection by N.
// `pccEnvS` is first normalised by the sum of probe fades — already a weighted
// average — and then divided AGAIN by `numProbesVctLerp`, the count of probes
// that passed the fade test (Samples/Media/Hlms/Pbs/Any/
// ForwardPlus_DecalsCubemaps_piece_ps.any; the non-VCT branch of the same piece
// has no such division). Measured on the identical pixel of this scene:
// 1 probe -> r = 1.000, 4 probes -> r = 0.251. That is why the thresholds below
// are 0.15 and not 0.9.
//
// Its own binary, like gi.pcc_mirror: the hybrid binds process-wide HlmsPbs
// state (sVctBindingOwner), so a GI scene must not share a process with
// gi.modes' scene (spec §9).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>

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

static void showBox(const char *what, const Vec3 &mn, const Vec3 &mx)
{
    std::printf("   %-26s %7.2f %7.2f %7.2f  ..  %7.2f %7.2f %7.2f\n",
                what, mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
}

static NodeId box(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale,
                  float metal = 0.0f, float rough = 0.9f)
{
    const NodeId n = enginetest::addTestCube(s, albedo, metal, rough);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

// ---------------------------------------------------------------------------
// Case 1: the finding-2 regression. The gi.pcc_mirror room, run at the shell
// thickness P4 reported (1.4) as well as the original 0.4, with automatic
// bounds and with explicit ones. Every combination must show the red wall in
// the mirror: probe PLACEMENT must stop caring what the lit volume says.
// ---------------------------------------------------------------------------
static void roomCase(Engine *engine, View *view, const char *label, float shell,
                     bool autoBounds, float margin)
{
    Scene *s = engine->createScene(std::string("room_") + label);
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // Interior x,z in [-4,4], y in [0,5]; the shell grows OUTWARD, so the room a
    // camera stands in is identical at every thickness and the only thing the
    // case varies is what the GI bounds say about it.
    const Colour white(0.85f, 0.85f, 0.85f), red(1.0f, 0.02f, 0.02f);
    const float h = shell * 0.5f, span = 8.0f + shell * 2.0f;
    box(s, white, Vec3(0.0f, -h, 0.0f),        Vec3(span, shell, span));
    box(s, white, Vec3(0.0f, 5.0f + h, 0.0f),  Vec3(span, shell, span));
    box(s, white, Vec3(0.0f, 2.5f, -4.0f - h), Vec3(span, 5.0f, shell));
    box(s, white, Vec3(-4.0f - h, 2.5f, 0.0f), Vec3(shell, 5.0f, span));
    box(s, white, Vec3( 4.0f + h, 2.5f, 0.0f), Vec3(shell, 5.0f, span));
    box(s, red,   Vec3(0.0f, 2.5f, 4.0f + h),  Vec3(span, 5.0f, shell));   // THE red wall
    box(s, Colour(1, 1, 1), Vec3(0.0f, 2.0f, 0.0f), Vec3(1.6f, 1.6f, 1.6f), 1.0f, 0.0f);
    enginetest::addDirectionalLight(s, Vec3(0.0f, -0.12f, 0.993f), 6.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 3.6f), Vec3(0.0f, 2.0f, 0.0f));

    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 2;
    gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
    if (!autoBounds) {
        const float outer = 4.0f + shell + margin;
        gi.boundsMin = Vec3(-outer, -shell - margin, -outer);
        gi.boundsMax = Vec3( outer, 5.0f + shell + margin, outer);
    }
    std::printf("-- %s (shell %.1f, %s)\n", label, shell,
                autoBounds ? "auto bounds" : "explicit bounds");
    const bool ok = s->setGlobalIllumination(gi);
    if (!ok) std::printf("   engine error: %s\n", engine->lastError().c_str());
    CHECK(ok, "the hybrid builds");
    render(engine);
    Image img;
    view->readPixels(img);
    const Colour m = img.at(64, 64);
    std::printf("   mirror pixel               r=%.3f g=%.3f b=%.3f\n", m.r, m.g, m.b);

    const GiStatus st = s->giStatus();
    showBox("lit volume", st.boundsMin, st.boundsMax);
    showBox("probe region", st.probeRegionMin, st.probeRegionMax);
    CHECK(st.probeCount == 4 && st.pccBound, "the 2x1x2 probe grid built and bound");

    // The regression itself.
    CHECK(m.r > m.g + 0.12f && m.r > 0.15f,
          "the mirror shows the RED WALL through the probes (P4 finding 2)");

    // ...and WHY it does: the probe region is the room's interior, not the lit
    // volume. Both interior faces within a shell thickness of the true room.
    CHECK(st.probeRegionMin.x > -4.0f - 0.01f && st.probeRegionMin.x < -4.0f + shell,
          "the probe region's -X face sits on the room's inner wall");
    CHECK(st.probeRegionMax.z < 4.0f + 0.01f && st.probeRegionMax.z > 4.0f - shell,
          "the probe region's +Z face sits on the room's inner wall");
    CHECK(st.probeRegionMin.y >= -0.01f && st.probeRegionMin.y < 0.5f,
          "the probe region's floor sits on the room's floor, not under it");
    // ...and it is INSIDE the lit volume, never the other way round.
    CHECK(st.probeRegionMin.x >= st.boundsMin.x && st.probeRegionMax.x <= st.boundsMax.x &&
          st.probeRegionMin.y >= st.boundsMin.y && st.probeRegionMax.y <= st.boundsMax.y &&
          st.probeRegionMin.z >= st.boundsMin.z && st.probeRegionMax.z <= st.boundsMax.z,
          "the probe region is contained in the lit volume");

    GiParams off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// The MEASURED LIMIT this suite deliberately does NOT assert, recorded here so
// the next reader does not rediscover it as a bug: a room whose LIT VOLUME the
// user pads out to roughly twice its size (explicit bounds +-7.9 around a +-5.4
// room shell) still loses its probe reflections — the probe region is placed
// perfectly, but the VCT cone hit drifts 16+ voxels from the probe's parallax
// hit and `getPccVctBlendWeight` correctly hands the pixel to the cone trace,
// which in a sealed room is black. Widening the blend window to ~32 voxels
// restores it (measured), but that is the hybrid abandoning its own judgement.
// The auto path can no longer produce such a volume (its margin is one voxel);
// the remaining route is a user typing it, and the honest answer is a panel
// warning off `world.giStatus()`, not a tuned constant. Reported as a residual.

// Case 2 (the spec's own acceptance test): the DEFAULT-SCENE shape — a ground
// plane far larger than anything on it. The lit volume must hug the primitives.
// ---------------------------------------------------------------------------
static void groundPlaneCase(Engine *engine, View *view)
{
    std::printf("-- ground plane vs primitives (auto bounds)\n");
    Scene *s = engine->createScene("ground");
    view->setScene(s);
    box(s, Colour(0.7f, 0.7f, 0.7f), Vec3(0, -0.1f, 0), Vec3(200.0f, 0.2f, 200.0f));  // the Ground
    box(s, Colour(0.8f, 0.2f, 0.2f), Vec3(-1.5f, 0.5f, 0), Vec3(1, 1, 1));
    box(s, Colour(0.2f, 0.8f, 0.2f), Vec3( 0.0f, 0.5f, 0), Vec3(1, 1, 1));
    box(s, Colour(0.2f, 0.2f, 0.8f), Vec3( 1.5f, 0.5f, 0), Vec3(1, 1, 1));
    enginetest::addDirectionalLight(s, Vec3(0.2f, -1.0f, 0.3f), 4.0f);

    GiParams gi;
    gi.mode = GiMode::Vct;         // no probes: this case is about the VOLUME
    gi.quality = GiQuality::Low;
    CHECK(s->setGlobalIllumination(gi), "VCT builds over the default-scene shape");
    const GiStatus st = s->giStatus();
    showBox("lit volume", st.boundsMin, st.boundsMax);
    // The ground is 200 across. Four items, median largest extent = 1, limit 4:
    // the ground is rejected and the primitives (x in [-2,2]) survive.
    CHECK(st.boundsMax.x - st.boundsMin.x < 10.0f,
          "the lit volume hugs the primitives instead of the 200-unit ground");
    CHECK(st.boundsMin.x <= -2.0f && st.boundsMax.x >= 2.0f,
          "...and still contains every primitive");
    GiParams off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// Case 3 (the spec's other acceptance test): a scene that IS one big object must
// NOT collapse. Below four items the outlier filter does not run at all —
// "the median" is not a statement about a population of three.
// ---------------------------------------------------------------------------
static void singleBigMeshCase(Engine *engine, View *view)
{
    std::printf("-- one big room mesh (the single-item fallback)\n");
    Scene *s = engine->createScene("single");
    view->setScene(s);
    box(s, Colour(0.7f, 0.7f, 0.7f), Vec3(0, 0, 0), Vec3(60.0f, 20.0f, 60.0f));
    enginetest::addDirectionalLight(s, Vec3(0.2f, -1.0f, 0.3f), 4.0f);

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Low;
    CHECK(s->setGlobalIllumination(gi), "VCT builds over a single large mesh");
    const GiStatus st = s->giStatus();
    showBox("lit volume", st.boundsMin, st.boundsMax);
    CHECK(st.boundsMax.x - st.boundsMin.x >= 60.0f,
          "a scene that IS one big mesh keeps the whole mesh in the lit volume");
    GiParams off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// Case 4: the deterministic escape hatch. THREE items, so the heuristic is
// switched off entirely and only the per-node flag can save the volume.
// ---------------------------------------------------------------------------
static void excludeFlagCase(Engine *engine, View *view)
{
    std::printf("-- the per-node exclude flag (three items: heuristic OFF)\n");
    Scene *s = engine->createScene("exclude");
    view->setScene(s);
    const NodeId ground = box(s, Colour(0.7f, 0.7f, 0.7f), Vec3(0, -0.1f, 0),
                              Vec3(200.0f, 0.2f, 200.0f));
    box(s, Colour(0.8f, 0.2f, 0.2f), Vec3(-1.0f, 0.5f, 0), Vec3(1, 1, 1));
    box(s, Colour(0.2f, 0.8f, 0.2f), Vec3( 1.0f, 0.5f, 0), Vec3(1, 1, 1));
    enginetest::addDirectionalLight(s, Vec3(0.2f, -1.0f, 0.3f), 4.0f);

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Low;
    CHECK(s->setGlobalIllumination(gi), "VCT builds with the ground included");
    GiStatus st = s->giStatus();
    showBox("lit volume, ground in", st.boundsMin, st.boundsMax);
    CHECK(st.boundsMax.x - st.boundsMin.x > 100.0f,
          "with three items the outlier heuristic does NOT run (the ground is in)");
    CHECK(!s->nodeGiBoundsExcluded(ground), "the flag starts off");

    s->setNodeGiBoundsExcluded(ground, true);
    CHECK(s->nodeGiBoundsExcluded(ground), "setNodeGiBoundsExcluded reads back");
    // The flag invalidates the GI caches; the flush is at frame time, like every
    // other geometry change.
    render(engine, 2);
    st = s->giStatus();
    showBox("lit volume, ground out", st.boundsMin, st.boundsMax);
    CHECK(st.boundsMax.x - st.boundsMin.x < 10.0f,
          "excluding the ground pulls the lit volume onto the two boxes");
    CHECK(st.boundsMin.x <= -1.5f && st.boundsMax.x >= 1.5f, "...and still contains them");

    // The ground is EXCLUDED FROM THE BOUNDS, not from GI: it must still be one
    // of the items handed to the voxelizer, so bounced light still comes off it.
    // Proven negatively, which is all this suite can do cheaply: turning the flag
    // back off restores the old volume exactly, i.e. the flag is a pure filter
    // with no side effect on the item set.
    s->setNodeGiBoundsExcluded(ground, false);
    render(engine, 2);
    st = s->giStatus();
    CHECK(st.boundsMax.x - st.boundsMin.x > 100.0f,
          "clearing the flag restores the ground to the bounds");

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
    cfg.logFile = "test-gi-pcc-bounds-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("bounds", 128, 128, Colour(0, 0, 0));

    groundPlaneCase(engine.get(), view);
    singleBigMeshCase(engine.get(), view);
    excludeFlagCase(engine.get(), view);
    // The hybrid cases last: they take the process-wide HlmsPbs binding.
    roomCase(engine.get(), view, "thin_snug",  0.4f, false, 0.2f);
    roomCase(engine.get(), view, "thick_auto", 1.4f, true,  0.0f);
    roomCase(engine.get(), view, "thick_snug", 1.4f, false, 0.2f);

    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
