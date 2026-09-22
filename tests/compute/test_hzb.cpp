// chain.hzb — THE HIERARCHICAL DEPTH PYRAMID (SPECS/NANITE_SPEC.md §4.3).
//
// WHAT IT IS. One R32_FLOAT texture at the view's resolution with a full mip
// chain down to 1x1, built once per frame right after the opaque pass by one
// compute pass per level: mip 0 is a copy of the scene depth, and every level
// after it holds the CLOSEST depth of its footprint in the level above. A
// stackless screen-space trace walks it instead of stepping pixel by pixel —
// Epic measure the ray compaction that rides on it at up to a 50% tracing
// speedup — and NOTHING IN THIS ENGINE READS IT YET. It is shared Photon
// infrastructure and it is off everywhere until a spike asks for it.
//
// WHICH DEPTH A LEVEL KEEPS IS THE CHAIN'S OWN REQUEST (PostFxDesc::hzbFarthest,
// ATOM-SUBSTRATE-1's fix round) and the DEFAULT IS THE FARTHEST, because that is
// the only direction an occlusion cull can be conservative against: a
// closest-depth level makes a texel that is half wall and half sky report the
// wall, and an object seen through the sky half is culled. A stackless
// screen-space trace wants the opposite and asks for its own build; nothing
// reads that one today, and this suite drives BOTH so neither can rot.
//
// WHICH WAY IS CLOSE is a separate question with its own answer: Ogre's Vulkan
// render system runs REVERSE-Z at this pin (near = 1, far = 0 —
// `RenderSystem::isReverseDepth`, which our shadow-map clear material has always
// branched on), so the FARTHEST sample of a footprint is its MINIMUM stored value
// and the nearest is its maximum. The suite reads both conventions out of
// HzbStatus rather than assuming either.
//
// WHAT IS ASSERTED:
//   1. THE SHAPE. The pyramid exists, has the mip count the resolution implies
//      (9 at 256x256, 11 at 1920x1080) and is the view's own size.
//   2. MIP 0 IS THE DEPTH BUFFER. A cube at a known distance on the matte
//      ground must show up as a region of NEARER depth than the far plane, and
//      the far region must be exactly the cleared far value — otherwise the
//      seed pass could be writing anything and every test below would still
//      pass.
//   3. THE REDUCTION, texel by texel, against an EXACTLY COMPUTED FOOTPRINT.
//      Every mip-3 texel must be EXACTLY the extreme its chain asked for — the
//      farthest, or the closest on a closest-depth build — of the mip-0 region it
//      covers. An equality in both directions, because either error is real: a
//      level that overstates its region rejects geometry that is visible (the one
//      error a hierarchical depth test must never make) and one that understates
//      it rejects less than it could.
//
//      The footprint is DERIVED, not assumed to be 8x8, by composing the
//      reducer's own coverage rule level by level — which is what lets the
//      assertion be an equality on a view whose levels do not halve exactly.
//      On a 256x256 view every interior and edge footprint comes out 8 wide; on
//      an ODD view they are 8 wide in the interior and wider only in the last
//      row and column, because the shader extends its gather to three samples
//      for the LAST destination texel of an odd axis and only for it. Extending
//      it everywhere is also conservative and also passes an inequality — which
//      is exactly why this is an equality against a computed footprint instead.
//   4. THE TOP IS THE WHOLE FRAME. The single texel of the last level must be
//      the closest depth anywhere in the picture.
//   5. THE SAME, ON AN ODD-SIZED VIEW (255x135): three of its reductions have an
//      odd source (255->127, 127->63, 63->31), so the extension path runs, and
//      the equality above must still hold.
//   6. THE COST, at 1920x1080, from patch 0027's GPU timestamps through the
//      render-loop monitor: the sum of the HZB passes' GPU milliseconds. The
//      brief's budget is < 0.3 ms on the 4080; the number is printed either
//      way, because a budget nobody can see is not a measurement.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
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

