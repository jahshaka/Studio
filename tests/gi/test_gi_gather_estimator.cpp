// gi.gather_estimator — THE GATHER'S ESTIMATOR AGAINST THE PRE-LANE ONE (GATHER-NOISE-1's guard,
// TESTING-SPEED-B1: the seconds-long successor of gi.flythrough_noise, whose 810 s flights re-flew the
// pre-lane estimator every run; its source and the script that re-runs it by hand are kept under
// spikes/testing-speed-b1/flythrough/, never a row).
//
// THE BEHAVIOUR GUARDED (the owner's report: blurry splotches over the floor while moving). The
// splotch is the screen-probe gather's Monte-Carlo noise where the pixel history is SHORT — the floor
// entering the view as the camera turns. GATHER-NOISE-1 cut it with three levers, all shipped:
// the probe filter's reach (filterRadius), the young history's wide read (youngFrames/youngReach)
// and the rays stratified across neighbouring probes (crossStrata). The pre-lane estimator is
// GatherTuning{filterRadius 1, youngFrames 1, youngReach 1, crossStrata 1}; the shipped one is
// every field 0.
//
// TWO ARMS IN ONE PROCESS (trap 12: paired, the same chain, the same frames):
//   THE SHOWROOM (the Showroom-2-shaped room of the flythrough, its camera at the flythrough's
//   fourth capture — the floor and the wall band in view), 256 x 256 at Epic, restOff (a still
//   view would otherwise hold its rest mean and every estimator's answer be one draw): the camera
//   settles looking AWAY, TURNS onto the pose in 4 frames (young pixels: the wide read acts on them
//   only), then a 32-frame window reads the gather's own answer back every frame (GatherTuning::
//   readback: E/pi before any material, light or tonemap) — and the SAME TURN IS FLOWN TWICE (a twin
//   pair). Two twin pairs per arm in ABBA order (pre, shipped, shipped, pre), pooled per arm.
//   NOISE IS THE FLYTHROUGH'S METRIC: the floor region's (the lower 40 %) 16-px block means of the
//   luminance, the mean |run 0 - run 1| (two independent draws of one path: the sequence advances with
//   the frame index) over the window, as a fraction of the region's mean. (A frame-to-frame delta of
//   ONE run is NOT this quantity: the history's EMA correlates consecutive frames.)
//   ERROR IS THE FLYTHROUGH'S BLUR BAR: the converged reference at the pose (two rest means of 64 frames
//   at two sample sequences, GatherTuning restFrames/restSeed, under the pre-lane estimator), and the
//   RMS over blocks and frames of (run - reference) as a fraction of its mean — a wider read lowers the
//   noise and RAISES this where the irradiance has a gradient.
//   THE LEAK (the flythrough's leak arm, one pose): a thin partition, a lamp on one side, the camera
//   turning in 4 frames onto the two floors seen from above — the young dark floor beside the
//   partition must carry no more of the lit side's light than the pre-lane estimator's.
// THE BARS. The floor noise: shipped <= kFloorNoiseBar (0.80) x the pre-lane's — THIS ROW'S OWN CALIBRATION (the
// lead, TESTING-SPEED-B1 2026-10-12, the house rule: measured, with headroom either way). The flythrough's 0.60
// (GATHER-NOISE-1's -40 %) was the bar of ITS measurement — two 360-frame flights at 768x432, motion-averaged — and
// stays with that measurement, re-run by hand (spikes/testing-speed-b1/flythrough/flythrough.sh). This row measures
// one pose, two twin pairs per arm at 256 x 256, whose ratio is the draw's: five arrangements of the same frames read
// 0.535 / 0.560 / 0.588 / 0.599 / 0.647 (the ABBA order 0.647), the arms SWAPPED 1.675 — 0.80 sits above every
// measured draw and far below the swap (1.700 in the ABBA order). The error: shipped <= 1.05 x the pre-lane's (the flythrough's kErrorTolerance,
// unchanged). The dark side <= the pre-lane's x 1.10 + 0.002 x the lit floor's (unchanged). No stored constants:
// both arms are measured every run.
// THE BAR BITES: `test_gi_gather_estimator --swap-arms` runs the same process with the arms' roles
// exchanged and must FAIL (spikes/testing-speed-b1/estimator-red.txt) — a proof, never a row.
//
// FRAMES (declared on the row): kChainFrames + 2 x (2 + 64 + 4 + the readback's tail) for the reference
// + 8 x (kAwayFrames + kTurnFrames + kWindowFrames + the tail <= kTailFrames) + 2 x (kLeakAwayFrames +
// kTurnFrames + kLeakReadFrames). The readback is on for the read frames only (its copy and decode cost a
// Debug frame ~30 ms). Frames, never time.
// BUDGET 15 s: THE MEASUREMENT (TESTING-SPEED-B1, the lead 2026-10-12): 640 + ~300 frames and two references;
// <= 10 s warm (a cold process pays its shader compiles in its first frames: spikes/testing-speed-b1/profile.txt).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[768];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kSize = 256u, kBlock = 16u;
static const double kFloorNoiseBar = 0.80;   // this row's calibration (the header: five draws 0.535..0.647, the swap 1.675)
// THE FRAMES (TESTING-SPEED-B1). kChainFrames: the flythrough's warm-up (its 60-frame chain build + the ~170 frames
// its references rendered before the first flight); kLeakAwayFrames: its leak arm's 120-frame settle away;
// kAwayFrames fills an arm's history at the away pose (a 10-frame EMA, GatherTuning::historyFrames: four of them).
static const int kChainFrames = 230;     // the chain built over the room before the reference and the arms
static const int kAwayFrames = 40;       // an arm's history filled at the away pose
static const int kTurnFrames = 4;        // the turn (the flythrough's leak arm's): young pixels
static const int kWindowFrames = 32;     // the measured window
static const int kTailFrames = 8;        // the readback retires a few frames late
static const int kLeakAwayFrames = 120;  // the leak arm's settle away (the flythrough's)
static const int kLeakReadFrames = 10;   // the leak arm's frames after the turn (read from the 6th)

