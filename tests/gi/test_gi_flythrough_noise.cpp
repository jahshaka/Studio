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
// THE SUITE flies the rig's path (the gather-splotch drive: five segments of 90
// frames through a Showroom-2-shaped room — the sample's own layout, built here
// so the suite needs no document) and reads THE GATHER'S OWN ANSWER at ten
// poses through GatherTuning::readback: E/pi per pixel, before any material,
// light or tonemap. At each pose a CONVERGED REFERENCE is the mean of four rest
// means of 64 frames each at four independent sample sequences (the arms
// "gather.restFrames" / "gather.restSeed" as tuning fields), built with the
// PRE-LANE estimator (the 3 x 3 filter, no young reach).
//
// TWO ARMS IN ONE PROCESS, each flown twice from the same start:
//   * PRE-LANE: GatherTuning{filterRadius 1, youngFrames 1, youngReach 1} — the
//     estimator before this lane, exactly;
//   * SHIPPED: every tuning field 0.
// THE METRIC (the rig's): 16-px block means of the luminance of E/pi, over the
// floor (the lower 40 % of the frame), as a fraction of the reference's mean
// there. NOISE = mean |run 0 - run 1| over the blocks (two independent draws
// of the same path: sqrt 2 x the noise); BIAS = the mean signed (run - reference).
// THE BARS: the shipped noise is at most 0.60 of the pre-lane noise (the brief's
// -40 %), and the shipped bias moves by at most a measured tolerance from the
// pre-lane bias (no light added or taken: an estimator stays unbiased).
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
/// THE OPEN CONTROL'S NOISE BAR: no noisier than the pre-lane estimator, within
/// the measurement's own run-to-run spread.
static const double kOpenNoNoisier = 1.05;

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
/// The floor's blocks: the lower 40 % of the frame (the rig's region).
static bool floorBlock(size_t i)
{
    const unsigned bw = kW / kBlock, bh = kH / kBlock;
    return unsigned(i / bw) >= unsigned(std::floor(bh * 0.6));
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

struct Reading { double noise = 0.0, bias = 0.0; };
static Reading measure(const std::vector<Blocks> &r0, const std::vector<Blocks> &r1,
                       const std::vector<Blocks> &ref)
{
    double noise = 0.0, bias = 0.0, refSum = 0.0;
    size_t n = 0;
    for (int c = 0; c < kCaptures; ++c)
        for (size_t i = 0; i < ref[size_t(c)].size(); ++i) {
            if (!floorBlock(i)) continue;
            noise += std::fabs(r0[size_t(c)][i] - r1[size_t(c)][i]);
            bias += 0.5 * (r0[size_t(c)][i] + r1[size_t(c)][i]) - ref[size_t(c)][i];
            refSum += ref[size_t(c)][i];
            ++n;
        }
    Reading r;
    if (n && refSum > 0.0) {
        r.noise = noise / refSum;   // mean |d| / mean reference
        r.bias = bias / refSum;
    }
    return r;
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
    const bool open = arm == "open";
    if (!open && arm != "showroom2") { std::printf("FAIL: unknown arm %s\n", arm.c_str()); return 1; }

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
    const Reading pre = measure(a0, a1, ref), now = measure(b0, b1, ref);
    std::printf("   floor noise (mean |run0 - run1| / mean E): pre-lane %.4f, shipped %.4f (ratio %.3f)\n",
                pre.noise, now.noise, pre.noise > 0.0 ? now.noise / pre.noise : 0.0);
    std::printf("   floor bias  (mean run - reference / mean E): pre-lane %+.4f, shipped %+.4f\n", pre.bias,
                now.bias);
    if (open)
        CHECK_MSG(pre.noise > 0.0 && now.noise <= kOpenNoNoisier * pre.noise,
                  "THE OPEN CONTROL: under the sky the moving floor is no noisier than the pre-lane "
                  "estimator's (%.4f against %.4f, ratio %.3f, bar %.2f)", now.noise, pre.noise,
                  pre.noise > 0.0 ? now.noise / pre.noise : 0.0, kOpenNoNoisier);
    else
        CHECK_MSG(pre.noise > 0.0 && now.noise <= 0.60 * pre.noise,
                  "THE SPLOTCH: the moving floor's noise is at most 0.60 of the pre-lane estimator's "
                  "(%.4f against %.4f, ratio %.3f)", now.noise, pre.noise,
                  pre.noise > 0.0 ? now.noise / pre.noise : 0.0);
    CHECK_MSG(std::fabs(now.bias - pre.bias) <= kBiasTolerance,
              "NO LIGHT ADDED OR TAKEN: the floor's mean against the converged reference moves by %.4f "
              "(pre-lane %+.4f, shipped %+.4f; bar %.4f)", std::fabs(now.bias - pre.bias), pre.bias, now.bias,
              kBiasTolerance);
    e->destroyView(view);
    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
