// gi.cascade_determinism — THE SAME SCENE VOXELISES TO THE SAME PICTURE, whatever
// the renderer did on the way there (fork ad452604a+155a56bf8 (was 0061 and 0062); PHOTON_SPEC §7
// E2 round 2).
//
// TWO DEFECTS MADE THIS FALSE, and both are in the pin:
//
//   0061 — THE OCTANTS DID NOT FOLLOW THE REGION. `dividideOctants()` copies the
//     region into every octant, `build()` culls items against that copy AND
//     passes its minimum as the voxelisation shader's world origin, and
//     `setRegionToVoxelize()` replaced the region without touching them. So a
//     cascade that SCROLLED voxelised the geometry of the box it used to cover,
//     at the origin it used to have, into a texture the shader maps onto the box
//     it covers now. Measured as 42,42,42 against 47,47,47 on the same document
//     — the session that built the scene against the session that reopened it —
//     with nothing in the document, the probes, the field, the boxes, the item
//     counts or the attach sets differing at all.
//
//   0062 — THE DISPATCH ORDER WAS THE ALLOCATOR'S. The voxelisation's per-voxel
//     merge writes the double-sided flag as the LAST dispatch's answer (an
//     upstream `max(x, x)` typo), and the dispatch order was a std::map keyed on
//     buffer POINTERS. The same scene in two processes = two orders = two sets of
//     voxels, and the flag is read per light (`abs(NdotL)` against
//     `saturate(NdotL)`), so it is a systematically brighter or darker junction.
//
// WHAT THIS SUITE ASSERTS, and why each case is the shape it is:
//
//   1. A CASCADE SCROLLED TO A PLACE IS THE CASCADE BUILT AT IT. The camera is
//      moved far enough to re-centre the chain, then the whole arm is rebuilt
//      from scratch at that same pose: the two pictures must agree. This is 0061
//      and it is the case that reds on the pre-patch pin.
//   2. THE SAME SCENE VOXELISED TWICE IS THE SAME PICTURE — the idempotence the
//      first case depends on, asserted on its own so a failure says which.
//   3. THE ATTACH ORDER DOES NOT DECIDE THE PICTURE. The items are attached in
//      the reverse order and the chain rebuilt: same picture. This is 0062's
//      shape — order-independence — expressed the one way a suite in this process
//      can express it (two allocation orders in two processes cannot be staged
//      from inside one, so the order that CAN be varied is the one we control).
//   4. ...AND NOT WHEN IT DECIDES THE DISPATCH PARTITION EITHER (fork ad452604a+155a56bf8+0338ca7f2+c4c80b5f7
//      (was 0065)). Case 3's four objects fit one material pool, so the order only
//      varies what happens INSIDE one dispatch. Two hundred objects each owning
//      a material — what the editor actually produces — spread over several
//      pools, and which pool a material lands in is its creation order, so the
//      attach order decides WHICH DISPATCH each object is voxelised by. That is
//      what fork ad452604a+155a56bf8 (was 0062) bought determinism from by giving every material its own
//      dispatch (86 -> 321 ms on a dense scene) and what 0065 fixes at the cause
//      instead: the per-voxel merge accumulates exact integer sums and resolves
//      them once, so no partition and no order changes a voxel.
//
// Its own binary like every GI suite.
// PHOTON-GATHER-1d: THE GATHER PINNED OFF. Since 1d the screen-probe gather is
// the diffuse at every ray tier (GiToggle::Auto resolves on at Medium and above);
// this suite measures the voxel chain / the field / the cones / the probes, which
// it pins, so its numbers stay about them. The gather has its own suites
// (gi.gather_*).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "../support/timingbars.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                     \
    } while (0)

static const unsigned kSize = 128;

static void render(Engine *e, int frames = 1)
{
    enginetest::frames(e, frames);
}

static GiParams chainGi()
{
    GiParams gi;
    gi.gather = GiToggle::Off;   // PHOTON-GATHER-1d (the header)
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;     // the CONE-traced diffuse: the voxels, read directly
    gi.updateBudget = 0;         // no probe work
    return gi;
}

