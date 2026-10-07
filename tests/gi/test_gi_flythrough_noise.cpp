// gi.flythrough_noise.Showroom2 — THE GATHER'S NOISE IN MOTION (GATHER-NOISE-1).
//
// The owner's report: blurry splotches over the floor and walls of Showroom 2
// while moving. Measured (spikes/gather-splotch, spikes/gather-noise-1): the
// splotch is the screen-probe gather's Monte-Carlo noise, spread by the probe
// filter and the bilinear into 16-40 px blobs, and on a moving camera MOST of
// it sits where the pixel history is SHORT — the floor and wall entering at an
// edge the camera turns toward — while a pixel whose history is full averages
// exactly the frames the EMA promises (no reprojection loss).
//
// THE SUITE flies the rig's path (the gather-splotch drive: five segments of 72
// frames through a Showroom-2-shaped room — the sample's own layout, built here
// so the suite needs no document) and reads THE GATHER'S OWN ANSWER at the end of
// each segment through GatherTuning::readback: E/pi per pixel, before any
// material, light or tonemap. At each pose a CONVERGED REFERENCE is the mean of
// two rest means of 64 frames at two independent sample sequences (the arms
// "gather.restFrames" / "gather.restSeed" as tuning fields), built with the
// PRE-LANE estimator (the 3 x 3 filter, no young reach, no cross-probe strata).
//
// TWO ARMS IN ONE PROCESS, each flown twice from the same start:
//   * PRE-LANE: GatherTuning{filterRadius 1, youngFrames 1, youngReach 1,
//     crossStrata 1} — the estimator before this lane, exactly;
//   * SHIPPED: every tuning field 0.
// THE METRIC (the rig's): 16-px block means of the luminance of E/pi, as a
// fraction of the reference's mean, in two regions — the FLOOR (the lower 40 %)
// and the WALLS (the band from 25 % to 60 % of the height). NOISE = mean
// |run 0 - run 1| (two independent draws of one path: sqrt 2 x the noise);
// BIAS = the mean signed (run - reference); ERROR = the RMS over blocks of
// (run - reference), which a BLUR raises where the irradiance has a gradient
// (a smear lowers the noise and moves no region mean, so noise and bias alone
// are blind to it).
// THE BARS: the floor's noise at most 0.60 of the pre-lane (the brief's -40 %),
// the walls' no worse, the error at most 5 % above the pre-lane's in both regions
// (kErrorTolerance says why), and each
// region's bias moved by at most kBiasTolerance.
//
// ARM "open": the same blocks under the open sky (the owner: an open scene shows
// no splotch) — no noisier, no larger error, the mean unmoved.
// ARM "leak": NO LIGHT THROUGH A WALL. A thin partition on one floor, a lamp on
// one side; the camera turns to uncover the dark side, whose pixels are YOUNG and
// read the wide neighbourhood — the dark floor beside the partition must carry no
// more of the lit side's light than the pre-lane estimator's bilinear does.
//
// Frames, never time.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

static void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

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

/// THE SHOWROOM-2-SHAPED ROOM (gi.gather_stable's: a closed 24 x 24 m room,
/// 7.25 m to the ceiling, four columns, three point lamps, the ambient black so
/// every indirect photon is the gather's), plus the sample's four spheres'
/// footprint as blocks — what the path's silhouettes uncover.
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

/// THE OPEN CONTROL (the lead's, from the owner: an open, sky-lit scene shows no
/// splotch): the same blocks and columns on a 60 m ground under the analytic
/// sky and a low sun, no walls and no roof — most gather rays escape to the
/// smooth sky. The lane must not make it noisier, blur it or move its mean.
static void buildOpen(Scene *s)
{
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    addBox(s, cube, matte(Colour(0.62f, 0.60f, 0.58f)), Vec3(0.0f, -0.25f, 0.0f), Vec3(60.0f, 0.5f, 60.0f));
    for (int i = 0; i < 4; ++i)
        addBox(s, cube, matte(Colour(0.70f, 0.70f, 0.70f)),
               Vec3((i & 1) ? 8.0f : -8.0f, 3.5f, (i & 2) ? 8.0f : -8.0f), Vec3(2.4f, 7.0f, 2.4f));
    addBox(s, cube, matte(Colour(0.25f, 0.45f, 0.80f)), Vec3(-4.0f, 1.0f, 0.0f), Vec3(2.0f, 2.0f, 2.0f));
    addBox(s, cube, matte(Colour(0.85f, 0.85f, 0.80f)), Vec3(3.5f, 0.75f, -2.0f), Vec3(1.5f, 1.5f, 3.0f));
    addBox(s, cube, matte(Colour(0.30f, 0.65f, 0.30f)), Vec3(1.0f, 0.5f, 7.0f), Vec3(1.0f, 1.0f, 1.0f));
    addBox(s, cube, matte(Colour(0.78f, 0.45f, 0.30f)), Vec3(0.0f, 3.5f, -12.25f), Vec3(25.0f, 7.5f, 0.5f));
    SkyDesc sky;
    sky.mode = SkyMode::Atmosphere;
    sky.atmosphere.hasSun = true;
    sky.atmosphere.sunDir[0] = 0.4f; sky.atmosphere.sunDir[1] = 0.6f; sky.atmosphere.sunDir[2] = 0.3f;
    s->setSky(sky);
    enginetest::addDirectionalLight(s, Vec3(-0.4f, -0.6f, -0.3f), 2.0f);
}

