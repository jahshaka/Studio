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
// WHICH WAY IS CLOSE. Ogre's Vulkan render system runs REVERSE-Z at this pin
// (near = 1, far = 0 — `RenderSystem::isReverseDepth`, which our shadow-map
// clear material has always branched on), so the closest depth of a footprint
// is its MAXIMUM stored value and the reduction is a max. The suite reads the
// convention out of HzbStatus rather than assuming it, and asserts the
// reduction in the direction that convention implies.
//
// WHAT IS ASSERTED:
//   1. THE SHAPE. The pyramid exists, has the mip count the resolution implies
//      (9 at 256x256, 11 at 1920x1080) and is the view's own size.
//   2. MIP 0 IS THE DEPTH BUFFER. A cube at a known distance on the matte
//      ground must show up as a region of NEARER depth than the far plane, and
//      the far region must be exactly the cleared far value — otherwise the
//      seed pass could be writing anything and every test below would still
//      pass.
//   3. THE REDUCTION, texel by texel. Every mip-3 texel must be AT LEAST AS
//      CLOSE as the closest of its 8x8 footprint in mip 0, and no closer than
//      that footprint's closest — i.e. exactly it. This is the property a
//      hierarchical depth test depends on: a level that claimed something is
//      farther than it really is would cull geometry that is actually visible.
//      256x256 is used so every level halves exactly and the footprint really
//      is 8x8 (the shader's odd-size extension is a separate, conservative
//      path and would make the equality an inequality).
//   4. THE TOP IS THE WHOLE FRAME. The single texel of the last level must be
//      the closest depth anywhere in the picture.
//   5. THE COST, at 1920x1080, from patch 0027's GPU timestamps through the
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
static View *buildScene(Engine *e, const char *name, unsigned w, unsigned h, Scene *&sceneOut)
{
    View *view = e->createOffscreenView(name, w, h, Colour(0, 0, 0));
    Scene *scene = e->createScene(name);
    view->setScene(scene);
    sceneOut = scene;

    PostFxDesc fx;
    fx.hzb = true;             // the only thing this view asks the chain for
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

/// "at least as close as", in the engine's own convention.
static bool atLeastAsClose(float a, float b, bool reverseZ)
{
    return reverseZ ? a >= b - 1e-6f : a <= b + 1e-6f;
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
    // 256x256: the exact-halving case, where a mip-3 texel's footprint really
    // is 8x8 and the reduction can be asserted as an equality.
    // =====================================================================
    std::printf("\n== 256x256: shape and the reduction ==\n");
    Scene *scene = nullptr;
    View *view = buildScene(e, "hzb256", 256u, 256u, scene);
    for (int i = 0; i < 4; ++i) e->renderOneFrame();

    HzbStatus st;
    CHECK(e->hzbStatus(view, st) && st.built, "the view built a pyramid");
    if (!st.built) { std::printf("FAILED (%d failures)\n", failures + 1); return 1; }
    std::printf("   %ux%u, %u levels, reverse-Z %d\n", st.width, st.height, st.levels,
                st.reverseDepth ? 1 : 0);
    CHECK(st.width == 256u && st.height == 256u, "mip 0 is the view's own resolution");
    CHECK(st.levels == 9u, "256x256 gives 9 levels (256..1)");

    std::vector<float> mip0, mip3, top;
    unsigned w0 = 0, h0 = 0, w3 = 0, h3 = 0, wt = 0, ht = 0;
    CHECK(e->readHzbLevel(view, 0u, mip0, w0, h0), "mip 0 reads back");
    CHECK(e->readHzbLevel(view, 3u, mip3, w3, h3), "mip 3 reads back");
    CHECK(e->readHzbLevel(view, st.levels - 1u, top, wt, ht), "the top level reads back");
    CHECK(w0 == 256u && h0 == 256u && w3 == 32u && h3 == 32u && wt == 1u && ht == 1u,
          "the levels have the sizes the chain implies");

    // ---- 2. mip 0 really is the depth buffer -----------------------------
    const float farValue = st.reverseDepth ? 0.0f : 1.0f;
    size_t nearer = 0, atFar = 0;
    float closest0 = st.reverseDepth ? 0.0f : 1.0f;
    for (float d : mip0) {
        if (std::fabs(d - farValue) < 1e-6f) ++atFar;
        else ++nearer;
        closest0 = st.reverseDepth ? std::max(closest0, d) : std::min(closest0, d);
    }
    std::printf("   mip0: %zu texels nearer than the far plane, %zu at it; closest %.6f\n",
                nearer, atFar, closest0);
    CHECK(nearer > 1000u, "mip 0 carries the geometry's depth (not a blank buffer)");
    CHECK(atFar > 100u, "mip 0 carries untouched far pixels too (so there IS a reduction to do)");

    // ---- 3. the reduction, texel by texel --------------------------------
    size_t tooFar = 0, tooClose = 0;
    for (unsigned y = 0; y < h3; ++y) {
        for (unsigned x = 0; x < w3; ++x) {
            float foot = st.reverseDepth ? 0.0f : 1.0f;
            for (unsigned fy = 0; fy < 8u; ++fy)
                for (unsigned fx = 0; fx < 8u; ++fx) {
                    const float d = mip0[size_t(y * 8u + fy) * w0 + (x * 8u + fx)];
                    foot = st.reverseDepth ? std::max(foot, d) : std::min(foot, d);
                }
            const float got = mip3[size_t(y) * w3 + x];
            if (!atLeastAsClose(got, foot, st.reverseDepth)) ++tooFar;
            if (!atLeastAsClose(foot, got, st.reverseDepth)) ++tooClose;
        }
    }
    std::printf("   mip3 vs its 8x8 footprints: %zu too far, %zu too close (of %u)\n",
                tooFar, tooClose, w3 * h3);
    CHECK(tooFar == 0, "every mip-3 texel is at least as close as the closest of its 8x8 block");
    CHECK(tooClose == 0, "and no closer than it — the reduction is exactly the closest depth");

    // ---- 4. the top level is the whole frame ------------------------------
    std::printf("   top level %.6f vs the frame's closest %.6f\n", top[0], closest0);
    CHECK(std::fabs(top[0] - closest0) < 1e-6f,
          "the 1x1 level is the closest depth anywhere in the picture");

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
    // BY WORKSPACE, not just by pass name: the 256x256 view is still alive and
    // still building its own 9-level pyramid every frame. A count that ignored
    // which workspace a pass belongs to would read 20 passes and a cost that is
    // two pyramids added together.
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

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