/// THE SHOWROOM'S POSE: the flythrough's fourth capture (its kKeys[4]); away = the same eye turned 40 degrees left.
static const Vec3 kPos(10.5f, 1.7f, 4.0f), kTarget(-5.0f, 0.5f, -4.0f), kAway(-1.0f, 0.5f, 12.0f);

static void render(Engine *e, int frames) { enginetest::frames(e, frames);}

static PbrParams matte(const Colour &albedo)
{
    PbrParams p;
    p.albedo = albedo;
    p.roughness = 1.0f;
    p.workflow = PbrParams::Workflow::Specular;
    p.ior = 1.0f;
    p.specularColour = Colour(0.0f, 0.0f, 0.0f);
    return p;
}

static void addBox(Scene *s, MeshId mesh, const PbrParams &p, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = s->createNode();
    const MaterialId m = s->createPbrMaterial(p);
    if (!n || !m || !s->attachMesh(n, mesh, m)) return;
    s->setNodeTransform(n, pos, Quat(), scale);
}

static void addPointLamp(Scene *s, const Vec3 &pos, float intensity, float range)
{
    const NodeId n = s->createNode();
    if (!n) return;
    s->setNodeTransform(n, pos, Quat(), Vec3(1, 1, 1));
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1.0f, 0.97f, 0.92f);
    l.intensity = intensity;
    l.range = range;
    l.castShadows = true;
    s->setLight(n, l);
}