/// THE RIG'S PATH (spikes/gather-splotch/rig/drive2.py): six key poses, five
/// segments, a capture at the middle and the end of each.
struct Key { Vec3 pos, target; };
static const Key kKeys[6] = {
    { Vec3(0.0f, 1.7f, -10.0f), Vec3(0.0f, 1.0f, 0.0f) },
    { Vec3(-10.5f, 1.7f, -4.0f), Vec3(5.0f, 0.5f, 4.0f) },
    { Vec3(-10.5f, 1.7f, 4.0f), Vec3(5.0f, 0.5f, -4.0f) },
    { Vec3(0.0f, 1.7f, 5.5f), Vec3(0.0f, 1.0f, -10.0f) },
    { Vec3(10.5f, 1.7f, 4.0f), Vec3(-5.0f, 0.5f, -4.0f) },
    { Vec3(5.0f, 1.7f, -10.0f), Vec3(-6.0f, 1.0f, 4.0f) },
};
static Key poseAt(int seg, float t)
{
    const Key &a = kKeys[seg], &b = kKeys[seg + 1];
    const auto lerp = [t](const Vec3 &x, const Vec3 &y) {
        return Vec3(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t);
    };
    return { lerp(a.pos, b.pos), lerp(a.target, b.target) };
}
static const int kSteps = 72;         // frames a segment: ~10 m/s at 60 Hz (the rig's 8-16 m/s band)
static const int kCaptures = 5;       // the end of each segment
static const unsigned kW = 768u, kH = 432u, kBlock = 16u;
/// THE BIAS BAR: how far the shipped floor's mean against the converged
/// reference may move from the pre-lane estimator's, as a fraction of the mean.
static const double kBiasTolerance = 0.005;
/// THE "NO NOISIER" BAR (the open control, the walls): no noisier than the
/// pre-lane estimator, within the measurement's own run-to-run spread.
static const double kNoNoisier = 1.05;
/// THE BLUR BAR: the error against the converged reference at most 5 % above the
/// pre-lane's. Measured (the lane's sweep, the walls): the 17-tap filter +50 % (not
/// shipped), a young reach of 4 cells +12 % (not shipped), the shipped 3 cells
/// +0.8 % and +3.8 % in two runs (the young pixels' transient blur along a lamp's
/// gradient, diluted as their history fills), 2.5 cells under 8 frames -0.6 % at a
/// floor ratio of 0.59; at HIGH (16-px stride) 3 CELLS raised it +13 % — the reach
/// is 24 PIXELS since, whatever the stride. The floor's error FALLS (0.042 -> 0.034). A blur of the
/// 17-tap filter's size fails this bar by ten times its margin.
static const double kErrorTolerance = 1.05;

/// The luminance of E/pi as 16-px block means (whole blocks only).
using Blocks = std::vector<double>;
static Blocks blocksOf(const std::vector<float> &irr, unsigned w, unsigned h)
{
    const unsigned bw = w / kBlock, bh = h / kBlock;
    Blocks b(size_t(bw) * bh, 0.0);
    for (unsigned y = 0; y < bh * kBlock; ++y)
        for (unsigned x = 0; x < bw * kBlock; ++x) {
            const float *p = &irr[(size_t(y) * w + x) * 4u];
            b[size_t(y / kBlock) * bw + x / kBlock] += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
        }
    for (double &v : b) v /= double(kBlock * kBlock);
    return b;
}
/// The regions, by block row: the FLOOR is the lower 40 % of the frame (the rig's
/// region), the WALLS the band from 25 % to 60 % (the path's eye height looks
/// along the room: walls and columns there).
enum class Region { Floor, Walls };
static bool inRegion(size_t i, Region r)
{
    const unsigned bw = kW / kBlock, bh = kH / kBlock;
    const unsigned row = unsigned(i / bw);
    if (r == Region::Floor) return row >= unsigned(std::floor(bh * 0.6));
    return row >= unsigned(std::floor(bh * 0.25)) && row < unsigned(std::floor(bh * 0.6));
}

