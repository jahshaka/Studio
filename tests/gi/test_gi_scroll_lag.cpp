// gi.scroll_lag (PHOTON-7 stage 2, THE SCHEDULE; PREFLIGHT_1_2 §5.5-§5.6) — CASCADE 0 IS NEVER QUEUED BEHIND THE CHAIN.
//
// A scroll's slab is spread over a COUNT of chunk frames (giCascadeScrollFrames: the cascade's motion slack walked at
// 1.4 m/s, then the commit's own frame), and cascade 0 has a RESERVED slot every frame beside the general one - so
// the box the walker stands in moves within its own count, whatever the outer cascades owe. The door is
// GiStatus::CascadeStatus::lagLast / lagMax: the frames from the frame the camera's step test first owed a cascade a
// move to the rebuild that placed it for the camera.
//   THE TABLE: each desktop tier's counts, from its own resolved chain, printed against the table the count's comment
//     states (Epic / High 3 / 7 / 31 / 641, Medium 9 / 20 / 31 / 641, Low 9 / 213).
//   THE DOLLY: Epic, 1.4 m/s (0.0233 m a frame) down a 60 m floor with props, past the outermost cascade's step - cascade
//     0's lag at most 4 frames (its 3 chunk frames and the commit's), every one of its scrolls committed; and EVERY
//     cascade's lag within its own motion slack in frames (the outer cascades share the general slot: the read's W1).
//   THE FLY: Epic, 0.25 m a frame over 15 m (about six of cascade 0's 2.578 m steps) - cascade 0's lag at most one step
//     at that speed (10 frames), and it kept up (a scroll per step).
// A fixture (the engine alone, one small offscreen view; the gather, the field and the reflection off).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