/// THE SHOWROOM-2-SHAPED ROOM (gi.gather_stable's: a closed 24 x 24 m room, 7.25 m to the ceiling,
/// four columns, three point lamps, the ambient black so every indirect photon is the gather's), plus
/// the sample's four spheres' footprint as blocks.
static void buildShowroom(Scene *s)
{
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    addBox(s, cube, matte(Colour(0.62f, 0.60f, 0.58f)), Vec3(0.0f, -0.25f, 0.0f), Vec3(25.0f, 0.5f, 25.0f));
    addBox(s, cube, matte(Colour(0.80f, 0.80f, 0.80f)), Vec3(0.0f, 7.5f, 0.0f), Vec3(25.0f, 0.5f, 25.0f));
    addBox(s, cube, matte(Colour(0.75f, 0.74f, 0.72f)), Vec3(0.0f, 3.5f, 12.25f), Vec3(25.0f, 7.5f, 0.5f));
    addBox(s, cube, matte(Colour(0.78f, 0.45f, 0.30f)), Vec3(0.0f, 3.5f, -12.25f), Vec3(25.0f, 7.5f, 0.5f));
    addBox(s, cube, matte(Colour(0.75f, 0.74f, 0.72f)), Vec3(-12.25f, 3.5f, 0.0f), Vec3(0.5f, 7.5f, 25.0f));
    addBox(s, cube, matte(Colour(0.75f, 0.74f, 0.72f)), Vec3(12.25f, 3.5f, 0.0f), Vec3(0.5f, 7.5f, 25.0f));
    for (int i = 0; i < 4; ++i)
        addBox(s, cube, matte(Colour(0.70f, 0.70f, 0.70f)),
               Vec3((i & 1) ? 8.0f : -8.0f, 3.5f, (i & 2) ? 8.0f : -8.0f), Vec3(2.4f, 7.0f, 2.4f));
    addBox(s, cube, matte(Colour(0.25f, 0.45f, 0.80f)), Vec3(-4.0f, 1.0f, 0.0f), Vec3(2.0f, 2.0f, 2.0f));
    addBox(s, cube, matte(Colour(0.85f, 0.85f, 0.80f)), Vec3(3.5f, 0.75f, -2.0f), Vec3(1.5f, 1.5f, 3.0f));
    addBox(s, cube, matte(Colour(0.30f, 0.65f, 0.30f)), Vec3(1.0f, 0.5f, 7.0f), Vec3(1.0f, 1.0f, 1.0f));
    addPointLamp(s, Vec3(0.0f, 6.2f, 0.0f), 38.2f, 30.0f);
    addPointLamp(s, Vec3(-8.0f, 5.0f, -5.0f), 20.3f, 30.0f);
    addPointLamp(s, Vec3(8.0f, 5.0f, 5.0f), 20.3f, 30.0f);
}

/// THE LEAK ROOM (the flythrough's leak arm): a 30 m floor, a partition 0.1 m thick and 2 m tall
/// along z at x = 0, a lamp on its -x side; no sky, no ambient.
static void buildLeak(Scene *s)
{
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    addBox(s, cube, matte(Colour(0.7f, 0.7f, 0.7f)), Vec3(0.0f, -0.25f, 0.0f), Vec3(30.0f, 0.5f, 30.0f));
    addBox(s, cube, matte(Colour(0.7f, 0.7f, 0.7f)), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.1f, 2.0f, 16.0f));
    addPointLamp(s, Vec3(-1.2f, 1.5f, 0.0f), 30.0f, 15.0f);
}

static Vec3 lerp3(const Vec3 &a, const Vec3 &b, float t)
{
    return Vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

static bool epicGather(Scene *s)
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Epic;   // the owner's tier: an 8-px stride, 64 rays
    gi.ddgi = GiToggle::Off;        // the gather is the diffuse, alone
    gi.gather = GiToggle::On;
    return s->setGlobalIllumination(gi);
}

/// The luminance of E/pi as 16-px block means; the FLOOR is the lower 40 % of the frame.
static std::vector<double> floorBlocks(const std::vector<float> &irr)
{
    const unsigned bw = kSize / kBlock, bh = kSize / kBlock, row0 = unsigned(std::floor(bh * 0.6));
    std::vector<double> b(size_t(bw) * (bh - row0), 0.0);
    for (unsigned y = row0 * kBlock; y < bh * kBlock; ++y)
        for (unsigned x = 0; x < bw * kBlock; ++x) {
            const float *p = &irr[(size_t(y) * kSize + x) * 4u];
            b[size_t(y / kBlock - row0) * bw + x / kBlock] += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
        }
    for (double &v : b) v /= double(kBlock * kBlock);
    return b;
}