/// Renders until the readback holds the frame `want` (or a later one: never).
static bool readFrame(Engine *e, Scene *s, unsigned want, std::vector<float> &out)
{
    for (int k = 0; k < 16; ++k) {
        const GatherStatus g = s->giStatus().gather;
        if (g.irradianceFrame == want && !g.irradiance.empty()) {
            out = g.irradiance;
            return true;
        }
        render(e, 1);
    }
    return false;
}

/// THE CONVERGED REFERENCE at one pose: two rest means of 64 frames at two
/// sequences (128 frames: its own noise is a third of a moving frame's at the
/// floor's block scale, and it enters only the BIAS, which averages it over
/// every floor block), under the pre-lane estimator. The camera is nudged first
/// so the rest restarts at the pose.
static Blocks reference(Engine *e, View *view, Scene *s, const Key &k, const GatherTuning &base)
{
    Blocks sum;
    for (unsigned seed = 1; seed <= 2; ++seed) {
        GatherTuning t = base;
        t.restFrames = 64u;
        t.restSeed = seed;
        s->setGatherTuning(t);
        enginetest::testCameraLookAt(view, Vec3(k.pos.x + 0.01f, k.pos.y, k.pos.z), k.target);
        render(e, 2);
        enginetest::testCameraLookAt(view, k.pos, k.target);
        render(e, 64 + 12);   // the rest mean completes at its 64th rest frame and holds
        const GatherStatus g = s->giStatus().gather;
        if (g.irradiance.empty() || g.irradianceW != kW || g.irradianceH != kH) {
            std::printf("   reference: readback %zu floats, %u x %u (running %d, on %d, rest %u)\n",
                        g.irradiance.size(), g.irradianceW, g.irradianceH, int(g.running), int(g.on),
                        g.restFrames);
            return {};
        }
        const Blocks b = blocksOf(g.irradiance, kW, kH);
        if (sum.empty()) sum.assign(b.size(), 0.0);
        for (size_t i = 0; i < b.size(); ++i) sum[i] += b[i] * 0.5;
    }
    return sum;
}

/// ONE FLIGHT of the path under `t`: the ten captures' blocks.
static std::vector<Blocks> fly(Engine *e, View *view, Scene *s, const GatherTuning &t)
{
    s->setGatherTuning(t);
    enginetest::testCameraLookAt(view, kKeys[0].pos, kKeys[0].target);
    render(e, 40);
    std::map<unsigned, int> wanted;   // gather frame -> capture
    std::vector<Blocks> caps(kCaptures);
    std::vector<float> irr;
    int got = 0;
    const auto harvest = [&]() {
        const GatherStatus g = s->giStatus().gather;
        auto it = wanted.find(g.irradianceFrame);
        if (it != wanted.end() && !g.irradiance.empty() && caps[size_t(it->second)].empty()) {
            caps[size_t(it->second)] = blocksOf(g.irradiance, g.irradianceW, g.irradianceH);
            ++got;
        }
    };
    for (int seg = 0; seg < 5; ++seg)
        for (int i = 1; i <= kSteps; ++i) {
            const Key k = poseAt(seg, float(i) / float(kSteps));
            enginetest::testCameraLookAt(view, k.pos, k.target);
            render(e, 1);
            if (i == kSteps) wanted[s->giStatus().gather.frame - 1u] = seg;
            harvest();
        }
    for (int k = 0; k < 8 && got < kCaptures; ++k) {   // the last capture retires a few frames late
        render(e, 1);
        harvest();
    }
    for (const Blocks &b : caps)
        if (b.empty()) return {};
    return caps;
}