/// A cube on the ground, seen by a perspective camera. Deliberately not the
/// matte default floor: what matters here is that the depth buffer has BOTH a
/// near region and untouched far pixels, so the reduction has something to do.
static View *buildScene(Engine *e, const char *name, unsigned w, unsigned h, Scene *&sceneOut,
                        bool farthest = true)
{
    View *view = e->createOffscreenView(name, w, h, Colour(0, 0, 0));
    Scene *scene = e->createScene(name);
    view->setScene(scene);
    sceneOut = scene;

    PostFxDesc fx;
    fx.hzb = true;             // the only thing this view asks the chain for
    fx.hzbFarthest = farthest;
    view->setPostFx(fx);

    scene->setAmbient(Colour(0.4f, 0.4f, 0.4f), Colour(0.2f, 0.2f, 0.2f));
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.6f, 0.6f, 0.6f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.55f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(20.0f, 0.1f, 20.0f));
    const NodeId box = enginetest::addTestCube(scene, Colour(0.8f, 0.2f, 0.2f), 0.0f, 0.5f);
    enginetest::setNodePosition(scene, box, Vec3(0.0f, 0.0f, 0.0f));
    enginetest::setNodeScale(scene, box, Vec3(2.0f, 2.0f, 2.0f));

    CameraDesc cam;
    cam.position = Vec3(0.0f, 2.0f, 8.0f);
    cam.farClip = 500.0f;
    view->setCamera(cam);
    return view;
}

/// Is `a` at least as EXTREME as `b` in the direction this chain keeps? With
/// reverse-Z a farther sample is a smaller number, so "at least as far" is <=.
static bool atLeastAsExtreme(float a, float b, bool reverseZ, bool farthest)
{
    const bool nearerIsLarger = reverseZ;
    const bool wantLarger = farthest ? !nearerIsLarger : nearerIsLarger;
    return wantLarger ? a >= b - 1e-6f : a <= b + 1e-6f;
}

/// The seed of a running extreme, and the pick, in one place.
static float extremeSeed(bool reverseZ, bool farthest)
{
    const bool wantLarger = farthest ? !reverseZ : reverseZ;
    return wantLarger ? 0.0f : 1.0f;
}
static float pickExtreme(float a, float b, bool reverseZ, bool farthest)
{
    const bool wantLarger = farthest ? !reverseZ : reverseZ;
    return wantLarger ? std::max(a, b) : std::min(a, b);
}

/// WHICH MIP-0 TEXELS A LEVEL-`level` TEXEL COVERS, one axis, composed level by
/// level from the reducer's own rule (JahHzbReduce_cs.glsl): destination texel i
/// gathers source texels 2i and 2i+1, plus 2i+2 when the source size is odd AND
/// i is the last texel of the axis (the uncovered source column/row is the last
/// one, so nothing else needs the third sample). Source reads are clamped to the
/// last texel, exactly as the shader clamps them.
///
/// Deriving the footprint instead of assuming 8x8 is what makes the suite able
/// to assert an EQUALITY on a view whose levels do not halve exactly — and what
/// makes it fail if the extension is ever applied to every texel again.
static void coverage(unsigned size0, unsigned level, std::vector<unsigned> &begin,
                     std::vector<unsigned> &end)
{
    begin.resize(size0);
    end.resize(size0);
    for (unsigned i = 0; i < size0; ++i) { begin[i] = i; end[i] = i; }

    unsigned src = size0;
    for (unsigned L = 1; L <= level; ++L) {
        const unsigned dst = std::max(src >> 1u, 1u);
        std::vector<unsigned> nb(dst), ne(dst);
        for (unsigned i = 0; i < dst; ++i) {
            const unsigned a = std::min(2u * i, src - 1u);
            unsigned b = std::min(2u * i + 1u, src - 1u);
            if ((src & 1u) != 0u && i == dst - 1u) b = std::min(2u * i + 2u, src - 1u);
            nb[i] = begin[a];
            ne[i] = end[b];
        }
        begin.swap(nb);
        end.swap(ne);
        src = dst;
    }
}