/// ONE TURN under `t`: the away pose settled (no readback: its copy costs a Debug frame ~30 ms), the 4-frame
/// turn, then the window's floor blocks by their frame offset after the turn's last frame.
static std::vector<std::vector<double>> turnWindow(Engine *e, View *view, Scene *s, GatherTuning t)
{
    const Vec3 &pos = kPos, &target = kTarget, &away = kAway;
    t.readback = false;
    s->setGatherTuning(t);
    enginetest::testCameraLookAt(view, pos, away);
    render(e, kAwayFrames);
    t.readback = true;
    s->setGatherTuning(t);
    for (int i = 1; i <= kTurnFrames; ++i) {
        enginetest::testCameraLookAt(view, pos, lerp3(away, target, float(i) / float(kTurnFrames)));
        render(e, 1);
    }
    const unsigned first = s->published().gi.gather.frame - 1u;   // the turn's last frame
    std::vector<std::vector<double>> got(static_cast<size_t>(kWindowFrames));
    size_t n = 0;
    const auto harvest = [&]() {
        const GatherStatus g = s->published().gi.gather;
        if (g.irradiance.empty() || g.irradianceW != kSize || g.irradianceH != kSize) return;
        if (g.irradianceFrame >= first && g.irradianceFrame < first + unsigned(kWindowFrames) &&
            got[g.irradianceFrame - first].empty()) {
            got[g.irradianceFrame - first] = floorBlocks(g.irradiance);
            ++n;
        }
    };
    for (int i = 0; i < kWindowFrames; ++i) { render(e, 1); harvest(); }
    for (int i = 0; i < kTailFrames && n < size_t(kWindowFrames); ++i) { render(e, 1); harvest(); }
    return got;
}

/// THE CONVERGED REFERENCE at the pose (the flythrough's, at one pose): two rest means of 64 frames at two
/// independent sample sequences (GatherTuning restFrames / restSeed) under the PRE-LANE estimator, averaged — its
/// own noise a fraction of a young frame's, and it enters only the ERROR. The camera is nudged first so the rest
/// restarts at the pose; the readback is on for the held frames only.
static std::vector<double> reference(Engine *e, View *view, Scene *s, const GatherTuning &preLane)
{
    std::vector<double> sum;
    for (unsigned seed = 1; seed <= 2; ++seed) {
        GatherTuning t = preLane;
        t.restOff = false;
        t.restFrames = 64u;
        t.restSeed = seed;
        t.readback = false;
        s->setGatherTuning(t);
        enginetest::testCameraLookAt(view, Vec3(kPos.x + 0.01f, kPos.y, kPos.z), kTarget);
        render(e, 2);
        enginetest::testCameraLookAt(view, kPos, kTarget);
        render(e, 64 + 4);   // the rest mean completes at its 64th rest frame and HOLDS
        const unsigned since = s->published().gi.gather.frame;
        t.readback = true;
        s->setGatherTuning(t);
        std::vector<double> b;
        for (int k = 0; k < kTailFrames + 4 && b.empty(); ++k) {
            render(e, 1);
            const GatherStatus g = s->published().gi.gather;
            if (!g.irradiance.empty() && g.irradianceW == kSize && g.irradianceH == kSize && g.irradianceFrame >= since)
                b = floorBlocks(g.irradiance);
        }
        if (b.empty()) return {};
        if (sum.empty()) sum.assign(b.size(), 0.0);
        for (size_t i = 0; i < b.size(); ++i) sum[i] += 0.5 * b[i];
    }
    return sum;
}

/// ONE ARM'S READINGS, pooled over its twin pairs. NOISE (the flythrough's metric at one pose): the same turn flown
/// twice from the same start — two independent draws, the sequence advancing with the frame index — the mean
/// |run 0 - run 1| of the floor blocks over the window, as a fraction of their mean. ERROR (the flythrough's blur
/// bar): the RMS over blocks and frames of (run - reference), as a fraction of the reference's mean — a wider read
/// lowers the noise and RAISES this where the irradiance has a gradient.
struct ArmReading {
    double diff = 0.0, mean = 0.0, sq = 0.0;
    size_t nsq = 0;
    unsigned frames = 0u;
    double noise() const { return mean > 0.0 ? diff / mean : 0.0; }
    double error(double refMean) const { return nsq && refMean > 0.0 ? std::sqrt(sq / double(nsq)) / refMean : 0.0; }
};
static void twinPair(Engine *e, View *view, Scene *s, const GatherTuning &t, const std::vector<double> &ref,
                     ArmReading &a)
{
    const auto r0 = turnWindow(e, view, s, t);
    const auto r1 = turnWindow(e, view, s, t);
    for (size_t k = 0; k < r0.size(); ++k) {
        if (r0[k].empty() || r1[k].empty()) continue;
        for (size_t i = 0; i < r0[k].size(); ++i) {
            a.diff += std::fabs(r1[k][i] - r0[k][i]);
            a.mean += 0.5 * (r1[k][i] + r0[k][i]);
            const double d0 = r0[k][i] - ref[i], d1 = r1[k][i] - ref[i];
            a.sq += 0.5 * (d0 * d0 + d1 * d1);
            ++a.nsq;
        }
        ++a.frames;
    }
}

