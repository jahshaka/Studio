// gi.field_dependency — THE IRRADIANCE FIELD GENERATES ONLY WHEN ITS INPUTS CHANGED
// (ENGINE-UPGRADE-2 stage 2; the ASYNC spec's capture: IrradianceField/Gen ran on EVERY
// frame of a mover fixture at 4.9-5.7 ms).
//
// The field's work is filed by the monitor as cache work (`ifd.*` details: converge,
// replace, refine, follow). On a converged room with the sun:
//   A. THE CAMERA WALKS (1.4 m/s), nothing else moves: the field's inputs (the light in
//      the voxels, the geometry) did not change — only a follow of the cascade-0 window
//      may run (a scroll integrates the probes that entered), never a re-integration.
//   B. TWO MOVERS move (no cards, not voxelised), the camera still, the field converged:
//      the voxel light does not change with them — no field work at all.
//   A'. THE WALK'S SCROLLS integrate the entered planes and owe their refinements over
//      those planes only (PhotonIrradianceField's band): the work is bounded by the
//      entered probes x the target samples, never a whole-grid refinement per scroll
//      (RED ON THE BASE: 41,472 integrations in the walk and a whole-grid debt of
//      129,024 more over the next 126 frames).
// Counts per frame, never clocks.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const unsigned kSize = 96;