struct Reading { double noise = 0.0, bias = 0.0, error = 0.0; };
static Reading measure(const std::vector<Blocks> &r0, const std::vector<Blocks> &r1,
                       const std::vector<Blocks> &ref, Region region)
{
    double noise = 0.0, bias = 0.0, sq = 0.0, refSum = 0.0;
    size_t n = 0;
    for (int c = 0; c < kCaptures; ++c)
        for (size_t i = 0; i < ref[size_t(c)].size(); ++i) {
            if (!inRegion(i, region)) continue;
            const double d0 = r0[size_t(c)][i] - ref[size_t(c)][i], d1 = r1[size_t(c)][i] - ref[size_t(c)][i];
            noise += std::fabs(r0[size_t(c)][i] - r1[size_t(c)][i]);
            bias += 0.5 * (d0 + d1);
            sq += 0.5 * (d0 * d0 + d1 * d1);
            refSum += ref[size_t(c)][i];
            ++n;
        }
    Reading r;
    if (n && refSum > 0.0) {
        const double mean = refSum / double(n);
        r.noise = noise / refSum;   // mean |d| / mean reference
        r.bias = bias / refSum;
        r.error = std::sqrt(sq / double(n)) / mean;   // RMS (run - reference) / mean reference
    }
    return r;
}

// ===========================================================================
// THE LEAK ARM
// ===========================================================================
/// Where the pixel (px, py) of the kW x kH view from testCameraLookAt(pos, target)
/// (45 degrees vertical) meets the floor y = 0.
static Vec3 floorPointOf(const Vec3 &pos, const Vec3 &target, unsigned px, unsigned py)
{
    Vec3 f(target.x - pos.x, target.y - pos.y, target.z - pos.z);
    float l = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    f = Vec3(f.x / l, f.y / l, f.z / l);
    Vec3 r(-f.z, 0.0f, f.x);
    l = std::sqrt(r.x * r.x + r.z * r.z);
    r = Vec3(r.x / l, 0.0f, r.z / l);
    const Vec3 u(r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x);
    const float t = std::tan(22.5f * 3.14159265f / 180.0f), aspect = float(kW) / float(kH);
    const float nx = (2.0f * (float(px) + 0.5f) / float(kW) - 1.0f) * aspect;
    const float ny = 1.0f - 2.0f * (float(py) + 0.5f) / float(kH);
    const Vec3 d(f.x + (r.x * nx + u.x * ny) * t, f.y + (r.y * nx + u.y * ny) * t, f.z + (r.z * nx + u.z * ny) * t);
    if (d.y >= -1e-4f) return Vec3(1e9f, 0.0f, 1e9f);
    const float k = -pos.y / d.y;
    return Vec3(pos.x + d.x * k, 0.0f, pos.z + d.z * k);
}