/// HOW FAR APART TWO PICTURES ARE, per PIXEL and not as a mean. A mean is the
/// wrong instrument here: the defects above are systematic but LOCAL — a stale
/// octant is wrong where the box moved, a lost double-sided flag is wrong at the
/// junctions — and averaging over 16,384 pixels divides a real 5/255 down into
/// the noise. The worst pixel is what "the same picture" means.
static float apart(const Image &a, const Image &b)
{
    float worst = 0.0f;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Colour ca = a.at(x, y), cb = b.at(x, y);
            worst = std::max(worst, std::fabs(ca.r - cb.r));
            worst = std::max(worst, std::fabs(ca.g - cb.g));
            worst = std::max(worst, std::fabs(ca.b - cb.b));
        }
    return worst;
}
/// The mean too, printed beside it — it is what a reader compares across runs.
static float meanLum(const Image &img)
{
    float s = 0.0f;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Colour c = img.at(x, y);
            s += 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
        }
    return s / float(kSize * kSize);
}
/// 1/255 in linear terms at the mid-grey these pictures sit at. Two renders of the
/// same voxels are bit-identical; this is the margin for the dither/rounding a
/// re-render can carry, and every defect above is far outside it.
static const float kOneOf255 = 1.0f / 255.0f;
/// THE FLOOR OF AN ORDER TEST, and it is a property of the data rather than a
/// concession: the voxel textures are 8-BIT, and the per-voxel merge averages
/// into them (`mixAverage3/4`) — an average of the same numbers in a different
/// order is the same value only up to that quantisation. So two attach orders can
/// differ by ONE step of the voxel's own precision and be the same answer; they
/// cannot differ by more. Measured on this scene: 6/255 before fork ad452604a+155a56bf8 (was 0062),
/// 1/255 after.
static const float kOneVoxelStep = 1.6f / 255.0f;

// =========================================================================================
// gi.scroll_slab (--scroll-slab) — A SCROLLED CASCADE IS, BIT FOR BIT, THE CASCADE BUILT WHERE IT
// IS NOW, AND IT COST A SLAB (ENGINE-UPGRADE-U5 stage 1).
//
// A camera scroll keeps the voxels the new box shares with the old one (copied, shifted; the surface
// position re-resolved at its new cell from the merge's own sums) and voxelises only the entering
// slab, spread over frames by the per-frame budget, committed when the slab is complete. So:
//   A. THE STORE: a scroll by (k, j, -i) cells - three slabs, none a multiple of the dispatch's 4
//      (the bounds guard) - leaves cascade 0's voxeliser store (albedo, normal, the coverage and the
//      surface position per half: Scene::giVoxelVolume) EQUAL, float for float, to a whole build at
//      the same placement. The light is not compared: a scroll injects over the light the cascade
//      held (LAMPREST-3's rule, unchanged), a from-scratch build over none.
//   B. THE SPREAD: the same with the budget tiny (gi.scrollBudgetMs) - the slab cut into many chunks
//      over many frames, the cascade's placement and its readers' box UNCHANGED until the commit,
//      GI not at rest meanwhile, and the committed store again a whole build's.
//   C. AN EDIT DURING THE SPREAD that reaches only the box being built (outside the live one, which
//      the dirty path tests) drops the scroll and the next one builds the edited scene: the store is
//      again a whole build's of the scene as it now stands.
//   D. THE COST, on a lattice at 128 cells (the High tier): the voxelisation of a scroll by the
//      shipped step (its rows vct.voxel0 + vct.scroll0) <= 0.35 of a whole build of cascade 0
//      (vct.voxel0 of an edit's rebuild) - decision 1's bar for a slab of 26 % of the volume; GPU
//      time, paired in this process, a millisecond bar (gi.scroll_slab.timing asserts it).
// A, B and C run a two-cascade table at 64 cells (a dyadic cell, 10 m / 64: the arithmetic every
// shipped table has), so a store read is 0.26 M voxels.
static bool sameStore(const GiVoxelVolume &a, const GiVoxelVolume &b, const char *what)
{
    if (!a.available || !b.available || a.width != b.width || a.height != b.height || a.depth != b.depth) {
        std::printf("   %s: a store is missing or the sizes differ\n", what);
        return false;
    }
    for (int i = 0; i < 3; ++i)
        if (a.origin[i] != b.origin[i] || a.cell[i] != b.cell[i]) {
            std::printf("   %s: the placement differs (axis %d: origin %.6f vs %.6f)\n", what, i,
                        double(a.origin[i]), double(b.origin[i]));
            return false;
        }
    struct Arr { const char *name; const std::vector<float> *x, *y; };
    const Arr arrs[] = { { "albedo", &a.albedo, &b.albedo }, { "normal", &a.normal, &b.normal },
                         { "coverageP", &a.coverageP, &b.coverageP }, { "coverageN", &a.coverageN, &b.coverageN },
                         { "positionP", &a.positionP, &b.positionP }, { "positionN", &a.positionN, &b.positionN } };
    bool same = true;
    for (const Arr &r : arrs) {
        if (r.x->size() != r.y->size() || r.x->empty()) {
            std::printf("   %s: %s sizes %zu vs %zu\n", what, r.name, r.x->size(), r.y->size());
            same = false;
            continue;
        }
        size_t diff = 0, first = size_t(-1);
        for (size_t i = 0; i < r.x->size(); ++i)
            if (std::memcmp(&(*r.x)[i], &(*r.y)[i], sizeof(float)) != 0) {
                if (first == size_t(-1)) first = i;
                ++diff;
            }
        if (diff) {
            const size_t v = first / 4u;
            std::printf("   %s: %s differs in %zu of %zu floats (first at voxel %zu,%zu,%zu: %.7f vs %.7f)\n",
                        what, r.name, diff, r.x->size(), v % size_t(a.width), (v / size_t(a.width)) % size_t(a.height),
                        v / size_t(a.width * a.height), double((*r.x)[first]), double((*r.y)[first]));
            same = false;
        }
    }
    size_t lit = 0;
    for (size_t i = 3; i < a.albedo.size(); i += 4) lit += a.albedo[i] > 0.0f ? 1u : 0u;
    std::printf("   %s: %s (%zu occupied voxels)\n", what, same ? "the stores are equal" : "THE STORES DIFFER", lit);
    return same;
}