static GiParams tierGi(GiQuality q)
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = q;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::Off;
    gi.updateBudget = 0;
    gi.numBounces = 1;
    return gi;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-scroll-lag-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("lag", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("lag");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    // A 60 m floor down x, and props along the walk on both sides: every entering slab has geometry to voxelise.
    const NodeId floor = enginetest::addTestCube(scene, Colour(0.7f, 0.7f, 0.7f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, floor, Vec3(20.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, floor, Vec3(80.0f, 0.1f, 24.0f));
    for (int k = 0; k < 14; ++k) {
        const NodeId p = enginetest::addTestCube(scene, Colour(0.3f + 0.05f * k, 0.5f, 0.8f - 0.04f * k), 0.0f, 0.6f);
        enginetest::setNodePosition(scene, p, Vec3(-4.0f + 4.1f * float(k), 0.8f, (k & 1) ? 2.4f : -2.6f));
        enginetest::setNodeScale(scene, p, Vec3(0.8f, 1.6f, 0.8f));
    }
    enginetest::addDirectionalLight(scene, Vec3(0.3f, -1.0f, -0.4f), 3.0f);
    Vec3 cam(0.0f, 1.6f, 0.0f);
    const auto look = [&](const Vec3 &p) {
        cam = p;
        enginetest::testCameraLookAt(view, p, Vec3(p.x + 4.0f, 1.2f, p.z - 1.0f));
    };
    look(cam);

    // ---- THE TABLE ---------------------------------------------------------------------------------------------
    std::printf("== the scroll counts per tier (giCascadeScrollFrames, from each tier's resolved chain) ==\n");
    {
        const struct { GiQuality q; const char *name; std::vector<int> want; } tiers[] = {
            { GiQuality::Low, "Low", { 9, 213 } }, { GiQuality::Medium, "Medium", { 9, 20, 31, 641 } },
            { GiQuality::High, "High", { 3, 7, 31, 641 } }, { GiQuality::Epic, "Epic", { 3, 7, 31, 641 } } };
        bool all = true;
        for (const auto &t : tiers) {
            if (!scene->setGlobalIllumination(tierGi(t.q))) { std::printf("   %s: no chain\n", t.name); all = false; continue; }
            run(4);
            const GiStatus st = scene->published().gi;
            std::printf("   %-6s", t.name);
            bool same = st.cascades.size() == t.want.size();
            for (size_t i = 0; i < st.cascades.size(); ++i) {
                const GiStatus::CascadeStatus &cs = st.cascades[i];
                GiParams::GiCascadeDesc d;
                d.halfSize = cs.halfSize;
                d.resolution = cs.resolution;
                d.stepCells = cs.step / cs.cell;
                const int f = giCascadeScrollFrames(d);
                std::printf("  c%zu %d (slack %.3f m, step %.3f m)", i, f,
                            double(giCascadeGuaranteedRadius(d) - giNearFieldRadius(d)), double(cs.step));
                same = same && i < t.want.size() && f == t.want[i];
            }
            std::printf("%s\n", same ? "" : "  <- not the stated table");
            all = all && same;
        }
        CHECK(all, "THE COUNTS ARE THE STATED TABLE: the motion slack at 1.4 m/s, capped by the step period");
    }

    // ---- THE DOLLY ---------------------------------------------------------------------------------------------
    std::printf("\n== the dolly: Epic, 1.4 m/s ==\n");
    {
        GiParams off;
        off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        run(2);
        look(Vec3(0.0f, 1.6f, 0.0f));
        CHECK(scene->setGlobalIllumination(tierGi(GiQuality::Epic)), "the Epic chain builds");
        run(12);   // every cascade built where the dolly starts
        e->waitForAsyncShaders();   // the commit's shader built (a wall-clock wait the frames below must not count)
        const GiStatus pre = scene->published().gi;
        const float v = kGiWalkMetresPerFrame;
        const float step0 = pre.cascades[0].step;
        // PAST THE OUTERMOST CASCADE'S STEP (PHOTON-7 stage 2's read, W1): every cascade scrolls at least once, the outer
        // ones sharing the general slot - and at least 2.2 of cascade 0's steps.
        const float stepOut = pre.cascades.back().step;
        const int n = int(std::ceil(std::max(2.2f * step0, 1.05f * stepOut) / v));
        for (int f = 0; f < n; ++f) {
            look(Vec3(cam.x + v, cam.y, cam.z));
            run(1);
        }
        run(6);   // the last scroll's commit
        const GiStatus post = scene->published().gi;
        std::printf("   %d frames, %.2f m; per cascade: scrolls, lag last / max (frames)\n", n, double(n * v));
        for (size_t i = 0; i < post.cascades.size(); ++i)
            std::printf("     c%zu  +%llu scrolls, lag %llu / %llu\n", i, post.cascades[i].scrolls - pre.cascades[i].scrolls,
                        post.cascades[i].lagLast, post.cascades[i].lagMax);
        CHECK(post.cascades[0].scrolls - pre.cascades[0].scrolls >= 2u, "cascade 0 scrolled at each of its steps");
        CHECK(post.cascades[0].lagMax <= 4u && post.cascades[0].lagMax >= 1u,
              "CASCADE 0'S LAG AT 1.4 m/s IS AT MOST 4 FRAMES (its chunk frames and the commit's)");
        // EVERY CASCADE WITHIN ITS OWN SLACK (W1): the frames the walker takes to cross a cascade's motion slack (its
        // guaranteed radius less the near field, at 1.4 m/s) bound how late its placement may come - the outer cascades
        // share the general slot by age, so concurrent flights stretch each other.
        bool allScrolled = true, allWithin = true;
        for (size_t i = 0; i < post.cascades.size(); ++i) {
            const GiStatus::CascadeStatus &cs = post.cascades[i];
            GiParams::GiCascadeDesc d;
            d.halfSize = cs.halfSize;
            d.resolution = cs.resolution;
            d.stepCells = cs.step / cs.cell;
            const double slack = double(giCascadeGuaranteedRadius(d) - giNearFieldRadius(d));
            const unsigned long long slackFrames = (unsigned long long)std::floor(slack / double(v));
            const bool scrolled = cs.scrolls > pre.cascades[i].scrolls;
            std::printf("     c%zu  lag max %llu of its slack's %llu frames (%.3f m)%s\n", i, cs.lagMax, slackFrames, slack,
                        scrolled ? "" : "  <- never scrolled");
            allScrolled = allScrolled && scrolled;
            allWithin = allWithin && cs.lagMax <= slackFrames;
        }
        CHECK(allScrolled, "every cascade scrolled on the dolly");
        CHECK(allWithin, "EVERY CASCADE'S LAG IS WITHIN ITS OWN SLACK AT 1.4 m/s (the outer ones share the general slot)");
    }

    // ---- THE FLY -----------------------------------------------------------------------------------------------
    std::printf("\n== the fly: Epic, 0.25 m a frame ==\n");
    {
        GiParams off;
        off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        run(2);
        look(Vec3(0.0f, 1.6f, 0.0f));
        CHECK(scene->setGlobalIllumination(tierGi(GiQuality::Epic)), "the Epic chain builds again");
        run(12);
        e->waitForAsyncShaders();
        const GiStatus pre = scene->published().gi;
        const float v = 0.25f;
        const int n = 60;
        for (int f = 0; f < n; ++f) {
            look(Vec3(cam.x + v, cam.y, cam.z));
            run(1);
        }
        run(6);
        const GiStatus post = scene->published().gi;
        const float step0 = post.cascades[0].step;
        const unsigned long long bar = (unsigned long long)std::floor(step0 / v);
        std::printf("   %d frames, %.2f m (cascade 0's step %.3f m = %llu frames); per cascade: scrolls, lag last / max\n",
                    n, double(n * v), double(step0), bar);
        for (size_t i = 0; i < post.cascades.size(); ++i)
            std::printf("     c%zu  +%llu scrolls, lag %llu / %llu\n", i, post.cascades[i].scrolls - pre.cascades[i].scrolls,
                        post.cascades[i].lagLast, post.cascades[i].lagMax);
        const unsigned long long steps = (unsigned long long)std::floor(double(n * v) / double(step0));
        CHECK(post.cascades[0].scrolls - pre.cascades[0].scrolls + 1u >= steps,
              "cascade 0 kept up: a scroll per step of the fly");
        CHECK(post.cascades[0].lagMax <= bar, "CASCADE 0'S LAG AT 0.25 m A FRAME IS AT MOST ONE STEP");
    }

    GiParams off;
    off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    run(2);
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
