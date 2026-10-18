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
//   S. (--stop, the row gi.field_variance_stop; ENGINE-UPGRADE-2B stage 4) THE PER-PROBE
//      VARIANCE STOP against its oracle, in one process. The ORACLE first: the stop off
//      (gi.fieldStopOff), the room converged to the target (25 samples), every probe's running
//      mean read at every count n it passes through. Then the STOP: the field rebuilt with the
//      stop on and converged. The rays' rotations are a function of the probe's lattice point
//      and its sample index, so the stop's n samples ARE the oracle's first n. THE BARS, declared
//      before the measurement:
//        S1 the stop's estimate of every probe is the oracle's own running mean at the probe's
//           count n, byte for byte (the stop changes WHEN a probe stops, never what it holds);
//        S2 statistical - over the lit texels of the stopped probes, z = (stop - oracle 25) /
//           (sigma sqrt(1/n - 1/25)), sigma the texel's one-sample deviation from the oracle's
//           25 samples (its second moment; the difference of two nested means has that
//           deviation): at least 93 % within |z| <= 2 (a Student t of 24 degrees of freedom
//           covers 94.3 %; the slack is the 36 correlated texels of a probe);
//        S3 the probes' stated errors hold AS A 2-SIGMA STATEMENT: at most 5 % of the stopped probes
//           with a mean |z| over their lit texels above 2. (Declared first as "every probe at most
//           2", on the belief that a probe's 36 texels are 36 draws whose mean |z| concentrates at
//           0.80 - they are ONE draw: every texel integrates the same rays, so a probe's mean |z|
//           is distributed as one |z| and 5 % of a correct estimator's probes exceed 2. Measured:
//           7 of 8192 at a stop from 4 samples, 12 at 6, the oracle's own replay. The bar that
//           could not be met by any estimator is restated here, flagged in the stage 4 report.)
//        S3b the precision itself, at the same confidence: at least 95 % of the stopped probes' lit
//           texels within e (0.16) of the oracle (the derivation's two sigma);
//        S4 the probe work (the generation job's own count of integrations): the stop's at most
//           half the oracle's (the instrument, --variance, predicts 5.9x less);
//        S5 at rest after it: no integration in 30 frames, giAtRest.
//        S6 the RESET path (a light tick, the common event) stops like the build: its re-integration
//           at most half the oracle's;
//      The early end (every probe stopped: no refinement owed) trusts the read-back count only when
//      no newer non-refining work was defined after the reading was asked for (the stale-reading
//      guard: PhotonIrradianceField::noteEstimatesReplaced).
// Counts per frame, never clocks.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/PhotonStatus.h"
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