/// THE LEAK ARM: the young dark floor's mean E/pi just after the turn, and the lit floor's.
static unsigned leakRun(Engine *e, View *view, Scene *s, const GatherTuning &t,
                        const std::vector<unsigned char> &dark, const std::vector<unsigned char> &lit,
                        double &darkE, double &litE)
{
    const Vec3 pos(0.0f, 8.0f, -3.0f), target(0.0f, 0.0f, 1.0f), away(10.0f, 8.0f, -3.0f);
    GatherTuning quiet = t;
    quiet.readback = false;   // the copy costs a Debug frame ~30 ms: on for the read frames only
    s->setGatherTuning(quiet);
    enginetest::testCameraLookAt(view, pos, away);
    render(e, kLeakAwayFrames);
    s->setGatherTuning(t);
    darkE = litE = 0.0;
    unsigned frames = 0;
    for (int i = 1; i <= kTurnFrames + kLeakReadFrames; ++i) {
        enginetest::testCameraLookAt(view, pos, lerp3(away, target, std::min(1.0f, float(i) / float(kTurnFrames))));
        render(e, 1);
        const GatherStatus g = s->published().gi.gather;
        // the readback lags a few frames: from the turn's end on, the view's first young frames
        if (i < 6 || g.irradiance.empty() || g.irradianceW != kSize || g.irradianceH != kSize) continue;
        double d = 0.0, l = 0.0;
        size_t nd = 0, nl = 0;
        for (size_t px = 0; px < size_t(kSize) * kSize; ++px) {
            const float *v = &g.irradiance[px * 4u];
            const double lum = 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
            if (dark[px]) { d += lum; ++nd; }
            if (lit[px]) { l += lum; ++nl; }
        }
        if (nd && nl) { darkE += d / double(nd); litE += l / double(nl); ++frames; }
    }
    if (frames) { darkE /= frames; litE /= frames; }
    return frames;
}