static float halfToFloat(unsigned short h) {
    const unsigned s = (h >> 15) & 1u, e = (h >> 10) & 0x1Fu, m = h & 0x3FFu;
    float v;
    if (e == 0) v = std::ldexp(float(m), -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp(float(m | 0x400u), int(e) - 25);
    return s ? -v : v;
}

struct FieldWork {
    unsigned frames = 0, framesWithWork = 0;
    unsigned long long probes = 0;   ///< probe integrations (the work's units)
    double gpuMs = 0.0;              ///< ...and their GPU ms where timed
    std::map<std::string, unsigned> byDetail;   // frames carrying each detail
};

static FieldWork fieldWork(const std::vector<FrameRecord> &recs) {
    FieldWork w;
    for (const FrameRecord &r : recs) {
        ++w.frames;
        bool any = false;
        std::map<std::string, bool> seen;
        for (const CacheWork &c : r.cacheWork)
            if (c.detail.rfind("ifd.", 0) == 0) {
                any = true;
                seen[c.detail] = true;
                w.probes += c.units;
                if (c.gpuMs > 0.0f) w.gpuMs += c.gpuMs;
            }
        if (any) ++w.framesWithWork;
        for (const auto &kv : seen) ++w.byDetail[kv.first];
    }
    return w;
}

static void print(const char *tag, const FieldWork &w) {
    std::printf("   [%s] %u frames, %u with field work, %llu probe integrations, %.2f GPU ms:", tag, w.frames,
                w.framesWithWork, w.probes, w.gpuMs);
    for (const auto &kv : w.byDetail) std::printf(" %s x%u", kv.first.c_str(), kv.second);
    std::printf("\n");
}

int main(int argc, char **argv)
{
    // --stills: the measurement of STILL objects moved every frame (by hand; not the row).
    const bool stillsArm = argc > 1 && std::string(argv[1]) == "--stills";
    // --reach <bounces>: THE TRUE REACH OF A STILL'S MOVE (ENGINE-UPGRADE-2B stage 4's instrument, a
    // measurement): with the field's rays NOT rotated (gi.fieldStatic) an unchanged probe re-integrates
    // to the same bytes, so the probes whose atlas tiles differ after the move has settled are exactly
    // the probes whose integrand the move changed.
    const bool reachControl = argc > 1 && std::string(argv[1]) == "--reach-control";
    // --variance: THE PER-PROBE STANDARD ERROR, SAMPLE BY SAMPLE (ENGINE-UPGRADE-2B stage 4's
    // instrument, a measurement): an at-rest re-integration of the converged room (nothing
    // changed), the atlas read after every pass; each probe's per-sample values follow from its
    // running means (x_n = n m_n - (n - 1) m_{n-1}), and with them its standard error.
    const bool varianceArm = argc > 1 && std::string(argv[1]) == "--variance";
    const bool reachArm = (argc > 1 && std::string(argv[1]) == "--reach") || reachControl;
    const int reachBounces = argc > 2 ? std::atoi(argv[2]) : 1;
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-field-dependency-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();

    View *view = e->createOffscreenView("fielddep", kSize, kSize, Colour(0, 0, 0));
    Scene *s = e->createScene("fielddep");
    view->setScene(s);
    view->setOffscreenContract(OffscreenContract::Live);
    view->setShadows(true);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.04f, 0.04f, 0.04f));
    const auto slab = [&](const Vec3 &pos, const Vec3 &scale) {
        const NodeId n = enginetest::addTestCube(s, Colour(0.7f, 0.7f, 0.7f), 0.0f, 0.9f);
        enginetest::setNodePosition(s, n, pos);
        enginetest::setNodeScale(s, n, scale);
        return n;
    };
    slab(Vec3(0.0f, -0.05f, 0.0f), Vec3(12.0f, 0.1f, 12.0f));   // floor
    slab(Vec3(-6.0f, 2.0f, 0.0f), Vec3(0.2f, 4.0f, 12.0f));     // walls
    slab(Vec3(6.0f, 2.0f, 0.0f), Vec3(0.2f, 4.0f, 12.0f));
    slab(Vec3(0.0f, 2.0f, -6.0f), Vec3(12.0f, 4.0f, 0.2f));
    NodeId movers[2], stills[2];
    for (int i = 0; i < 2; ++i) {
        stills[i] = enginetest::addTestCube(s, Colour(0.2f, 0.5f, 0.8f), 0.0f, 0.6f);
        enginetest::setNodePosition(s, stills[i], Vec3(-2.5f + 5.0f * float(i), 0.5f, -3.0f));
        movers[i] = enginetest::addTestCube(s, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.6f);
        s->setNodeMovable(movers[i], true);
        enginetest::setNodePosition(s, movers[i], Vec3(-1.5f + 3.0f * float(i), 0.5f, 0.0f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = reachArm ? reachBounces : 1;
    gi.ddgi = GiToggle::On;   // the field (the irradiance field's generation) is the subject
    if (reachArm) e->setArm("gi.fieldStatic", 1.0);
    s->setGlobalIllumination(gi);
    Vec3 cam(0.0f, 1.6f, 4.0f);
    enginetest::testCameraLookAt(view, cam, Vec3(0.0f, 1.0f, -4.0f));
    // CONVERGE: render until the field has no work for 30 frames running (bounded).
    e->setFrameMonitor(MonitorLevel::Review);
    int quiet = 0, settle = 0;
    unsigned settleWork = 0;
    for (; settle < 1200 && quiet < 30; ++settle) {
        e->renderOneFrame();
        std::vector<FrameRecord> recs;
        e->takeFrameRecords(recs);
        const FieldWork w = fieldWork(recs);
        settleWork += w.framesWithWork;
        quiet = (w.frames && !w.framesWithWork) ? quiet + int(w.frames) : (w.frames ? 0 : quiet);
    }
    std::printf("   converged after %d frames (quiet %d; %u frames with field work)\n", settle, quiet, settleWork);
    CHECK(quiet >= 30, "the field converges and goes quiet on a still scene");
    const GiStatus g0 = s->giStatus();
    std::printf("   the field: bound %d, %d probes, %d a frame\n", g0.ifdBound ? 1 : 0, g0.ifdProbes,
                g0.ifdProbesPerFrame);
    CHECK(g0.ifdBound && g0.ifdProbes > 0, "the scene's irradiance field is live (not vacuous)");

    if (varianceArm) {
        // Settled already (above). Re-integrate with nothing changed, reading the atlas after
        // every pass: a pass is done when the refinements owed move (or the field goes quiet).
        std::vector<GiFieldAtlas> passes;
        unsigned owedSeen = s->giStatus().ifdRefinesOwed;   // 0: converged
        s->refreshGiLighting(false);
        int quietRun = 0;
        bool started = false;
        GiFieldAtlas prev;
        for (int f = 0; f < 600 && quietRun < 20; ++f) {
            e->renderOneFrame();
            std::vector<FrameRecord> recs;
            e->takeFrameRecords(recs);
            const FieldWork w = fieldWork(recs);
            quietRun = (w.frames && !w.framesWithWork) ? quietRun + 1 : 0;
            const unsigned owed = s->giStatus().ifdRefinesOwed;
            // A PASS ENDED on the frame before the refinements owed fall (this frame began the next)
            // or on the last frame with work (the field went quiet); the reset raising them
            // (0 -> K - 1) is the event's own pass beginning, not an end. The atlas of the frame
            // before is kept for that.
            const bool began = owedSeen != ~0u && owed > owedSeen;
            if ((owedSeen != ~0u && owed < owedSeen) || (quietRun == 1 && started)) passes.push_back(prev);
            if (began) started = true;
            owedSeen = owed;
            if (started) { prev = GiFieldAtlas(); s->giFieldAtlas(prev); }
        }
        // The quiet frame's read repeats the last pass when the refinement's own boundary already
        // captured it: one pass per sample, the target's count.
        const unsigned target = s->giStatus().ifdTargetSamples;
        if (passes.size() > target) passes.resize(target);
        const unsigned K = unsigned(passes.size());
        if (!K || !passes[0].available) { std::printf("FAIL: no atlas passes read\n"); return 1; }
        const GiFieldAtlas &a0 = passes[0];
        const unsigned np = a0.probes[0] * a0.probes[1] * a0.probes[2];
        const unsigned bordered = a0.irradBordered, res = bordered - 2u, width = a0.irradWidth;
        const unsigned bpp = a0.irradBytesPerTexel;
        std::printf("   %u passes read (the target %u samples), %u probes, %ux%u texels each, %u bytes a texel\n", K,
                    s->giStatus().ifdTargetSamples, np, res, res, bpp);
        const auto lum = [&](const GiFieldAtlas &a, unsigned p, unsigned tx, unsigned ty) {
            const unsigned x = (p * bordered) % width + 1u + tx, y = ((p * bordered) / width) * bordered + 1u + ty;
            const unsigned short *h = reinterpret_cast<const unsigned short *>(&a.irradiance[(size_t(y) * width + x) * bpp]);
            return 0.2126f * halfToFloat(h[0]) + 0.7152f * halfToFloat(h[1]) + 0.0722f * halfToFloat(h[2]);
        };
        // Per probe: the sample index n at which 2 SE / |mean| <= 0.16 holds for EVERY texel of it
        // (the precision K was derived for), and the median relative sample deviation.
        const unsigned checkpoints[4] = { 3u, 5u, 10u, 25u };
        unsigned within[4] = { 0, 0, 0, 0 };
        unsigned long long workAtStop[2] = { 0, 0 };   // samples spent with the stop at n >= 2, n >= 4
        std::vector<float> relSigma;
        relSigma.reserve(size_t(np) * res * res);
        unsigned dark = 0;
        for (unsigned p = 0; p < np; ++p) {
            unsigned stopAt = K + 1u;   // the first n (>= 2) where every texel meets the bound
            std::vector<double> sum(res * res, 0.0), sum2(res * res, 0.0);
            bool anyLit = false;
            for (unsigned n = 1; n <= K; ++n) {
                bool all = true;
                for (unsigned i = 0; i < res * res; ++i) {
                    const unsigned tx = i % res, ty = i / res;
                    const double mn = lum(passes[n - 1], p, tx, ty);
                    const double mp = n > 1 ? lum(passes[n - 2], p, tx, ty) : 0.0;
                    const double x = double(n) * mn - double(n - 1) * mp;   // the n-th sample
                    sum[i] += x; sum2[i] += x * x;
                    const double mean = sum[i] / n;
                    if (mean > 1e-6) anyLit = true;
                    if (n < 2) { all = false; continue; }
                    const double var = std::max(0.0, (sum2[i] - n * mean * mean) / double(n - 1));
                    const double se = std::sqrt(var / n);
                    if (mean <= 1e-6) continue;   // an unlit texel: no relative error to bound
                    if (2.0 * se / mean > 0.16) all = false;
                    if (n == K) relSigma.push_back(float(std::sqrt(var) / mean));
                }
                if (all && stopAt > K) stopAt = n;
            }
            if (!anyLit) ++dark;
            workAtStop[0] += std::min(stopAt, K);
            workAtStop[1] += std::min(std::max(stopAt, 4u), K);
            for (int c = 0; c < 4; ++c) if (stopAt <= checkpoints[c]) ++within[c];
        }
        std::sort(relSigma.begin(), relSigma.end());
        const auto pct = [&](double q) { return relSigma.empty() ? 0.0 : double(relSigma[size_t(q * (relSigma.size() - 1))]); };
        std::printf("   ONE SAMPLE'S RELATIVE DEVIATION per texel (sigma / mean over %zu lit texels): median %.4f, "
                    "p90 %.4f, p99 %.4f, max %.4f\n", relSigma.size(), pct(0.5), pct(0.9), pct(0.99),
                    relSigma.empty() ? 0.0 : double(relSigma.back()));
        std::printf("   PROBES AT THE PRECISION (2 SE / mean <= 0.16 on every texel), of %u (%u unlit):", np, dark);
        for (int c = 0; c < 4; ++c) std::printf(" by sample %u: %u (%.1f %%)", checkpoints[c], within[c], 100.0 * within[c] / np);
        std::printf("\n");
        std::printf("   THE WORK a variance stop would spend (samples, all probes): %llu stopping from sample 2, %llu "
                    "from sample 4, against %llu at the target (%.2fx / %.2fx less)\n", workAtStop[0], workAtStop[1],
                    (unsigned long long)np * K, double(np) * K / double(workAtStop[0] ? workAtStop[0] : 1),
                    double(np) * K / double(workAtStop[1] ? workAtStop[1] : 1));
        e->setFrameMonitor(MonitorLevel::Off);
        return 0;
    }
    if (reachArm) {
        const auto settleField = [&]() {
            int q = 0, n = 0;
            for (; n < 2000 && q < 30; ++n) {
                e->renderOneFrame();
                std::vector<FrameRecord> recs;
                e->takeFrameRecords(recs);
                const FieldWork w = fieldWork(recs);
                q = (w.frames && !w.framesWithWork) ? q + int(w.frames) : (w.frames ? 0 : q);
            }
            return n;
        };
        settleField();
        GiFieldAtlas a0, a1;
        CHECK(s->giFieldAtlas(a0) && a0.available, "the field's atlas reads back (before)");
        // THE CONTROL: a re-integration with nothing changed (an at-rest light tick re-injects the
        // chain and resets the field) — every probe must come back to the same bytes, or the
        // instrument measures noise, not reach.
        if (reachControl) s->refreshGiLighting(false);
        else enginetest::setNodePosition(s, stills[0], Vec3(-2.0f, 0.5f, -3.0f));
        const int frames = settleField();
        CHECK(s->giFieldAtlas(a1) && a1.available, "the field's atlas reads back (after)");
        const unsigned np = a0.probes[0] * a0.probes[1] * a0.probes[2];
        const unsigned bordered = a0.irradBordered, bpp = a0.irradBytesPerTexel, width = a0.irradWidth;
        unsigned changed = 0;
        for (unsigned p = 0; p < np; ++p) {
            const unsigned x0 = (p * bordered) % width, y0 = ((p * bordered) / width) * bordered;
            bool diff = false;
            for (unsigned y = 0; y < bordered && !diff; ++y) {
                const size_t row = (size_t(y0 + y) * width + x0) * bpp;
                diff = std::memcmp(&a0.irradiance[row], &a1.irradiance[row], size_t(bordered) * bpp) != 0;
            }
            if (diff) ++changed;
        }
        std::printf("   REACH at %d bounce(s): %s — %u of %u probes changed (%.1f %%), "
                    "settled in %d frames\n", reachBounces,
                    reachControl ? "THE CONTROL, a re-integration with nothing changed" : "one still moved 0.5 m", changed, np, 100.0 * changed / (np ? np : 1), frames);
        e->setFrameMonitor(MonitorLevel::Off);
        return failures ? 1 : 0;
    }
    const auto run = [&](const char *tag, int frames, bool walk, bool move, bool moveStills = false) {
        std::vector<FrameRecord> all;
        for (int f = 0; f < frames; ++f) {
            if (walk) {
                cam.z -= 1.4f / 60.0f;
                enginetest::testCameraLookAt(view, cam, Vec3(cam.x, 1.0f, cam.z - 8.0f));
            }
            if (move)
                for (int i = 0; i < 2; ++i)
                    enginetest::setNodePosition(s, movers[i], Vec3(-1.5f + 3.0f * float(i) + 0.5f * float((f / 6) % 2),
                                                                   0.5f, 0.02f * float(f)));
            if (moveStills)
                for (int i = 0; i < 2; ++i)
                    enginetest::setNodePosition(s, stills[i], Vec3(-2.5f + 5.0f * float(i) + 0.4f * std::sin(0.05f * float(f)),
                                                                   0.5f, -3.0f));
            e->renderOneFrame();
            std::vector<FrameRecord> recs;
            e->takeFrameRecords(recs);
            all.insert(all.end(), recs.begin(), recs.end());
        }
        for (int f = 0; f < 6; ++f) {
            e->renderOneFrame();
            std::vector<FrameRecord> recs;
            e->takeFrameRecords(recs);
            all.insert(all.end(), recs.begin(), recs.end());
        }
        const FieldWork w = fieldWork(all);
        print(tag, w);
        return w;
    };
    // B FIRST, on the converged field: two movers, the camera still.
    const FieldWork movers2 = run("two movers", 120, false, true);
    CHECK(movers2.framesWithWork == 0u,
          "MOVERS GENERATE NO FIELD: no field work on any frame (movers are not in the voxels the field reads)");
    // A: the camera walks; the field's window follows cascade 0 and scrolls.
    const GiStatus g0w = s->giStatus();
    const FieldWork walk = run("camera walks", 120, true, false);
    const GiStatus g1 = s->giStatus();
    const unsigned long long scrolls = g1.ifdScrolls - g0w.ifdScrolls;
    const unsigned long long bound = (unsigned long long)g1.ifdScrollProbes * g1.ifdTargetSamples * scrolls;
    std::printf("   follows %llu (scrolls %llu, replacements %llu); the last scroll entered %u probes, the "
                "target %u samples; refines owed %u at the end\n",
                g1.ifdFollows - g0w.ifdFollows, scrolls, g1.ifdReplacements - g0w.ifdReplacements,
                g1.ifdScrollProbes, g1.ifdTargetSamples, g1.ifdRefinesOwed);
    const unsigned walkReintegrations = walk.byDetail.count("ifd.converge") ? walk.byDetail.at("ifd.converge") : 0u;
    CHECK(scrolls > 0u && g1.ifdReplacements == g0w.ifdReplacements,
          "the walk scrolls the field's window (not vacuous; no re-placement)");
    CHECK(walkReintegrations == 0u,
          "A WALKING CAMERA RE-INTEGRATES NOTHING: no ifd.converge frame (the light and the geometry did not change)");
    CHECK(walk.probes <= bound,
          ("A SCROLL'S WORK IS WHAT IT TOUCHED: " + std::to_string(walk.probes) +
           " probe integrations <= the entered probes x the target samples (" + std::to_string(bound) +
           ") — never a whole-grid refinement").c_str());
    // C (--stills, a measurement, printed): STILL objects moved every frame — the voxels
    // re-built under them, a real change of the field's input; today's orchestration
    // re-integrates the WHOLE field per re-injection (OgreGi.cpp, stage 4's).
    if (stillsArm) {
        for (int i = 0; i < 300; ++i) e->renderOneFrame();
        { std::vector<FrameRecord> drop; e->takeFrameRecords(drop); }
        run("stills move", 120, false, false, true);
        // ONE STILL MOVED ONCE (ENGINE-UPGRADE-2B stage 4's instrument): the field's work from the
        // move until it settles — its frames, its probe integrations, its GPU ms.
        for (int i = 0; i < 300; ++i) e->renderOneFrame();
        { std::vector<FrameRecord> drop; e->takeFrameRecords(drop); }
        enginetest::setNodePosition(s, stills[0], Vec3(-2.0f, 0.5f, -3.0f));
        std::vector<FrameRecord> one;
        for (int f = 0; f < 240; ++f) {
            e->renderOneFrame();
            std::vector<FrameRecord> recs;
            e->takeFrameRecords(recs);
            one.insert(one.end(), recs.begin(), recs.end());
        }
        const GiStatus gs = s->giStatus();
        print("one still moved once", fieldWork(one));
        std::printf("   the field: %d probes (a whole re-integration is %d x the target %u samples)\n", gs.ifdProbes,
                    gs.ifdProbes, gs.ifdTargetSamples);
    }
    e->setFrameMonitor(MonitorLevel::Off);
    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