/// A 30 m floor, a partition 0.1 m thick and 2 m tall along z at x = 0, a lamp on
/// its -x side; no sky, no ambient: the +x floor beside the partition sees
/// nothing lit (the partition hides the lamp's floor and its own lit face). The
/// camera looks DOWN from 8 m over the partition — its top a few pixels wide, the
/// two floors side by side in the picture, one plane — after TURNING onto that
/// view in 4 frames, so the dark floor (+x, to 1.5 m from the partition) is
/// young and reads the wide neighbourhood. Its mean E/pi over the frames after
/// the turn, shipped against pre-lane (whose bilinear reaches one cell).
static int leakMain(Engine *e, View *view, Scene *s)
{
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    addBox(s, cube, matte(Colour(0.7f, 0.7f, 0.7f)), Vec3(0.0f, -0.25f, 0.0f), Vec3(30.0f, 0.5f, 30.0f));
    addBox(s, cube, matte(Colour(0.7f, 0.7f, 0.7f)), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.1f, 2.0f, 16.0f));
    addPointLamp(s, Vec3(-1.2f, 1.5f, 0.0f), 30.0f, 15.0f);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Epic;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::On;
    CHECK(s->setGlobalIllumination(gi), "the chain builds over the partition");
    const Vec3 pos(0.0f, 8.0f, -3.0f), target(0.0f, 0.0f, 1.0f), away(10.0f, 8.0f, -3.0f);
    std::vector<unsigned char> dark(size_t(kW) * kH, 0), lit(size_t(kW) * kH, 0);
    for (unsigned y = 0; y < kH; ++y)
        for (unsigned x = 0; x < kW; ++x) {
            const Vec3 q = floorPointOf(pos, target, x, y);
            if (std::fabs(q.z) > 6.0f) continue;
            // THE WHOLE DARK FLOOR within 1.5 m, from the partition's own face (0.05 m) on:
            // the wide read moves light ALONG this floor (a transient blur of whatever lies
            // on it, the base's own light at the partition's foot included), which keeps
            // this region's mean; only light from ACROSS the partition raises it.
            if (q.x > 0.06f && q.x < 1.5f) dark[size_t(y) * kW + x] = q.x < 0.6f ? 2 : 1;
            if (q.x < -0.25f && q.x > -1.5f) lit[size_t(y) * kW + x] = 1;   // the lamp's side
        }
    double nearSum = 0.0;
    const auto run = [&](const GatherTuning &t, double &darkE, double &litE) {
        nearSum = 0.0;
        s->setGatherTuning(t);
        enginetest::testCameraLookAt(view, pos, away);
        render(e, 120);
        darkE = litE = 0.0;
        unsigned frames = 0;
        for (int i = 1; i <= 4 + 10; ++i) {
            const float k = std::min(1.0f, float(i) / 4.0f);
            const Vec3 tg(away.x + (target.x - away.x) * k, away.y + (target.y - away.y) * k,
                          away.z + (target.z - away.z) * k);
            enginetest::testCameraLookAt(view, pos, tg);
            render(e, 1);
            const GatherStatus g = s->giStatus().gather;
            // the readback lags a few frames: from the turn's end on, the view's first young frames
            if (i < 6 || g.irradiance.empty() || g.irradianceW != kW || g.irradianceH != kH) continue;
            double d = 0.0, l = 0.0, nr = 0.0;
            size_t nd = 0, nl = 0, nn = 0;
            for (size_t px = 0; px < size_t(kW) * kH; ++px) {
                const float *v = &g.irradiance[px * 4u];
                const double lum = 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
                if (dark[px]) { d += lum; ++nd; }
                if (dark[px] == 2) { nr += lum; ++nn; }
                if (lit[px]) { l += lum; ++nl; }
            }
            if (nd && nl && nn) { darkE += d / nd; litE += l / nl; nearSum += nr / nn; ++frames; }
        }
        if (frames) { darkE /= frames; litE /= frames; nearSum /= frames; }
        return frames;
    };
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
    double preDark = 0.0, preLit = 0.0, nowDark = 0.0, nowLit = 0.0;
    const unsigned nPre = run(preLane, preDark, preLit);
    const double preNear = nearSum;
    const unsigned nNow = run(shipped, nowDark, nowLit);
    const double nowNear = nearSum;
    // ...and THE CONVERGED dark strip: the pre-lane history settled (restOff, 200 frames still).
    double convDark = 0.0, convNear = 0.0;
    {
        GatherTuning t = preLane;
        s->setGatherTuning(t);
        enginetest::testCameraLookAt(view, pos, target);
        render(e, 200);
        const GatherStatus g = s->giStatus().gather;
        double d = 0.0, nr = 0.0;
        size_t nd = 0, nn = 0;
        for (size_t px = 0; !g.irradiance.empty() && px < size_t(kW) * kH; ++px) {
            const float *v = &g.irradiance[px * 4u];
            const double lum = 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
            if (dark[px]) { d += lum; ++nd; }
            if (dark[px] == 2) { nr += lum; ++nn; }
        }
        convDark = nd ? d / nd : 0.0;
        convNear = nn ? nr / nn : 0.0;
    }
    std::printf("   converged (settled) dark strip %.5f, its band within 0.6 m %.5f; young near band: pre %.5f, "
                "shipped %.5f\n", convDark, convNear, preNear, nowNear);
    std::printf("   the dark strip's E/pi just uncovered: pre-lane %.5f, shipped %.5f (the lit floor %.4f / %.4f; "
                "%u / %u frames)\n", preDark, nowDark, preLit, nowLit, nPre, nNow);
    // WHICH LEVER CARRIES WHAT (printed, not barred): each of the lane's three with
    // the other two at the shipped value.
    for (int k = 0; k < 3; ++k) {
        GatherTuning t = shipped;
        if (k == 0) t.filterRadius = 1u;
        if (k == 1) { t.youngFrames = 1u; t.youngReach = 1.0f; }
        if (k == 2) t.crossStrata = 1u;
        double d = 0.0, l = 0.0;
        run(t, d, l);
        std::printf("   ...shipped but %s: %.5f\n", k == 0 ? "the 3x3 filter" : k == 1 ? "no young reach" : "no strata", d);
    }
    CHECK(nPre >= 4u && nNow >= 4u && preLit > 0.0, "the turn's young frames were read back and the lit side is lit");
    CHECK_MSG(nowDark <= preDark * 1.10 + 0.002 * preLit,
              "NO LIGHT THROUGH A WALL: the young dark floor beside the partition carries %.5f against the "
              "pre-lane's %.5f (bar: x1.10 + 0.2 %% of the lit floor's %.4f)", nowDark, preDark, preLit);
    return 0;
}