int main(int argc, char **argv)
{
    const bool swapArms = argc > 1 && std::strcmp(argv[1], "--swap-arms") == 0;
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-gather-estimator-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();

    View *view = e->createOffscreenView("estimator", kSize, kSize, Colour(0, 0, 0));
    if (!view) { std::printf("FAIL: view\n"); return 1; }
    view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_estimator skips cleanly\n");
        e->destroyView(view);
        return 0;
    }
    view->setShadows(true);
    PostFxDesc fx;
    fx.allowOffscreen = true;   // the chain with its prepass: what the gather places probes from
    view->setPostFx(fx);

    GatherTuning preLane;
    preLane.readback = true;
    preLane.restOff = true;
    preLane.filterRadius = 1u;
    preLane.youngFrames = 1u;
    preLane.youngReach = 1.0f;
    preLane.crossStrata = 1u;
    GatherTuning shipped;
    shipped.readback = true;
    shipped.restOff = true;
    if (swapArms) {
        std::printf("   --swap-arms: the arms' roles exchanged (the bar's own proof: this run must FAIL)\n");
        std::swap(preLane, shipped);
    }

    // ---- THE SHOWROOM: the floor's noise --------------------------------------------------------
    Scene *s = e->createScene("estimator");
    view->setScene(s);
    buildShowroom(s);
    CHECK(epicGather(s), "the chain builds over the room");
    render(e, kChainFrames);
    const std::vector<double> ref = reference(e, view, s, preLane);
    CHECK(!ref.empty(), "the converged reference reads back at the pose");
    if (ref.empty()) return 1;
    double refMean = 0.0;
    for (double v : ref) refMean += v / double(ref.size());
    // ABBA (the read's finding: one order read 0.535, the other 0.599 — the chain still warming and the frame
    // index's draw both ride on the order): pre, shipped, shipped, pre, each a twin pair, pooled per arm.
    ArmReading pre, now;
    twinPair(e, view, s, preLane, ref, pre);
    twinPair(e, view, s, shipped, ref, now);
    twinPair(e, view, s, shipped, ref, now);
    twinPair(e, view, s, preLane, ref, pre);
    const double preNoise = pre.noise(), nowNoise = now.noise();
    const double ratio = preNoise > 0.0 ? nowNoise / preNoise : 0.0;
    const double preErr = pre.error(refMean), nowErr = now.error(refMean);
    std::printf("   FLOOR noise after the turn: pre-lane %.4f, shipped %.4f (ratio %.3f; %u / %u frames, two pairs "
                "each, ABBA)\n", preNoise, nowNoise, ratio, pre.frames, now.frames);
    std::printf("   FLOOR error against the converged reference: pre-lane %.4f, shipped %.4f (ratio %.3f)\n", preErr,
                nowErr, preErr > 0.0 ? nowErr / preErr : 0.0);
    CHECK(pre.frames >= unsigned(kWindowFrames) && now.frames >= unsigned(kWindowFrames),
          "both arms read the window back (the gather ran every frame)");
    CHECK_MSG(preNoise > 0.0 && nowNoise <= kFloorNoiseBar * preNoise,
              "THE FLOOR AFTER A TURN: the shipped estimator's noise is at most %.2f of the pre-lane's "
              "(%.4f against %.4f, ratio %.3f)", kFloorNoiseBar, nowNoise, preNoise, ratio);
    CHECK_MSG(preErr > 0.0 && nowErr <= 1.05 * preErr,
              "NO BLUR PAID FOR IT: the shipped estimator's error against the converged reference is at most "
              "1.05 x the pre-lane's (%.4f against %.4f)", nowErr, preErr);

    // ---- THE LEAK: no light through a wall ------------------------------------------------------
    // a view and a scene of its own (the flythrough's leak arm was its own process): the room's go first
    e->destroyView(view);
    e->destroyScene(s);
    view = e->createOffscreenView("estimator-leak", kSize, kSize, Colour(0, 0, 0));
    if (!view) { std::printf("FAIL: leak view\n"); return 1; }
    view->setOffscreenContract(OffscreenContract::StillPicture);
    Scene *leak = e->createScene("estimator-leak");
    view->setScene(leak);
    view->setShadows(true);
    view->setPostFx(fx);
    buildLeak(leak);
    CHECK(epicGather(leak), "the chain builds over the partition");
    const Vec3 pos(0.0f, 8.0f, -3.0f), target(0.0f, 0.0f, 1.0f);
    std::vector<unsigned char> dark(size_t(kSize) * kSize, 0), lit(size_t(kSize) * kSize, 0);
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Vec3 q = enginetest::groundPointForPixel(pos, target, x, y, kSize);
            if (std::fabs(q.z) > 6.0f) continue;
            // THE WHOLE DARK FLOOR within 1.5 m, from the partition's own face (0.05 m) on: only light
            // from ACROSS the partition raises this region's mean.
            if (q.x > 0.06f && q.x < 1.5f) dark[size_t(y) * kSize + x] = 1;
            if (q.x < -0.25f && q.x > -1.5f) lit[size_t(y) * kSize + x] = 1;   // the lamp's side
        }
    double preDark = 0.0, preLit = 0.0, nowDark = 0.0, nowLit = 0.0;
    const unsigned nPre = leakRun(e, view, leak, preLane, dark, lit, preDark, preLit);
    const unsigned nNow = leakRun(e, view, leak, shipped, dark, lit, nowDark, nowLit);
    std::printf("   the dark strip's E/pi just uncovered: pre-lane %.5f, shipped %.5f (the lit floor %.4f / %.4f; "
                "%u / %u frames)\n", preDark, nowDark, preLit, nowLit, nPre, nNow);
    CHECK(nPre >= 4u && nNow >= 4u && preLit > 0.0, "the turn's young frames were read back and the lit side is lit");
    CHECK_MSG(nowDark <= preDark * 1.10 + 0.002 * preLit,
              "NO LIGHT THROUGH A WALL: the young dark floor beside the partition carries %.5f against the "
              "pre-lane's %.5f (bar: x1.10 + 0.2 %% of the lit floor's %.4f)", nowDark, preDark, preLit);

    e->destroyView(view);
    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