/// Everything that can be asserted about one view's pyramid. `expectLevels` is
/// what the resolution implies; `exactHalving` only decides what gets PRINTED
/// (the footprint spans), never how hard the assertion is — the equality below
/// is against a computed footprint and holds on both kinds of view.
static void checkPyramid(Engine *e, View *view, const char *what, unsigned w, unsigned h,
                         unsigned expectLevels)
{
    // The direction under test comes from the chain itself, never from a guess
    // here: HzbStatus::farthest is what the reduce job was compiled with.
    std::printf("\n== %s (%ux%u) ==\n", what, w, h);
    HzbStatus st;
    char msg[192];
    if (!e->hzbStatus(view, st) || !st.built) {
        std::printf("FAIL: %s built no pyramid\n", what);
        ++failures;
        return;
    }
    std::printf("   %ux%u, %u levels, reverse-Z %d, keeps the %s depth, primed %d\n", st.width,
                st.height, st.levels, st.reverseDepth ? 1 : 0,
                st.farthest ? "FARTHEST" : "closest", st.primed ? 1 : 0);
    CHECK(st.primed, "a presented frame has written the pyramid");
    CHECK(st.width == w && st.height == h, "mip 0 is the view's own resolution");
    std::snprintf(msg, sizeof(msg), "%ux%u gives %u levels", w, h, expectLevels);
    CHECK(st.levels == expectLevels, msg);
    if (st.levels < 4u) { std::printf("FAIL: too few levels to test mip 3\n"); ++failures; return; }

    std::vector<float> mip0, mip3, top;
    unsigned w0 = 0, h0 = 0, w3 = 0, h3 = 0, wt = 0, ht = 0;
    CHECK(e->readHzbLevel(view, 0u, mip0, w0, h0), "mip 0 reads back");
    CHECK(e->readHzbLevel(view, 3u, mip3, w3, h3), "mip 3 reads back");
    CHECK(e->readHzbLevel(view, st.levels - 1u, top, wt, ht), "the top level reads back");
    CHECK(w0 == w && h0 == h && w3 == std::max(w >> 3u, 1u) && h3 == std::max(h >> 3u, 1u) &&
              wt == 1u && ht == 1u,
          "the levels have the sizes the chain implies");
    if (mip0.empty() || mip3.empty() || top.empty()) return;

    // ---- mip 0 really is the depth buffer --------------------------------
    const float farValue = st.reverseDepth ? 0.0f : 1.0f;
    size_t nearer = 0, atFar = 0;
    float extreme0 = extremeSeed(st.reverseDepth, st.farthest);
    for (float d : mip0) {
        if (std::fabs(d - farValue) < 1e-6f) ++atFar; else ++nearer;
        extreme0 = pickExtreme(extreme0, d, st.reverseDepth, st.farthest);
    }
    std::printf("   mip0: %zu texels nearer than the far plane, %zu at it; the %s of them %.6f\n",
                nearer, atFar, st.farthest ? "farthest" : "closest", extreme0);
    CHECK(nearer > 500u, "mip 0 carries the geometry's depth (not a blank buffer)");
    CHECK(atFar > 100u, "mip 0 carries untouched far pixels too (so there IS a reduction to do)");

    // ---- the reduction, texel by texel, against the computed footprint ----
    std::vector<unsigned> bx, ex, by, ey;
    coverage(w0, 3u, bx, ex);
    coverage(h0, 3u, by, ey);
    size_t tooFar = 0, tooClose = 0;
    unsigned minSpanX = 0xFFFFFFFFu, maxSpanX = 0, minSpanY = 0xFFFFFFFFu, maxSpanY = 0;
    for (unsigned y = 0; y < h3; ++y) {
        for (unsigned x = 0; x < w3; ++x) {
            const unsigned spanX = ex[x] - bx[x] + 1u, spanY = ey[y] - by[y] + 1u;
            minSpanX = std::min(minSpanX, spanX); maxSpanX = std::max(maxSpanX, spanX);
            minSpanY = std::min(minSpanY, spanY); maxSpanY = std::max(maxSpanY, spanY);
            float foot = extremeSeed(st.reverseDepth, st.farthest);
            for (unsigned fy = by[y]; fy <= ey[y]; ++fy)
                for (unsigned fx = bx[x]; fx <= ex[x]; ++fx)
                    foot = pickExtreme(foot, mip0[size_t(fy) * w0 + fx], st.reverseDepth,
                                       st.farthest);
            const float got = mip3[size_t(y) * w3 + x];
            if (!atLeastAsExtreme(got, foot, st.reverseDepth, st.farthest)) ++tooFar;
            if (!atLeastAsExtreme(foot, got, st.reverseDepth, st.farthest)) ++tooClose;
        }
    }
    std::printf("   mip3 footprints span %u..%u x %u..%u mip-0 texels; %zu too far, "
                "%zu too close (of %u)\n", minSpanX, maxSpanX, minSpanY, maxSpanY, tooFar,
                tooClose, w3 * h3);
    CHECK(tooFar == 0, "every mip-3 texel is at least as extreme as its footprint's own extreme");
    CHECK(tooClose == 0, "and no more extreme than it — the reduction is exactly that extreme");
    // The interior of ANY view reduces exactly 8 mip-0 texels per axis: seeing a
    // 9 or a 10 here is the over-conservative reducer coming back.
    CHECK(minSpanX == 8u && minSpanY == 8u,
          "the interior footprint is exactly 8 texels per axis (no neighbour absorbed)");

    // ---- the top level is the whole frame ---------------------------------
    std::printf("   top level %.6f vs the frame's own %s %.6f\n", top[0],
                st.farthest ? "farthest" : "closest", extreme0);
    CHECK(std::fabs(top[0] - extreme0) < 1e-6f,
          "the 1x1 level is that extreme over the whole picture");
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-chain-hzb-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    // =====================================================================
    // 256x256 — every level halves exactly, so every footprint is 8x8 and the
    // reducer's odd-size extension never runs.
    // =====================================================================
    Scene *scene = nullptr;
    View *view = buildScene(e, "hzb256", 256u, 256u, scene);
    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    checkPyramid(e, view, "256x256, exact halving", 256u, 256u, 9u);

    // =====================================================================
    // 255x135 — THE ODD CASE. Three of its reductions have an odd source
    // (255->127, 127->63, 63->31 on x; 135->67, 67->33, 33->16 on y), so the
    // extension path runs on every one of them. The same equality must hold:
    // the interior footprints stay 8 wide (nothing absorbs its neighbour) and
    // only the last row and column reach further, because that is the only
    // place the source has a texel no destination texel would otherwise cover.
    // =====================================================================
    Scene *oddScene = nullptr;
    View *odd = buildScene(e, "hzb255", 255u, 135u, oddScene);
    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    checkPyramid(e, odd, "255x135, odd levels", 255u, 135u, 8u);

    // =====================================================================
    // THE OTHER DIRECTION. Nothing reads the closest-depth chain today (Photon's
    // screen trace will), so it is driven here or it rots: the same equality, on
    // the same fixture, with `hzbFarthest` off.
    // =====================================================================
    Scene *nearScene = nullptr;
    View *nearView = buildScene(e, "hzbnear", 256u, 256u, nearScene, /*farthest=*/false);
    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    checkPyramid(e, nearView, "256x256, the CLOSEST-depth build", 256u, 256u, 9u);
    {
        HzbStatus nst;
        CHECK(e->hzbStatus(nearView, nst) && !nst.farthest,
              "the closest-depth build reports itself as one");
    }

    // =====================================================================
    // 1920x1080: the level count and THE COST, from patch 0027's GPU
    // timestamps. Its own view so the assertions above stay on the exact case.
    // =====================================================================
    std::printf("\n== 1920x1080: levels and cost ==\n");
    Scene *bigScene = nullptr;
    View *big = buildScene(e, "hzb1080", 1920u, 1080u, bigScene);
    for (int i = 0; i < 4; ++i) e->renderOneFrame();

    HzbStatus bst;
    CHECK(e->hzbStatus(big, bst) && bst.built, "the 1080p view built a pyramid");
    std::printf("   %ux%u, %u levels\n", bst.width, bst.height, bst.levels);
    CHECK(bst.levels == 11u, "1920x1080 gives 11 levels (1920..1)");

    e->setFrameMonitor(MonitorLevel::Review);
    for (int i = 0; i < 24; ++i) e->renderOneFrame();
    e->setFrameMonitor(MonitorLevel::Off);
    std::vector<FrameRecord> records;
    while (e->takeFrameRecords(records) > 0) {}

    // The best (lowest) measured frame: this box runs other lanes, and the
    // question "what does the pyramid cost" is answered by the cheapest clean
    // sample, not by the mean of a contended one.
    // BY WORKSPACE, not just by pass name: the 256x256 and 255x135 views are
    // still alive and still building their own pyramids every frame. A count
    // that ignored which workspace a pass belongs to would read 27 passes and a
    // cost that is three pyramids added together.
    const std::string bigWorkspace = "hzb1080/Workspace";
    float bestMs = -1.0f;
    unsigned measuredFrames = 0, passCount = 0;
    for (const FrameRecord &r : records) {
        float sum = 0.0f;
        unsigned n = 0;
        bool complete = true;
        for (const FramePass &p : r.passes) {
            if (p.workspace != bigWorkspace) continue;
            if (p.pass.rfind("Jahshaka HZB", 0) != 0) continue;
            ++n;
            if (p.gpuMs < 0.0f) complete = false; else sum += p.gpuMs;
        }
        if (!n) continue;
        passCount = std::max(passCount, n);
        if (!complete) continue;   // gpuMs is negative when unmeasured, never faked as 0
        ++measuredFrames;
        if (bestMs < 0.0f || sum < bestMs) bestMs = sum;
    }
    std::printf("   HZB passes seen per frame: %u (expected %u); frames with GPU samples: %u\n",
                passCount, bst.levels, measuredFrames);
    CHECK(passCount == bst.levels, "one compute pass per level runs every frame");
    if (bestMs >= 0.0f) {
        std::printf("   HZB GPU cost at 1920x1080: %.4f ms (best of %u measured frames)\n",
                    bestMs, measuredFrames);
        CHECK(bestMs < 0.3f, "the pyramid costs less than 0.3 ms at 1920x1080");
    } else {
        // Not a failure: GPU timestamps are a build/device capability
        // (FramePass::gpuMs is negative when unmeasured, never faked as 0).
        std::printf("   HZB GPU cost: NOT MEASURED on this build/device "
                    "(no timestamp samples came back)\n");
    }

    // =====================================================================
    // AND THE COST AT 4K (the design asked for both). 3840x2160 is four times
    // 1080p's pixels and one more level; the build is bandwidth-bound in its seed,
    // so this is the number that says whether the per-mip chain scales.
    // =====================================================================
    std::printf("\n== 3840x2160: levels and cost ==\n");
    Scene *uhdScene = nullptr;
    View *uhd = buildScene(e, "hzb4k", 3840u, 2160u, uhdScene);
    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    HzbStatus ust;
    CHECK(e->hzbStatus(uhd, ust) && ust.built, "the 4K view built a pyramid");
    std::printf("   %ux%u, %u levels\n", ust.width, ust.height, ust.levels);
    CHECK(ust.levels == 12u, "3840x2160 gives 12 levels (3840..1)");
    {
        e->setFrameMonitor(MonitorLevel::Review);
        for (int i = 0; i < 24; ++i) e->renderOneFrame();
        e->setFrameMonitor(MonitorLevel::Off);
        std::vector<FrameRecord> recs;
        while (e->takeFrameRecords(recs) > 0) {}
        const std::string ws = "hzb4k/Workspace";
        float best = -1.0f;
        unsigned measured = 0, passes = 0;
        for (const FrameRecord &r : recs) {
            float sum = 0.0f; unsigned n = 0; bool complete = true;
            for (const FramePass &pp : r.passes) {
                if (pp.workspace != ws) continue;
                if (pp.pass.rfind("Jahshaka HZB", 0) != 0) continue;
                ++n;
                if (pp.gpuMs < 0.0f) complete = false; else sum += pp.gpuMs;
            }
            if (!n) continue;
            passes = std::max(passes, n);
            if (!complete) continue;
            ++measured;
            if (best < 0.0f || sum < best) best = sum;
        }
        std::printf("   HZB passes per frame: %u (expected %u); frames with GPU samples: %u\n",
                    passes, ust.levels, measured);
        if (best >= 0.0f)
            std::printf("   HZB GPU cost at 3840x2160: %.4f ms (best of %u measured frames)\n",
                        best, measured);
        else
            std::printf("   HZB GPU cost at 4K: NOT MEASURED on this build/device\n");
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