static FieldWork fieldWork(const std::vector<MonitorRecord> &recs) {
    FieldWork w;
    for (const MonitorRecord &r : recs) {
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
    // --stop-sliver (the row gi.field_variance_stop_sliver): THE HEAVY TAIL - the same bars on a DARK room lit
    // only by an emissive sliver (no sun, no ambient): a probe that sees the sliver through a few of its rays
    // has an integrand that is rare-and-bright, so its first samples can all miss it (M2 near 0) and a stop at
    // n = 4 on them would be biased low. S3b on it says whether the stop's minimum holds there.
    const bool sliverArm = argc > 1 && std::string(argv[1]) == "--stop-sliver";
    const bool stopArm = (argc > 1 && std::string(argv[1]) == "--stop") || sliverArm;
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
    if (sliverArm) s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    else s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.04f, 0.04f, 0.04f));
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
    if (sliverArm) {
        // The sliver: 0.6 x 0.6 m, one 5 cm thick, emitting, 2 m up in the room's middle - from most probes a
        // source of a few rays' solid angle.
        PbrParams ep;
        ep.albedo = Colour(0, 0, 0);
        ep.roughness = 1.0f;
        ep.emissive = Colour(4, 4, 4);
        const NodeId sl = s->createNode();
        s->attachMesh(sl, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(ep));
        enginetest::setNodePosition(s, sl, Vec3(0.0f, 2.0f, -1.5f));
        enginetest::setNodeScale(s, sl, Vec3(0.6f, 0.6f, 0.05f));
    } else {
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
    }
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = reachArm ? reachBounces : 1;
    gi.ddgi = GiToggle::On;   // the field (the irradiance field's generation) is the subject
    if (reachArm) e->setArm("gi.fieldStatic", 1.0);
    if (stopArm) e->setArm("gi.fieldStopOff", 1.0);   // the oracle first: every probe to the target
    s->setGlobalIllumination(gi);
    Vec3 cam(0.0f, 1.6f, 4.0f);
    enginetest::testCameraLookAt(view, cam, Vec3(0.0f, 1.0f, -4.0f));
    // THE ORACLE'S RUNNING MEANS (--stop): every probe's tile (interior, raw RGBA16F) at the first
    // frame its count reads n, n = 1..25.
    const unsigned kOracleN = 25u;
    std::vector<std::vector<unsigned char>> oracle;   // [probe * kOracleN + n - 1] = the tile's bytes
    unsigned oRes = 0, oBordered = 0, oWidth = 0, oBpp = 0, oProbes = 0;
    const auto tileAt = [](const GiFieldAtlas &a, unsigned p, unsigned tx, unsigned ty) {
        const unsigned b = a.irradBordered;
        const unsigned x = (p * b) % a.irradWidth + 1u + tx, y = ((p * b) / a.irradWidth) * b + 1u + ty;
        return (size_t(y) * a.irradWidth + x) * a.irradBytesPerTexel;
    };
    const auto captureOracle = [&]() {
        GiFieldAtlas a;
        if (!s->giFieldAtlas(a) || !a.available) return;
        if (!oProbes) {
            oProbes = a.probes[0] * a.probes[1] * a.probes[2];
            oBordered = a.irradBordered; oRes = oBordered - 2u; oWidth = a.irradWidth; oBpp = a.irradBytesPerTexel;
            oracle.assign(size_t(oProbes) * kOracleN, std::vector<unsigned char>());
        }
        for (unsigned p = 0; p < oProbes; ++p) {
            const unsigned short *h = reinterpret_cast<const unsigned short *>(&a.irradiance[tileAt(a, p, 0, 0)]);
            const float nf = halfToFloat(h[3]);
            const unsigned n = unsigned(nf + 0.5f);
            if (n < 1u || n > kOracleN) continue;
            std::vector<unsigned char> &slot = oracle[size_t(p) * kOracleN + n - 1u];
            if (!slot.empty()) continue;
            slot.resize(size_t(oRes) * oRes * oBpp);
            for (unsigned ty = 0; ty < oRes; ++ty)
                std::memcpy(&slot[size_t(ty) * oRes * oBpp], &a.irradiance[tileAt(a, p, 0, ty)], size_t(oRes) * oBpp);
        }
    };
    // CONVERGE: render until the field has no work for 30 frames running (bounded).
    e->setFrameMonitor(MonitorLevel::Review);
    int quiet = 0, settle = 0;
    unsigned settleWork = 0;
    for (; settle < 1200 && quiet < 30; ++settle) {
        enginetest::frames(e, 1);
        if (stopArm) captureOracle();
        std::vector<MonitorRecord> recs;
        e->popMonitorRecords(recs);
        const FieldWork w = fieldWork(recs);
        settleWork += w.framesWithWork;
        quiet = (w.frames && !w.framesWithWork) ? quiet + int(w.frames) : (w.frames ? 0 : quiet);
    }
    std::printf("   converged after %d frames (quiet %d; %u frames with field work)\n", settle, quiet, settleWork);
    CHECK(quiet >= 30, "the field converges and goes quiet on a still scene");
    const GiStatus g0 = s->published().gi;
    std::printf("   the field: bound %d, %d probes, %d a frame\n", g0.ifdBound ? 1 : 0, g0.ifdProbes,
                g0.ifdProbesPerFrame);
    CHECK(g0.ifdBound && g0.ifdProbes > 0, "the scene's irradiance field is live (not vacuous)");

    if (stopArm) {
        const PhotonFieldStatus fo = photonFieldStatus(*s);
        std::printf("   THE ORACLE (the stop off): converged in %d frames, %llu integrations, %u probes\n", settle,
                    fo.integrations, fo.probes);
        CHECK(fo.field && fo.stopMinSamples == 0u, "the oracle's field runs with the stop off");
        // S6's half: A LIGHT TICK, the common event (an at-rest re-injection: the field's RESET path, not the
        // build's), settled - the job's integrations and the frames it took.
        const auto lightTick = [&](unsigned long long &integrations, int &frames) {
            { std::vector<MonitorRecord> drop; e->popMonitorRecords(drop); }   // the frames before are not the tick's
            const PhotonFieldStatus t0 = photonFieldStatus(*s);
            s->refreshGiLighting(false);
            int q = 0, f = 0;
            for (; f < 1200 && q < 30; ++f) {
                enginetest::frames(e, 1);
                std::vector<MonitorRecord> recs;
                e->popMonitorRecords(recs);
                const FieldWork w = fieldWork(recs);
                q = (w.frames && !w.framesWithWork) ? q + int(w.frames) : (w.frames ? 0 : q);
            }
            integrations = photonFieldStatus(*s).integrations - t0.integrations;
            frames = f - q;
        };
        // THE ORACLE'S TICK IS ITS BUILD'S WORK: with the stop off a light tick re-integrates the whole grid to the
        // target exactly as the build does (measured once: 203,776 integrations over 203 frames against the build's
        // 204,800), so the row compares the stop's tick with the oracle's build and does not pay a second 200-frame
        // convergence (source.row_budget's 10 s).
        unsigned long long tickOn = 0;
        const unsigned long long tickOff = fo.integrations;
        int tickOnFrames = 0;
        // THE STOP: the field rebuilt with the stop on (the arm reads at the build: the field off, then on).
        e->setArm("gi.fieldStopOff", 0.0);
        enginetest::frames(e, 1);
        GiParams off = gi;
        off.ddgi = GiToggle::Off;
        s->setGlobalIllumination(off);
        enginetest::frames(e, 4);
        s->setGlobalIllumination(gi);
        int sq = 0, sf = 0;
        for (; sf < 1200 && sq < 30; ++sf) {
            enginetest::frames(e, 1);
            std::vector<MonitorRecord> recs;
            e->popMonitorRecords(recs);
            const FieldWork w = fieldWork(recs);
            sq = (w.frames && !w.framesWithWork) ? sq + int(w.frames) : (w.frames ? 0 : sq);
        }
        const PhotonFieldStatus fs = photonFieldStatus(*s);
        std::printf("   THE STOP (n >= %u, the 95 %% t interval / mean <= %.2f): converged in %d frames, %llu integrations, %u of %u "
                    "probes stopped\n", fs.stopMinSamples, double(fs.stopPrecision), sf, fs.integrations,
                    fs.probesStopped, fs.probes);
        CHECK(fs.field && fs.stopMinSamples > 0u, "the stop's field runs with the stop on");
        CHECK(sq >= 30, "the stop's field converges and goes quiet");
        GiFieldAtlas sa;
        CHECK(s->giFieldAtlas(sa) && sa.available && oProbes &&
                  sa.probes[0] * sa.probes[1] * sa.probes[2] == oProbes && sa.irradBordered == oBordered,
              "the stop's atlas reads back, the oracle's layout");
        // S5: at rest.
        enginetest::frames(e, 30);
        const PhotonFieldStatus fr = photonFieldStatus(*s);
        std::printf("   at rest: %llu integrations in 30 frames, giAtRest %d\n", fr.integrations - fs.integrations,
                    s->published().gi.giAtRest ? 1 : 0);
        CHECK(fr.integrations == fs.integrations && s->published().gi.giAtRest,
              "S5: AT REST after the stop: no integration in 30 frames, the scene at rest");
        // S4: the work.
        std::printf("   THE WORK: %llu integrations with the stop, %llu without (%.2fx less)\n", fs.integrations,
                    fo.integrations, double(fo.integrations) / double(fs.integrations ? fs.integrations : 1));
        // THE SLIVER ROW ASSERTS THE STOP'S CORRECTNESS (S1, S3b, S5), not its saving: in a room lit only by a
        // small emitter almost every probe has a texel that has read no light, and such a texel keeps its probe
        // to the target - the work bars (S4, S6) and the coverage over a handful of stopped probes (S2, S3) are
        // printed there, not asserted.
        if (!sliverArm)
            CHECK(fs.integrations > 0 && 2u * fs.integrations <= fo.integrations,
                  "S4: THE STOP'S PROBE WORK is at most half the oracle's");
        // S6: THE RESET PATH STOPS LIKE THE BUILD - a light tick's re-integration with the stop on.
        if (!sliverArm) lightTick(tickOn, tickOnFrames);   // the sliver row asserts no work bar: it pays no tick
        if (!sliverArm)
        std::printf("   A LIGHT TICK (the reset path): %llu integrations over %d frames with the stop, against the "
                    "oracle's %llu (%.2fx less)\n", tickOn, tickOnFrames, tickOff,
                    double(tickOff) / double(tickOn ? tickOn : 1));
        if (!sliverArm)
            CHECK(tickOn > 0 && 2u * tickOn <= tickOff, "S6: A LIGHT TICK'S RE-INTEGRATION STOPS too: at most half the oracle's work");
        // S1 - S3, per probe.
        const auto lumOf = [](const unsigned char *t) {
            const unsigned short *h = reinterpret_cast<const unsigned short *>(t);
            return 0.2126 * halfToFloat(h[0]) + 0.7152 * halfToFloat(h[1]) + 0.0722 * halfToFloat(h[2]);
        };
        unsigned replayed = 0, missing = 0, stoppedEarly = 0, badProbes = 0;
        unsigned long long litTexels = 0, within2 = 0;
        double worstMeanZ = 0.0, worstPrecision = 0.0;
        unsigned long long withinE = 0;
        std::vector<unsigned> byN(kOracleN + 1u, 0u);
        const size_t tileBytes = size_t(oRes) * oRes * oBpp;
        std::vector<unsigned char> st(tileBytes);
        for (unsigned p = 0; p < oProbes; ++p) {
            for (unsigned ty = 0; ty < oRes; ++ty)
                std::memcpy(&st[size_t(ty) * oRes * oBpp], &sa.irradiance[tileAt(sa, p, 0, ty)], size_t(oRes) * oBpp);
            const unsigned n = unsigned(halfToFloat(reinterpret_cast<const unsigned short *>(st.data())[3]) + 0.5f);
            if (n >= 1u && n <= kOracleN) ++byN[n];
            bool have = n >= 1u && n <= kOracleN;
            for (unsigned k = 0; have && k < kOracleN; ++k) have = !oracle[size_t(p) * kOracleN + k].empty();
            if (!have) { ++missing; continue; }
            // S1: the oracle's own mean at n (the alpha included: the count).
            if (std::memcmp(st.data(), oracle[size_t(p) * kOracleN + n - 1u].data(), tileBytes) == 0) ++replayed;
            if (n >= kOracleN) continue;
            ++stoppedEarly;
            double sumZ = 0.0, worstStated = 0.0, worstTrue = 0.0, worstRel = 0.0, worstStatedErr = 0.0;
            unsigned lit = 0, overStated = 0;
            for (unsigned i = 0; i < oRes * oRes; ++i) {
                double sum = 0.0, sum2 = 0.0, prev = 0.0, sumN = 0.0, sum2N = 0.0;
                for (unsigned k = 1; k <= kOracleN; ++k) {
                    const double m = lumOf(&oracle[size_t(p) * kOracleN + k - 1u][size_t(i) * oBpp]);
                    const double x = double(k) * m - double(k - 1) * prev;   // the k-th sample
                    prev = m;
                    sum += x; sum2 += x * x;
                    if (k <= n) { sumN += x; sum2N += x * x; }
                }
                const double mean = sum / kOracleN;
                if (mean <= 1e-6) continue;   // unlit: no relative error to bound
                const double sigma = std::sqrt(std::max(0.0, (sum2 - kOracleN * mean * mean) / (kOracleN - 1)));
                const double sd = sigma * std::sqrt(1.0 / n - 1.0 / kOracleN);
                const double diff = lumOf(&st[size_t(i) * oBpp]) - mean;
                const double z = sd > 0.0 ? std::fabs(diff) / sd : (std::fabs(diff) <= 1e-3 * mean ? 0.0 : 99.0);
                // THE STATED ERROR: what the stop itself computed from its n samples (2 SE of its mean).
                const double meanN = sumN / n;
                const double stated = 2.0 * std::sqrt(std::max(0.0, (sum2N - n * meanN * meanN) / (n - 1)) / n);
                if (std::fabs(diff) > stated) ++overStated;
                worstStated = std::max(worstStated, stated / mean);
                worstTrue = std::max(worstTrue, 2.0 * sigma / std::sqrt(double(n)) / mean);
                if (std::fabs(diff) / mean > worstRel) { worstRel = std::fabs(diff) / mean; worstStatedErr = stated / mean; }
                worstPrecision = std::max(worstPrecision, std::fabs(diff) / mean);
                if (std::fabs(diff) <= double(fs.stopPrecision) * mean) ++withinE;
                ++litTexels;
                ++lit;
                if (z <= 2.0) ++within2;
                sumZ += z;
            }
            const double meanZ = lit ? sumZ / lit : 0.0;
            worstMeanZ = std::max(worstMeanZ, meanZ);
            if (meanZ > 2.0) {
                ++badProbes;
                std::printf("   probe %u: n %u, mean |z| %.3f; 2 SE/mean as stated (its n samples) max %.4f, from the "
                            "oracle's 25 max %.4f; worst |stop - oracle|/oracle %.4f (stated there %.4f); %u of %u texels "
                            "outside their stated error\n", p, n, meanZ, worstStated, worstTrue, worstRel, worstStatedErr,
                            overStated, lit);
            }
        }
        std::printf("   the stop's counts:");
        for (unsigned n = 1; n <= kOracleN; ++n) if (byN[n]) std::printf(" n=%u: %u", n, byN[n]);
        std::printf("\n   %u probes stopped before the target; %u of %u replay the oracle's own mean at their n "
                    "(%u without a full oracle record)\n", stoppedEarly, replayed, oProbes, missing);
        std::printf("   STATISTICS over %llu lit texels of the stopped probes: %.2f %% within 2 sigma; the worst "
                    "probe's mean |z| %.3f; %u probes over 2\n", litTexels,
                    100.0 * double(within2) / double(litTexels ? litTexels : 1), worstMeanZ, badProbes);
        CHECK(missing == 0u, "the oracle recorded every probe at every count (not vacuous)");
        if (!sliverArm) CHECK(stoppedEarly > 0u, "probes stop before the target (not vacuous)");
        CHECK(replayed == oProbes, "S1: EVERY PROBE'S STOP ESTIMATE IS THE ORACLE'S OWN MEAN AT ITS COUNT, byte for byte");
        if (!sliverArm) CHECK(litTexels > 0 && 100u * within2 >= 93u * litTexels,
              "S2: at least 93 % of the stopped probes' lit texels within 2 sigma of the 25-sample oracle");
        if (!sliverArm) CHECK(20u * badProbes <= stoppedEarly,
              "S3: the stopped probes' stated errors hold as a 2-sigma statement (at most 5 % with a mean |z| over 2)");
        std::printf("   THE PRECISION: %.3f %% of the stopped probes' lit texels within e = %.2f of the oracle; the "
                    "worst %.4f\n", 100.0 * double(withinE) / double(litTexels ? litTexels : 1),
                    double(fs.stopPrecision), worstPrecision);
        CHECK(100u * withinE >= 95u * litTexels,
              "S3b: at least 95 % of the stopped probes' lit texels within the precision e of the 25-sample oracle");
        e->setFrameMonitor(MonitorLevel::Off);
        std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
        return failures ? 1 : 0;
    }
    if (varianceArm) {
        // Settled already (above). Re-integrate with nothing changed, reading the atlas after
        // every pass: a pass is done when the refinements owed move (or the field goes quiet).
        std::vector<GiFieldAtlas> passes;
        enginetest::frames(e, 1);   // the snapshot moves at a frame's close (U3 stage 2b)
        unsigned owedSeen = s->published().gi.ifdRefinesOwed;   // 0: converged
        s->refreshGiLighting(false);
        int quietRun = 0;
        bool started = false;
        GiFieldAtlas prev;
        for (int f = 0; f < 600 && quietRun < 20; ++f) {
            enginetest::frames(e, 1);
            std::vector<MonitorRecord> recs;
            e->popMonitorRecords(recs);
            const FieldWork w = fieldWork(recs);
            quietRun = (w.frames && !w.framesWithWork) ? quietRun + 1 : 0;
            const unsigned owed = s->published().gi.ifdRefinesOwed;
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
        const unsigned target = s->published().gi.ifdTargetSamples;
        if (passes.size() > target) passes.resize(target);
        const unsigned K = unsigned(passes.size());
        if (!K || !passes[0].available) { std::printf("FAIL: no atlas passes read\n"); return 1; }
        const GiFieldAtlas &a0 = passes[0];
        const unsigned np = a0.probes[0] * a0.probes[1] * a0.probes[2];
        const unsigned bordered = a0.irradBordered, res = bordered - 2u, width = a0.irradWidth;
        const unsigned bpp = a0.irradBytesPerTexel;
        std::printf("   %u passes read (the target %u samples), %u probes, %ux%u texels each, %u bytes a texel\n", K,
                    s->published().gi.ifdTargetSamples, np, res, res, bpp);
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
                enginetest::frames(e, 1);
                std::vector<MonitorRecord> recs;
                e->popMonitorRecords(recs);
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
        std::vector<MonitorRecord> all;
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
            enginetest::frames(e, 1);
            std::vector<MonitorRecord> recs;
            e->popMonitorRecords(recs);
            all.insert(all.end(), recs.begin(), recs.end());
        }
        for (int f = 0; f < 6; ++f) {
            enginetest::frames(e, 1);
            std::vector<MonitorRecord> recs;
            e->popMonitorRecords(recs);
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
    const GiStatus g0w = s->published().gi;
    const FieldWork walk = run("camera walks", 120, true, false);
    const GiStatus g1 = s->published().gi;
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
        enginetest::frames(e, 300);
        { std::vector<MonitorRecord> drop; e->popMonitorRecords(drop); }
        run("stills move", 120, false, false, true);
        // ONE STILL MOVED ONCE (ENGINE-UPGRADE-2B stage 4's instrument): the field's work from the
        // move until it settles — its frames, its probe integrations, its GPU ms.
        enginetest::frames(e, 300);
        { std::vector<MonitorRecord> drop; e->popMonitorRecords(drop); }
        const PhotonFieldStatus i0 = photonFieldStatus(*s);
        enginetest::setNodePosition(s, stills[0], Vec3(-2.0f, 0.5f, -3.0f));
        std::vector<MonitorRecord> one;
        for (int f = 0; f < 240; ++f) {
            enginetest::frames(e, 1);
            std::vector<MonitorRecord> recs;
            e->popMonitorRecords(recs);
            one.insert(one.end(), recs.begin(), recs.end());
        }
        const GiStatus gs = s->published().gi;
        print("one still moved once", fieldWork(one));
        const PhotonFieldStatus i1 = photonFieldStatus(*s);
        std::printf("   the generation job's own count: %llu probe integrations (the stop %s, %u of %u probes stopped)\n",
                    i1.integrations - i0.integrations, i1.stopMinSamples ? "on" : "off", i1.probesStopped, i1.probes);
        std::printf("   the field: %d probes (a whole re-integration is %d x the target %u samples)\n", gs.ifdProbes,
                    gs.ifdProbes, gs.ifdTargetSamples);
    }
    e->setFrameMonitor(MonitorLevel::Off);
    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