int main(int argc, char **argv)
{
    const std::string arm = argc > 1 ? argv[1] : "showroom2";
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-flythrough-noise-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    const bool open = arm == "open", leak = arm == "leak";
    if (!open && !leak && arm != "showroom2") { std::printf("FAIL: unknown arm %s\n", arm.c_str()); return 1; }

    View *view = e->createOffscreenView("flythrough", kW, kH, Colour(0, 0, 0));
    if (!view) { std::printf("FAIL: view\n"); return 1; }
    view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.flythrough_noise skips cleanly\n");
        e->destroyView(view);
        return 0;
    }
    Scene *s = e->createScene("flythrough");
    view->setScene(s);
    view->setShadows(true);
    PostFxDesc fx;
    fx.allowOffscreen = true;   // the chain with its prepass: what the gather places probes from
    view->setPostFx(fx);
    if (leak) {
        const int rc = leakMain(e, view, s);
        e->destroyView(view);
        std::printf(rc || failures ? "FAILED (%d)\n" : "PASSED\n", failures);
        return rc || failures ? 1 : 0;
    }
    if (open) buildOpen(s);
    else buildShowroom(s);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Epic;   // the owner's tier: an 8-px stride, 64 rays
    gi.ddgi = GiToggle::Off;        // the gather is the diffuse, alone
    gi.gather = GiToggle::On;
    CHECK(s->setGlobalIllumination(gi), "the chain builds over the room");
    render(e, 60);

    GatherTuning preLane;
    preLane.readback = true;
    preLane.filterRadius = 1u;
    preLane.youngFrames = 1u;
    preLane.youngReach = 1.0f;
    preLane.crossStrata = 1u;
    GatherTuning shipped;
    shipped.readback = true;

    std::vector<Blocks> ref;
    for (int c = 0; c < kCaptures; ++c) {
        ref.push_back(reference(e, view, s, poseAt(c, 1.0f), preLane));
        if (ref.back().empty()) { std::printf("FAIL: no reference at capture %d\n", c); return 1; }
        std::printf("   reference %d built\n", c);
        std::fflush(stdout);
    }
    const auto a0 = fly(e, view, s, preLane), a1 = fly(e, view, s, preLane);
    const auto b0 = fly(e, view, s, shipped), b1 = fly(e, view, s, shipped);
    CHECK(!a0.empty() && !a1.empty() && !b0.empty() && !b1.empty(),
          "every flight read its five captures back (the gather ran every frame of the path)");
    if (a0.empty() || a1.empty() || b0.empty() || b1.empty()) return 1;
    for (Region region : { Region::Floor, Region::Walls }) {
        const char *name = region == Region::Floor ? "FLOOR" : "WALLS";
        const Reading pre = measure(a0, a1, ref, region), now = measure(b0, b1, ref, region);
        const double ratio = pre.noise > 0.0 ? now.noise / pre.noise : 0.0;
        std::printf("   %s noise %.4f -> %.4f (ratio %.3f), error %.4f -> %.4f, bias %+.4f -> %+.4f\n", name,
                    pre.noise, now.noise, ratio, pre.error, now.error, pre.bias, now.bias);
        const double bar = (open || region == Region::Walls) ? kNoNoisier : 0.60;
        CHECK_MSG(pre.noise > 0.0 && now.noise <= bar * pre.noise,
                  "%s, MOVING: the noise is at most %.2f of the pre-lane estimator's (%.4f against %.4f, ratio %.3f)",
                  name, bar, now.noise, pre.noise, ratio);
        CHECK_MSG(now.error <= kErrorTolerance * pre.error,
                  "%s, NO BLUR PAID FOR IT: the error against the converged reference is no larger than the "
                  "pre-lane's (%.4f against %.4f, bar x%.2f)", name, now.error, pre.error, kErrorTolerance);
        CHECK_MSG(std::fabs(now.bias - pre.bias) <= kBiasTolerance,
                  "%s, NO LIGHT ADDED OR TAKEN: the mean against the converged reference moves by %.4f "
                  "(pre-lane %+.4f, shipped %+.4f; bar %.4f)", name, std::fabs(now.bias - pre.bias), pre.bias,
                  now.bias, kBiasTolerance);
    }
    e->destroyView(view);
    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