/// THE SPREAD ARMS' BUDGET: small enough that the fixture's slab is cut into many chunks over many
/// frames (B asserts >= 8), large enough that the cut stops at layers rather than single groups, so a
/// spread is tens of frames, not hundreds (measured on this fixture: B's scroll takes ~20 frames).
static const double kSpreadBudgetMs = 0.01;

static int scrollSlabMain()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-scroll-slab-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    int frames = 1;
    const auto run = [&](int n) { render(e, n); frames += n; };

    View *view = e->createOffscreenView("scroll", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("scroll");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.05f, 0.05f, 0.05f));
    // The room of the suite's other cases, plus objects in and around the box the walk enters:
    // a junction (two walls on a floor), two-sided plates, and props on both sides of the step.
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.85f, 0.85f, 0.85f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(60.0f, 0.1f, 60.0f));
    const NodeId wallB = enginetest::addTestCube(scene, Colour(0.1f, 0.9f, 0.2f), 0.0f, 0.8f);
    enginetest::setNodePosition(scene, wallB, Vec3(-4.0f, 2.5f, 0.0f));
    enginetest::setNodeScale(scene, wallB, Vec3(0.3f, 5.0f, 12.0f));
    struct Prop { Vec3 pos, scale; Colour albedo; };
    const Prop props[] = {
        { Vec3(1.2f, 0.7f, 1.0f),  Vec3(1.0f, 1.0f, 1.0f),   Colour(0.2f, 0.3f, 0.95f) },
        { Vec3(5.6f, 0.5f, 0.3f),  Vec3(0.9f, 1.0f, 0.7f),   Colour(0.9f, 0.6f, 0.1f) },
        { Vec3(7.3f, 1.4f, -1.7f), Vec3(0.4f, 2.8f, 0.4f),   Colour(0.7f, 0.2f, 0.7f) },
        { Vec3(6.1f, 2.0f, 2.6f),  Vec3(1.7f, 0.05f, 1.3f),  Colour(0.3f, 0.8f, 0.8f) },   // a plate
        { Vec3(6.1f, 2.3f, 2.6f),  Vec3(1.7f, 0.05f, 1.3f),  Colour(0.8f, 0.8f, 0.3f) },   // its twin
        { Vec3(3.4f, 4.6f, -0.9f), Vec3(2.2f, 0.3f, 0.6f),   Colour(0.6f, 0.6f, 0.6f) },
        { Vec3(-1.8f, 1.1f, -2.4f),Vec3(0.6f, 2.2f, 0.6f),   Colour(0.9f, 0.1f, 0.1f) },
    };
    std::vector<NodeId> propNodes;
    for (const Prop &p : props) {
        const NodeId n = enginetest::addTestCube(scene, p.albedo, 0.0f, 0.6f);
        enginetest::setNodePosition(scene, n, p.pos);
        enginetest::setNodeScale(scene, n, p.scale);
        propNodes.push_back(n);
    }
    enginetest::addDirectionalLight(scene, Vec3(0.3f, -1.0f, -0.5f), 5.0f);

    GiParams small = chainGi();
    small.cascadeCount = 2;
    small.cascadeSet[0] = GiParams::GiCascadeDesc{ 5.0f, 64, 0.0f };
    small.cascadeSet[1] = GiParams::GiCascadeDesc{ 20.0f, 64, 0.0f };
    const Vec3 home(0.0f, 2.0f, 2.0f);
    Vec3 cam = home;
    const auto look = [&](const Vec3 &p) {
        cam = p;
        enginetest::testCameraLookAt(view, p, Vec3(p.x + 1.0f, 1.0f, p.z - 4.0f));
    };
    look(home);
    CHECK(scene->setGlobalIllumination(small), "a two-cascade chain at 64 cells builds");
    run(8);
    GiStatus st = scene->published().gi;
    if (st.cascades.size() != 2u) { std::printf("FAIL: the chain has %zu cascades\n", st.cascades.size()); return 1; }
    const float cell = st.cascades[0].cell, step = st.cascades[0].step;
    std::printf("   cascade 0: %d^3, cell %.5f m, step %.3f m\n", st.cascades[0].resolution, double(cell),
                double(step));

    const auto rebuildWhole = [&]() {
        GiParams off; off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        run(2);
        scene->setGlobalIllumination(small);
        run(8);
    };
    // Frames until cascade 0 has committed `want` scrolls (or `cap` frames).
    const auto untilScrolls = [&](unsigned long long want, int cap) {
        int n = 0;
        for (; n < cap && scene->published().gi.cascades[0].scrolls < want; ++n) run(1);
        return n;
    };
    GiVoxelVolume scrolled, built;

    // ---- A. one frame, three slabs -----------------------------------------------------------
    std::printf("\n== A: a scroll by three slabs is the store a whole build writes ==\n");
    {
        const GiStatus pre = scene->published().gi;
        const Vec3 to(home.x + 1.5f * step, home.y + 9.4f * cell, home.z - 13.3f * cell);
        look(to);
        const int n = untilScrolls(pre.cascades[0].scrolls + 1u, 8);
        const GiStatus post = scene->published().gi;
        const int sx = int(std::lround((post.cascades[0].centre.x - pre.cascades[0].centre.x) / cell));
        const int sy = int(std::lround((post.cascades[0].centre.y - pre.cascades[0].centre.y) / cell));
        const int sz = int(std::lround((post.cascades[0].centre.z - pre.cascades[0].centre.z) / cell));
        std::printf("   committed after %d frame(s); shift (%d, %d, %d) cells; scroll frames %llu; rebuilds %llu -> %llu\n",
                    n, sx, sy, sz, post.cascades[0].scrollFrames, pre.cascades[0].rebuilds, post.cascades[0].rebuilds);
        CHECK(post.cascades[0].scrolls == pre.cascades[0].scrolls + 1u, "THE CAMERA'S STEP TOOK THE SLAB PATH");
        CHECK(post.cascades[0].rebuilds == pre.cascades[0].rebuilds + 1u, "...as one rebuild of cascade 0");
        CHECK(sx != 0 && sy != 0 && sz != 0, "...on all three axes (three slabs)");
        CHECK(sx % 4 != 0 && sy % 4 != 0 && sz % 4 != 0, "...none a multiple of the dispatch's four (the bounds guard)");
        CHECK(n == 1, "...in the frame the step was seen (the default budget holds a fixture's slab)");
        CHECK(scene->giVoxelVolume(0, scrolled), "the scrolled store reads back");
        rebuildWhole();
        CHECK(scene->giVoxelVolume(0, built), "the whole build's store reads back");
        CHECK(sameStore(scrolled, built, "A"), "A SCROLLED STORE IS THE WHOLE BUILD'S, FLOAT FOR FLOAT");
    }

    // ---- B. the spread ---------------------------------------------------------------------------
    std::printf("\n== B: the slab spread over frames commits the same store ==\n");
    {
        e->setArm("gi.scrollBudgetMs", kSpreadBudgetMs);
        run(1);
        const GiStatus pre = scene->published().gi;
        const Vec3 from = cam;
        const Vec3 to(from.x - 1.5f * step, from.y - 5.2f * cell, from.z + 6.6f * cell);
        look(to);
        int flight = 0;
        bool placedStill = true, notAtRest = true, sawFlight = false;
        for (; flight < 400 && scene->published().gi.cascades[0].scrolls == pre.cascades[0].scrolls; ++flight) {
            run(1);
            const GiStatus mid = scene->published().gi;
            if (mid.cascades[0].scrolls != pre.cascades[0].scrolls) break;
            sawFlight = sawFlight || mid.cascades[0].scrolling;
            placedStill = placedStill && mid.cascades[0].centre.x == pre.cascades[0].centre.x &&
                          mid.cascades[0].centre.y == pre.cascades[0].centre.y &&
                          mid.cascades[0].centre.z == pre.cascades[0].centre.z;
            notAtRest = notAtRest && !mid.giAtRest;
        }
        const GiStatus post = scene->published().gi;
        const unsigned long long spent = post.cascades[0].scrollFrames - pre.cascades[0].scrollFrames;
        std::printf("   committed after %d frame(s), %llu of them building chunks\n", flight + 1, spent);
        CHECK(post.cascades[0].scrolls == pre.cascades[0].scrolls + 1u, "the spread scroll commits");
        CHECK(sawFlight && spent >= 8u, "...SPREAD over many frames by the budget (>= 8)");
        CHECK(placedStill, "...the cascade's placement - its readers' box - unchanged until the commit");
        CHECK(notAtRest, "...and GI not at rest while the scroll is owed");
        e->setArm("gi.scrollBudgetMs", 0.0);
        CHECK(scene->giVoxelVolume(0, scrolled), "the spread store reads back");
        rebuildWhole();
        CHECK(scene->giVoxelVolume(0, built), "the whole build's store reads back");
        CHECK(sameStore(scrolled, built, "B"), "A SPREAD SCROLL'S STORE IS THE WHOLE BUILD'S, FLOAT FOR FLOAT");
    }

    // ---- C. an edit in the box being built -------------------------------------------------------
    std::printf("\n== C: an edit reaching only the box being built drops the scroll ==\n");
    {
        e->setArm("gi.scrollBudgetMs", kSpreadBudgetMs);
        run(1);
        const GiStatus pre = scene->published().gi;
        const Vec3 from = cam;
        const Vec3 to(from.x + 1.5f * step, from.y, from.z);
        look(to);
        run(3);   // in flight: a few chunks built
        const bool flying = scene->published().gi.cascades[0].scrolling;
        // The prop beyond the live box's +x face, inside the target's: the dirty path's box test
        // (the live box) does not reach it.
        const float liveMaxX = pre.cascades[0].centre.x + pre.cascades[0].halfSize;
        enginetest::setNodePosition(scene, propNodes[2], Vec3(liveMaxX + 0.6f * step, 1.0f, from.z - 1.0f));
        const int n = untilScrolls(pre.cascades[0].scrolls + 1u, 400);
        const GiStatus post = scene->published().gi;
        std::printf("   in flight when edited: %d; committed %d frame(s) later; aborts %llu -> %llu\n",
                    int(flying), n, pre.cascadeScrollAborts, post.cascadeScrollAborts);
        CHECK(flying, "the scroll was in flight when the edit landed");
        CHECK(post.cascadeScrollAborts > pre.cascadeScrollAborts, "THE EDIT DROPPED THE SCROLL");
        CHECK(post.cascades[0].scrolls == pre.cascades[0].scrolls + 1u, "...and a scroll committed after it");
        e->setArm("gi.scrollBudgetMs", 0.0);
        run(2);
        CHECK(scene->giVoxelVolume(0, scrolled), "the store reads back");
        rebuildWhole();
        CHECK(scene->giVoxelVolume(0, built), "the whole build's store reads back");
        CHECK(sameStore(scrolled, built, "C"), "THE COMMITTED STORE HOLDS THE EDITED SCENE, AS A WHOLE BUILD DOES");
    }
    scene->removeNode(ground);
    scene->removeNode(wallB);
    for (NodeId n : propNodes) scene->removeNode(n);
    run(2);

    // ---- D. the cost at 128 cells ----------------------------------------------------------------
    std::printf("\n== D: a scroll's voxelisation against a whole build's, 128 cells (High) ==\n");
    {
        // A LATTICE that fills cascade 0's box along the whole walk (x from -4.8 to 13.2 m, the
        // camera's three steps reaching x = 7.9 and its box 5 m past that): 16 x 5 x 9 cubes, 1.2 m
        // apart - every slab enters geometry, and voxelising is what a build costs, not the clears.
        std::vector<NodeId> lattice;
        for (int x = 0; x < 16; ++x)
            for (int y = 0; y < 5; ++y)
                for (int z = 0; z < 9; ++z) {
                    const NodeId n = enginetest::addTestCube(scene, Colour(0.3f + 0.07f * float(x), 0.5f,
                                                                           0.3f + 0.07f * float(z)), 0.0f, 0.7f);
                    enginetest::setNodePosition(scene, n, Vec3(-4.8f + 1.2f * float(x), 0.3f + 1.2f * float(y),
                                                              -4.8f + 1.2f * float(z)));
                    enginetest::setNodeScale(scene, n, Vec3(0.5f, 0.5f, 0.5f));
                    lattice.push_back(n);
                }
        look(Vec3(0.0f, 2.0f, 0.0f));
        CHECK(scene->setGlobalIllumination(chainGi()), "the High chain over the lattice");
        run(12);
        const GiStatus hi = scene->published().gi;
        const float hStep = hi.cascades[0].step;
        std::printf("   cascade 0: %d^3, cell %.4f m, step %.3f m (%.0f cells)\n", hi.cascades[0].resolution,
                    double(hi.cascades[0].cell), double(hStep), double(hStep / hi.cascades[0].cell));
        e->setFrameMonitor(MonitorLevel::Review);
        std::vector<double> scrollGpu, wholeGpu;
        float x = 0.0f;
        for (int rep = 0; rep < 3; ++rep) {
            // A SCROLL by the shipped step, then a WHOLE rebuild of cascade 0 at the same place (an
            // edit inside its box: one lattice cube nudged and put back).
            std::vector<MonitorRecord> recs;
            e->takeMonitorRecords(recs);
            x += hStep * 1.02f;
            look(Vec3(x, 2.0f, 0.0f));
            run(12);
            e->takeMonitorRecords(recs);
            double s = 0.0; bool timed = false;
            for (const MonitorRecord &r : recs)
                for (const CacheWork &w : r.cacheWork)
                    if ((w.detail == "vct.voxel0" || w.detail == "vct.scroll0") && w.gpuMs >= 0.0f) { s += w.gpuMs; timed = true; }
            if (timed) scrollGpu.push_back(s);
            const NodeId nudge = lattice[size_t(4 * 45 + 2 * 9 + 4)];   // the cube at (0, 2.7, 0)
            enginetest::setNodePosition(scene, nudge, Vec3(x + 0.05f, 2.7f, 0.0f));
            run(12);
            e->takeMonitorRecords(recs);
            double w0 = -1.0;
            for (const MonitorRecord &r : recs)
                for (const CacheWork &w : r.cacheWork)
                    if (w.detail == "vct.voxel0" && w.gpuMs >= 0.0f) w0 = std::max(w0, double(w.gpuMs));
            if (w0 >= 0.0) wholeGpu.push_back(w0);
        }
        e->setFrameMonitor(MonitorLevel::Off);
        const GiStatus end = scene->published().gi;
        std::sort(scrollGpu.begin(), scrollGpu.end());
        std::sort(wholeGpu.begin(), wholeGpu.end());
        const double ms = scrollGpu.empty() ? -1.0 : scrollGpu[scrollGpu.size() / 2];
        const double mw = wholeGpu.empty() ? -1.0 : wholeGpu[wholeGpu.size() / 2];
        std::printf("   cascade 0 scrolls %llu; GPU ms per scroll (median of %zu) %.3f, per whole build (median of %zu) %.3f"
                    " -> %.3f of a whole build\n", end.cascades[0].scrolls, scrollGpu.size(), ms, wholeGpu.size(), mw,
                    mw > 0.0 ? ms / mw : -1.0);
        CHECK(end.cascades[0].scrolls >= hi.cascades[0].scrolls + 3u, "every step took the slab path");
        CHECK(!scrollGpu.empty() && !wholeGpu.empty(), "the monitor timed both on the GPU");
        JAH_TIMING_CHECK("gi.scroll_slab", ms >= 0.0 && mw > 0.0 && ms <= 0.35 * mw,
                         "A SCROLL BY THE SHIPPED STEP VOXELISES IN <= 0.35 OF A WHOLE BUILD AT 128 CELLS");
        for (NodeId n : lattice) scene->removeNode(n);
    }
    GiParams off; off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    run(2);
    std::printf("\n   frames rendered: %d\n", frames);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--scroll-slab") return scrollSlabMain();
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-cascade-determinism-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();

    View *view = e->createOffscreenView("determinism", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("determinism");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.05f, 0.05f, 0.05f));

    // A room with JUNCTIONS — a floor, two walls that meet it and a prop — because
    // the merge defect lives exactly where two dispatches write the same voxel.
    // Several materials, so the voxeliser has several buckets to order.
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.85f, 0.85f, 0.85f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(60.0f, 0.1f, 60.0f));
    const NodeId wallA = enginetest::addTestCube(scene, Colour(0.9f, 0.1f, 0.1f), 0.0f, 0.8f);
    enginetest::setNodePosition(scene, wallA, Vec3(0.0f, 2.5f, -5.0f));
    enginetest::setNodeScale(scene, wallA, Vec3(12.0f, 5.0f, 0.3f));
    const NodeId wallB = enginetest::addTestCube(scene, Colour(0.1f, 0.9f, 0.2f), 0.0f, 0.8f);
    enginetest::setNodePosition(scene, wallB, Vec3(-5.0f, 2.5f, 0.0f));
    enginetest::setNodeScale(scene, wallB, Vec3(0.3f, 5.0f, 12.0f));
    const NodeId prop = enginetest::addTestCube(scene, Colour(0.2f, 0.3f, 0.95f), 0.0f, 0.4f);
    enginetest::setNodePosition(scene, prop, Vec3(1.2f, 0.7f, 1.0f));
    enginetest::addDirectionalLight(scene, Vec3(0.3f, -1.0f, -0.5f), 5.0f);

    const Vec3 home(0.0f, 3.0f, 8.0f), look(0.0f, 1.0f, -2.0f);
    enginetest::testCameraLookAt(view, home, look);

    CHECK(scene->setGlobalIllumination(chainGi()), "the cascade chain builds");
    render(e, 8);
    GiStatus st = scene->published().gi;
    CHECK(!st.cascades.empty(), "the chain is up");
    if (st.cascades.empty()) return 1;
    // THE WALK IS MEASURED IN THE OUTERMOST CASCADE'S STEP, not the innermost's.
    // Every cascade must actually RE-CENTRE for this suite to be about scrolling,
    // and the outer one steps furthest (its step is the coarsest by construction:
    // 15 m against cascade 0's 5 on the shipped table). A walk that only moves the
    // inner cascades measures a third of the arm.
    const float stepOut = st.cascades.back().step;
    std::printf("   %zu cascades, the outermost steps every %.2f m\n", st.cascades.size(), stepOut);

    const auto picture = [&]() {
        render(e, 6);
        Image img;
        view->readPixels(img);
        return img;
    };
    const auto rebuildWhole = [&]() {
        GiParams off; off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        render(e, 2);
        scene->setGlobalIllumination(chainGi());
        render(e, 8);
    };

    // =====================================================================
    // CASE 1 — A CASCADE SCROLLED TO A PLACE IS THE CASCADE BUILT AT IT
    // =====================================================================
    std::printf("\n== case 1: scrolled to a place == built at it (fork ad452604a (was 0061)) ==\n");
    {
        // WALKED AWAY FROM THE GEOMETRY, AND LOOKING BACK AT IT. Distance alone
        // proves nothing here: this scene is symmetric about the origin, so a
        // cascade that voxelised the box it used to cover would cover nearly the
        // same THINGS and the two pictures would agree by luck. The walk is
        // therefore out of the inner cascades' reach of the walls and the prop
        // (they are all within 6 m of the origin, and cascade 0 covers 5), with
        // the camera turned back on them: what the near cascades hold after the
        // scroll is then a statement about the box they are actually on.
        const float walk = stepOut * 2.0f;                 // 30 m on the shipped table
        enginetest::testCameraLookAt(view, Vec3(home.x + walk, home.y, home.z), look);
        render(e, 40);                       // one cascade per frame; the queue drains
        const GiStatus scrolled = scene->published().gi;
        const Image afterScroll = picture();

        rebuildWhole();                      // the same pose, built from scratch
        enginetest::testCameraLookAt(view, Vec3(home.x + walk, home.y, home.z), look);
        const GiStatus built = scene->published().gi;
        const Image afterBuild = picture();

        std::printf("   scrolled mean %.5f (c0 rebuilds %llu)\n", meanLum(afterScroll),
                    scrolled.cascades[0].rebuilds);
        std::printf("   built    mean %.5f (c0 rebuilds %llu)\n", meanLum(afterBuild),
                    built.cascades[0].rebuilds);
        std::printf("   apart %.5f (%.2f of 255)\n", apart(afterScroll, afterBuild),
                    apart(afterScroll, afterBuild) * 255.0f);
        CHECK(scrolled.cascades[0].rebuilds > built.cascades[0].rebuilds,
              "the scrolled arm really did re-voxelise more than the fresh one");
        CHECK(apart(afterScroll, afterBuild) <= kOneOf255,
              "A CASCADE SCROLLED TO A PLACE RENDERS WHAT A CASCADE BUILT AT IT RENDERS");
    }

    // =====================================================================
    // CASE 2 — THE SAME SCENE VOXELISED TWICE IS THE SAME PICTURE
    // =====================================================================
    std::printf("\n== case 2: two builds of one scene agree ==\n");
    {
        const Image first = picture();
        rebuildWhole();
        const Image second = picture();
        std::printf("   apart %.5f (%.2f of 255)\n", apart(first, second),
                    apart(first, second) * 255.0f);
        CHECK(apart(first, second) <= kOneOf255, "two builds of one scene are one picture");
    }

    // =====================================================================
    // CASE 3 — THE ATTACH ORDER DOES NOT DECIDE THE PICTURE
    // =====================================================================
    // The order the voxeliser receives its items in is the order the engine walks
    // its item index, and the engine's own order is stable — so the way to vary it
    // from a suite is to make the SCENE's order different and rebuild. Detaching
    // and re-attaching the meshes in reverse re-registers them in the opposite
    // order; a chain built on that must render the same scene.
    std::printf("\n== case 3: the attach order does not decide the picture (0062) ==\n");
    {
        const Image before = picture();
        // Re-create the four meshes back to front. removeNode + addTestCube is the
        // engine's own route, so the item index really is rebuilt in reverse.
        struct Rebuilt { Vec3 pos, scale; Colour albedo; float rough; };
        const Rebuilt again[4] = {
            { Vec3(1.2f, 0.7f, 1.0f),   Vec3(1, 1, 1),        Colour(0.2f, 0.3f, 0.95f), 0.4f },
            { Vec3(-5.0f, 2.5f, 0.0f),  Vec3(0.3f, 5, 12),    Colour(0.1f, 0.9f, 0.2f),  0.8f },
            { Vec3(0.0f, 2.5f, -5.0f),  Vec3(12, 5, 0.3f),    Colour(0.9f, 0.1f, 0.1f),  0.8f },
            { Vec3(0.0f, -0.05f, 0.0f), Vec3(60, 0.1f, 60),   Colour(0.85f, 0.85f, 0.85f), 0.9f },
        };
        scene->removeNode(prop); scene->removeNode(wallB);
        scene->removeNode(wallA); scene->removeNode(ground);
        render(e, 2);
        for (const Rebuilt &r : again) {
            const NodeId n = enginetest::addTestCube(scene, r.albedo, 0.0f, r.rough);
            enginetest::setNodePosition(scene, n, r.pos);
            enginetest::setNodeScale(scene, n, r.scale);
        }
        render(e, 2);
        rebuildWhole();
        const Image reversed = picture();
        std::printf("   apart %.5f (%.2f of 255)\n", apart(before, reversed),
                    apart(before, reversed) * 255.0f);
        CHECK(apart(before, reversed) <= kOneVoxelStep,
              "THE SAME GEOMETRY ATTACHED IN THE OPPOSITE ORDER IS THE SAME PICTURE "
              "(to the voxel's own 8-bit step)");
    }

    // =====================================================================
    // CASE 4 — TWO HUNDRED OBJECTS, EACH WITH ITS OWN MATERIAL, IN TWO ORDERS
    // =====================================================================
    // Case 3's four objects fit in one material pool, so the voxeliser groups
    // them into ONE dispatch either way and only the order INSIDE it varies.
    // This is the other half, and it is the shape the editor actually produces:
    // every primitive gets its own material, so two hundred objects are two
    // hundred materials spread over several pools — and WHICH POOL a material
    // lands in is decided by the order it was created in. So the attach order
    // decides the PARTITION of the instances between dispatches, not just the
    // order within one.
    //
    // They are thin plates stacked in pairs, deliberately: a plate thinner than
    // the outer cascades' cell puts both of its faces in one voxel, which is the
    // case the voxelisation has to decide "these surfaces face opposite ways"
    // about — the decision that used to be made by whichever triangle a thread
    // reached first (fork ad452604a+155a56bf8+0338ca7f2+c4c80b5f7 (was 0065)).
    std::printf("\n== case 4: 200 objects, 200 materials, two orders (0065) ==\n");
    {
        struct Plate { Vec3 pos; Colour albedo; };
        std::vector<Plate> plates;
        for (int x = 0; x < 10; ++x)
            for (int z = 0; z < 10; ++z)
                for (int y = 0; y < 2; ++y) {
                    // A different albedo per PLATE — bound to the plate and not to
                    // the loop, so the reversed arm is the SAME SCENE built in the
                    // other order and not a different one.
                    const float t = float(plates.size()) / 200.0f;
                    plates.push_back({ Vec3(float(x) * 0.6f - 2.7f, 0.6f + float(y) * 0.5f,
                                            float(z) * 0.6f - 2.7f),
                                       Colour(0.2f + 0.7f * t, 0.8f - 0.6f * t, 0.5f) });
                }
        const Vec3 plateScale(0.5f, 0.06f, 0.5f);
        const auto build = [&](bool reverse) {
            std::vector<NodeId> made;
            made.reserve(plates.size());
            for (size_t i = 0; i < plates.size(); ++i) {
                const Plate &pl = plates[reverse ? plates.size() - 1u - i : i];
                // Every object gets its OWN material (addTestCube creates one per
                // call), which is what the editor gives every primitive.
                const NodeId n = enginetest::addTestCube(scene, pl.albedo, 0.0f, 0.6f);
                enginetest::setNodePosition(scene, n, pl.pos);
                enginetest::setNodeScale(scene, n, plateScale);
                made.push_back(n);
            }
            return made;
        };

        std::vector<NodeId> forward = build(false);
        render(e, 2);
        rebuildWhole();
        const Image forwardPic = picture();
        const GiStatus fwdSt = scene->published().gi;
        long long dispatches = 0;
        for (const auto &c : fwdSt.cascades) dispatches += c.voxelDispatches;
        std::printf("   forward: %zu objects, %lld dispatches over the chain\n", forward.size(),
                    dispatches);

        for (NodeId n : forward) scene->removeNode(n);
        render(e, 2);
        std::vector<NodeId> reversed = build(true);
        render(e, 2);
        rebuildWhole();
        const Image reversedPic = picture();

        std::printf("   apart %.5f (%.2f of 255)\n", apart(forwardPic, reversedPic),
                    apart(forwardPic, reversedPic) * 255.0f);
        CHECK(dispatches > 0, "the chain reported how many dispatches it spent");
        CHECK(apart(forwardPic, reversedPic) <= kOneVoxelStep,
              "TWO HUNDRED OBJECTS WITH TWO HUNDRED MATERIALS RENDER THE SAME PICTURE "
              "IN EITHER ATTACH ORDER");
        for (NodeId n : reversed) scene->removeNode(n);
        render(e, 2);
    }

    GiParams off; off.mode = GiMode::Off;
    CHECK(scene->setGlobalIllumination(off), "the chain comes down");
    render(e, 2);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
