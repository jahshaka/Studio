// gi.settle_partial — THE OWED SETTLE INJECTS ONLY THE STALE CASCADES
// (PHOTON-GATHER-1b item 6; WRITER-1's debt, audit F5; SPECS/photon/
// B4_ONE_WRITER_AND_FIELD_STEP_DESIGN.md "Lead decisions after the WRITER-1 audit").
//
// THE CHAIN'S ORDER. A light tick injects the cascades OUTERMOST FIRST, and each
// cascade reads only the cascades OUTSIDE it — so one sweep in that order is the
// chain's fixed point (kAtRestSweeps = 1). When a cascade k is re-voxelised in
// the frame an at-rest tick runs, the one-writer latch refuses the tick's second
// injection of k (the rebuild's came first, over the outer cascades' OLD light),
// and every cascade inside k then read a stale k. The stale set is exactly
// { k, k-1, ..., 0 }; the cascades outside k were injected by this very tick over
// inputs that have not moved since, so injecting them again changes no byte.
// The debt used to be a WHOLE sweep (n injections); it is now k + 1.
//
// WHAT IS ASSERTED, read out of the engine's own books (the render monitor's
// `vct.light.settle` rows — one injection each — and the cascades' rebuild
// counters) and the light voxels' digests:
//   1. an edit re-voxelises the cascades whose boxes contain it, ONE PER FRAME,
//      innermost first (every cascade here — the outermost box contains every
//      edit, so a lone cascade-2 rebuild is not a thing an edit can produce),
//      and an at-rest light tick is asked for IN CASCADE 2'S FRAME: the latch
//      refuses the tick's injection of cascade 2 (the rebuild's came first) and
//      the tick injects the other three;
//   2. the settle that follows costs k + 1 = 3 injections — cascades 2, 1, 0 —
//      not the chain's 4 (the base paid 4: the refused tick and every rebuild
//      each owed the whole chain);
//   3. and it IS the fixed point: a whole at-rest sweep run afterwards changes
//      no byte of any cascade's light voxels and no pixel of the picture (0/255,
//      the same frozen frame).
// PHOTON-GATHER-1d: THE GATHER PINNED OFF. Since 1d the screen-probe gather is
// the diffuse at every ray tier (GiToggle::Auto resolves on at Medium and above);
// this suite measures the voxel chain / the field / the cones / the probes, which
// it pins, so its numbers stay about them. The gather has its own suites
// (gi.gather_*).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[640];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-settle-partial-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("settle", 256u, 256u, Colour(0, 0, 0));
    Scene *s = e->createScene("settle");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    view->setShadows(true);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.02f, 0.02f, 0.03f));
    enginetest::leakroom::addSlab(s, Colour(0.8f, 0.8f, 0.8f), Vec3(0, -0.25f, 0), Vec3(80, 0.5f, 80));
    enginetest::leakroom::addSlab(s, Colour(0.8f, 0.2f, 0.2f), Vec3(0, 2.0f, -3.0f), Vec3(6, 4, 0.3f));
    const NodeId sun = enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 2.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 5.0f), Vec3(0.0f, 1.0f, -2.0f));
    GiParams gi;
    gi.gather = GiToggle::Off;   // PHOTON-GATHER-1d (the header)
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.numBounces = 2;
    gi.cascades = true;
    CHECK(s->setGlobalIllumination(gi), "the cascade chain builds");
    for (int f = 0; f < 240 && !s->giStatus().giAtRest; ++f) e->renderOneFrame();
    e->renderOneFrame();
    GiStatus g0 = s->giStatus();
    const size_t n = g0.cascades.size();
    CHECK_MSG(n >= 4u && g0.giAtRest, "a chain of %zu cascades, at rest", n);
    if (n < 4u) { std::printf("FAILED\n"); return 1; }
    for (size_t i = 0; i < n; ++i)
        std::printf("   cascade %zu: half %.2f m, cell %.3f m, centre (%.2f %.2f %.2f)\n", i,
                    double(g0.cascades[i].halfSize), double(g0.cascades[i].cell),
                    double(g0.cascades[i].centre.x), double(g0.cascades[i].centre.y),
                    double(g0.cascades[i].centre.z));

    // ---- THE EDIT, and an at-rest tick in CASCADE 2's rebuild frame -------
    const GiStatus::CascadeStatus &c1 = g0.cascades[1], &c2 = g0.cascades[2];
    const float along = 0.5f * (c1.halfSize + c2.halfSize);
    const Vec3 at(c2.centre.x + along, 0.3f, c2.centre.z);
    std::vector<unsigned long long> before(n);
    for (size_t i = 0; i < n; ++i) before[i] = g0.cascades[i].rebuilds;

    e->setFrameMonitor(MonitorLevel::Review);
    std::vector<FrameRecord> drop;
    e->renderOneFrame();
    e->takeFrameRecords(drop);
    const NodeId box = enginetest::leakroom::addSlab(s, Colour(0.9f, 0.9f, 0.2f), at,
                                                     Vec3(0.6f, 0.6f, 0.6f));
    CHECK(box != 0, "the edit's box exists");
    s->refreshGlobalIllumination();
    // THE FRAMES, one at a time: the tick is asked for right before the frame
    // that re-voxelises cascade 2 (the one after cascade 1's rebuild shows).
    unsigned settleInjections = 0, tickInCascade2Frame = 0;
    bool tickAsked = false, tickRanWithCascade2 = false;
    std::vector<std::string> order;
    std::vector<FrameRecord> all;
    for (int f = 0; f < 160; ++f) {
        const GiStatus now = s->giStatus();
        if (!tickAsked && now.cascades[1].rebuilds > before[1] && now.cascades[2].rebuilds == before[2]) {
            s->refreshGiLighting(false);
            tickAsked = true;
        }
        e->renderOneFrame();
        std::vector<FrameRecord> recs;
        e->takeFrameRecords(recs);
        all.insert(all.end(), recs.begin(), recs.end());
        if (f > 30 && s->giStatus().giAtRest) break;
    }
    e->setFrameMonitor(MonitorLevel::Off);
    {
        std::vector<FrameRecord> recs;
        e->takeFrameRecords(recs);      // what the monitor still held (GPU samples come back late)
        all.insert(all.end(), recs.begin(), recs.end());
    }
    for (const FrameRecord &r : all) {
        bool c2Here = false;
        unsigned tickUnits = 0;
        for (const CacheWork &w : r.cacheWork) {
            if (w.cache != CacheKind::Gi) continue;
            if (w.detail == "vct.light.settle") settleInjections += w.units;
            if (w.detail == "vct.cascade2") c2Here = true;
            if (w.detail == "vct.light") tickUnits += w.units;
            if (w.detail.rfind("vct.", 0) == 0 && w.units)
                order.push_back(w.detail + "x" + std::to_string(w.units));
        }
        if (c2Here && tickUnits) { tickRanWithCascade2 = true; tickInCascade2Frame = tickUnits; }
    }
    const GiStatus g1 = s->giStatus();
    std::printf("   rebuilds per cascade after the edit:");
    std::vector<unsigned long long> delta(n);
    for (size_t i = 0; i < n; ++i) {
        delta[i] = g1.cascades[i].rebuilds - before[i];
        std::printf(" %llu", delta[i]);
    }
    std::printf("\n   GI work rows in order:");
    for (const std::string &o : order) std::printf(" %s", o.c_str());
    std::printf("\n   the at-rest tick in cascade 2's frame injected %u cascade(s); the owed settle "
                "%u; latch refusals %llu\n", tickInCascade2Frame, settleInjections,
                (unsigned long long)g1.chainInjectionRefusals);
    CHECK_MSG(delta[0] == 1u && delta[1] == 1u && delta[2] == 1u && delta[3] == 1u,
              "the edit re-voxelises each cascade once, one per frame (rebuilds %llu %llu %llu %llu)",
              delta[0], delta[1], delta[2], delta[3]);
    CHECK_MSG(tickAsked && tickRanWithCascade2 && tickInCascade2Frame == unsigned(n) - 1u,
              "THE AT-REST TICK RAN IN CASCADE 2'S REBUILD FRAME and injected the other %u "
              "cascades (the latch holding cascade 2's second injection)", tickInCascade2Frame);
    CHECK_MSG(settleInjections == 3u,
              "A CASCADE-2 REBUILD AT REST COSTS 3 SETTLE INJECTIONS (cascades 2, 1, 0), NOT THE "
              "CHAIN'S %zu: %u", n, settleInjections);
    CHECK_MSG(g1.giAtRest, "...and the chain is at rest again (giAtRest %d)", int(g1.giAtRest));

    // ---- IT IS THE FIXED POINT: a whole sweep changes nothing ---------------
    std::vector<std::string> partial(n);
    for (size_t i = 0; i < n; ++i) partial[i] = s->giVoxelStats(int(i)).lightDigest;
    e->renderOneFrame();
    Image a; view->readPixels(a);
    s->refreshGiLighting(false);             // THE WHOLE AT-REST SWEEP, outermost first
    for (int f = 0; f < 60; ++f) {
        e->renderOneFrame();
        if (f > 4 && s->giStatus().giAtRest) break;
    }
    e->renderOneFrame();
    Image b; view->readPixels(b);
    unsigned same = 0;
    for (size_t i = 0; i < n; ++i) {
        const std::string whole = s->giVoxelStats(int(i)).lightDigest;
        std::printf("   cascade %zu light digest: partial %s, whole %s\n", i,
                    partial[i].substr(0, 16).c_str(), whole.substr(0, 16).c_str());
        if (!whole.empty() && whole == partial[i]) ++same;
    }
    CHECK_MSG(same == n, "THE PARTIAL SETTLE IS THE WHOLE SWEEP'S FIXED POINT: %u of %zu cascades' "
                         "light voxels byte-identical after a whole at-rest sweep", same, n);
    unsigned moved = 0;
    for (size_t i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i)
        if (a.rgba[i] != b.rgba[i]) ++moved;
    CHECK_MSG(a.rgba.size() == b.rgba.size() && moved == 0u,
              "...and the picture is the whole sweep's picture (%u bytes differ; bar 0/255)", moved);

    // ---- 4. A LIGHT WRITTEN WHILE A PARTIAL DEBT IDLES (fix round, audit F7) ----
    // After an edit's last rebuild (cascade 3) the chain owes cascades 2, 1, 0,
    // paid on the last three of four payable frames — so the first frame after
    // that rebuild pays nothing. A light written in THAT frame must restart the
    // settle over the WHOLE chain at once (an injection reads the lights' poses
    // as it runs: a settle across a light write is the fixed point of neither),
    // and the restart must pay on the frame it is seen, not after the idle count.
    {
        GiStatus gb = s->giStatus();
        std::vector<unsigned long long> rb(n);
        for (size_t i = 0; i < n; ++i) rb[i] = gb.cascades[i].rebuilds;
        const NodeId box2 = enginetest::leakroom::addSlab(s, Colour(0.2f, 0.9f, 0.9f),
                                                          Vec3(at.x, 0.3f, at.z + 2.0f),
                                                          Vec3(0.6f, 0.6f, 0.6f));
        CHECK(box2 != 0, "the second edit's box exists");
        s->refreshGlobalIllumination();
        e->setFrameMonitor(MonitorLevel::Review);
        bool wrote = false;
        int writeFrame = -1, firstSettleAfter = -1;
        unsigned settleAfter = 0;
        std::vector<FrameRecord> keep;
        for (int f = 0; f < 160; ++f) {
            if (!wrote && s->giStatus().cascades[3].rebuilds > rb[3]) {
                LightDesc l;
                l.type = LightType::Directional;
                l.colour = Colour(1.f, 1.f, 1.f);
                l.intensity = 2.6f / 3.14159265f;     // was 2.0 / pi
                CHECK(s->setLight(sun, l), "the sun's intensity is written mid-debt");
                wrote = true;
                writeFrame = f;
            }
            e->renderOneFrame();
            std::vector<FrameRecord> recs;
            e->takeFrameRecords(recs);
            keep.insert(keep.end(), recs.begin(), recs.end());
            if (f > writeFrame + 20 && wrote && s->giStatus().giAtRest) break;
        }
        e->setFrameMonitor(MonitorLevel::Off);
        {
            std::vector<FrameRecord> recs;
            e->takeFrameRecords(recs);
            keep.insert(keep.end(), recs.begin(), recs.end());
        }
        // The records carry the engine's frame numbers; the write happened before
        // the frame whose cascade-3 rebuild row came first, plus one.
        unsigned long long c3Frame = 0;
        for (const FrameRecord &r : keep)
            for (const CacheWork &w : r.cacheWork)
                if (w.cache == CacheKind::Gi && w.detail == "vct.cascade3" && !c3Frame) c3Frame = r.frame;
        for (const FrameRecord &r : keep) {
            if (r.frame <= c3Frame) continue;
            for (const CacheWork &w : r.cacheWork)
                if (w.cache == CacheKind::Gi && w.detail == "vct.light.settle" && w.units) {
                    settleAfter += w.units;
                    if (firstSettleAfter < 0) firstSettleAfter = int(r.frame - c3Frame);
                }
        }
        std::printf("   light write in the idle frame after cascade 3's rebuild: settle injections "
                    "after it %u (the whole chain is %zu), the first %d frame(s) after the rebuild\n",
                    settleAfter, n, firstSettleAfter);
        CHECK_MSG(wrote && settleAfter == unsigned(n),
                  "A LIGHT WRITTEN MID-DEBT RESTARTS THE SETTLE OVER THE WHOLE CHAIN: %u injections",
                  settleAfter);
        CHECK_MSG(firstSettleAfter == 1,
                  "...PAID FROM THE FRAME THE WRITE IS SEEN (the first settle step is %d frame(s) "
                  "after the rebuild; 1 = no idle frame)", firstSettleAfter);
        std::vector<std::string> mid(n);
        for (size_t i = 0; i < n; ++i) mid[i] = s->giVoxelStats(int(i)).lightDigest;
        s->refreshGiLighting(false);
        for (int f = 0; f < 60; ++f) {
            e->renderOneFrame();
            if (f > 4 && s->giStatus().giAtRest) break;
        }
        unsigned same2 = 0;
        for (size_t i = 0; i < n; ++i)
            if (s->giVoxelStats(int(i)).lightDigest == mid[i]) ++same2;
        CHECK_MSG(same2 == n, "...and it leaves the whole sweep's fixed point under the NEW light "
                              "(%u of %zu cascades byte-identical after a whole at-rest sweep)", same2, n);
    }

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
