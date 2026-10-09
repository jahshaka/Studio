// THE BLOOM SPREADS THE SAME WAY ACROSS AS DOWN (lane BLOOM-1).
//
// THE DEFECT (spikes/light-band-1): the bloom was Ogre's HDR sample reproduced
// — a bright pass into a FIXED 256x256 target, then six horizontal box blurs
// 65 taps wide and two vertical ones 11 taps tall. Its horizontal reach (6 x
// 64 = 384 texels) was more than the whole target at any aspect and its
// vertical reach 20 texels, 8 % of the height, so a point light's hot spot
// became a FULL-WIDTH horizontal band at its screen height: +34 display codes
// at both edges of the frame, on every tier.
//
// THE FIX is a progressive mip ladder sized from the VIEW (OgreChain.cpp,
// kBloomLevel; media/Hlms/Jahshaka/JahBloom.material): six half-size levels,
// a 13-tap downsample at every step and a tent upsample back. Every step
// halves both axes, so the filter is isotropic in pixels by construction.
//
// THE FIXTURE: one point light 0.3 m in front of a matte wall, the camera 8 m
// back on the light's axis at 960 x 540, so the hot spot is ROUND on screen —
// any difference between the halo's horizontal and vertical extent is the
// filter's, not the scene's. Graded at a fixed exposure with the SHIPPED bloom
// numbers (threshold 5, knee 2), undithered (the A/B is per pixel). Two
// pictures in ONE process, one chain: bloom amount 1 and amount 0 (which is
// the bloom-off picture, byte for byte — tests/hdr bloom_amount arm B).
//
// THE ARMS
//   A  CONTROL: the hot spot is there and it blooms (a halo of real size).
//   B  ISOTROPIC: the halo's half-width at half its peak excess, along the row
//      through the hot spot and along the column, agree within 10 %. The base
//      measured the row's at the frame's edge and the column's at ~12 px.
//   C  NO BAND: at both ends of the hot spot's row the bloomed picture equals
//      the bloom-off picture within one code (the base: +34).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                           std::printf("\n"); ++failures; } \
                           else { std::printf("  ok: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {

constexpr unsigned kW = 960, kH = 540;

Engine *gEngine = nullptr;
View   *gView = nullptr;

MeshData planeMesh(float half)
{
    MeshData d;
    const float p[4][3] = { {-half, -half, 0.0f}, { half, -half, 0.0f},
                            { half,  half, 0.0f}, {-half,  half, 0.0f} };
    for (int i = 0; i < 4; ++i) {
        d.positions.insert(d.positions.end(), { p[i][0], p[i][1], p[i][2] });
        d.normals.insert(d.normals.end(), { 0.0f, 0.0f, 1.0f });
    }
    d.indices = { 0, 1, 2, 0, 2, 3 };
    return d;
}

bool shoot(Image &out, int frames)
{
    for (int i = 0; i < frames; ++i) gEngine->renderOneFrame();
    return gView->readPixels(out);
}

/// The brightest channel of a pixel: the fixture is white light on grey, and
/// the max is what saturates first.
int code(const Image &img, unsigned x, unsigned y)
{
    const size_t i = (size_t(y) * img.width + x) * 4;
    return std::max({ int(img.rgba[i]), int(img.rgba[i + 1]), int(img.rgba[i + 2]) });
}

/// The bloom's excess along a line from the hot spot's centre outwards, in
/// display codes, averaged over the two pixels either side of the centre line
/// (the frame's centre falls between pixels at an even size).
std::vector<double> profile(const Image &on, const Image &off, bool horizontal, int dir)
{
    std::vector<double> v;
    const int cx = int(kW / 2), cy = int(kH / 2);
    const int n = horizontal ? cx : cy;
    for (int r = 0; r < n; ++r) {
        double sum = 0.0;
        for (int side = 0; side < 2; ++side) {
            int x, y;
            if (horizontal) { x = dir > 0 ? cx + r : cx - 1 - r; y = cy - side; }
            else            { y = dir > 0 ? cy + r : cy - 1 - r; x = cx - side; }
            sum += double(code(on, unsigned(x), unsigned(y)) - code(off, unsigned(x), unsigned(y)));
        }
        v.push_back(0.5 * sum);
    }
    return v;
}

/// The outer radius at which the excess falls to half its peak along the
/// line (linear between the two straddling pixels). The core of the hot spot
/// is saturated in both pictures, so the excess there is zero and the peak is
/// a ring; "half-width" is where the halo OUTSIDE it halves, which is the
/// filter's width.
double halfWidth(const std::vector<double> &v, double &peakOut)
{
    size_t ip = 0;
    for (size_t i = 0; i < v.size(); ++i) if (v[i] > v[ip]) ip = i;
    peakOut = v[ip];
    const double half = 0.5 * v[ip];
    for (size_t i = ip; i + 1 < v.size(); ++i)
        if (v[i + 1] <= half) {
            const double t = (v[i] - half) / std::max(1e-9, v[i] - v[i + 1]);
            return double(i) + t;
        }
    return double(v.size());       // never halves inside the frame: a band
}

/// The first radius past which the excess stays under one code to the edge.
int reach(const std::vector<double> &v)
{
    int last = -1;
    for (size_t i = 0; i < v.size(); ++i) if (v[i] >= 1.0) last = int(i);
    return last + 1;
}

}   // namespace

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-hdr-bloom-isotropic-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    gEngine = engine.get();

    gView = engine->createOffscreenView("bloomiso", kW, kH, Colour(0, 0, 0));
    Scene *s = engine->createScene("bloomiso");
    if (!gView || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    gView->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    gView->setShadows(false);

    {
        const NodeId n = s->createNode();
        const MeshId m = s->createMesh(planeMesh(40.0f));
        PbrParams p;
        p.albedo = Colour(0.5f, 0.5f, 0.5f);
        p.metalness = 0.0f;
        p.roughness = 1.0f;
        const MaterialId mat = s->createPbrMaterial(p);
        if (!n || !m || !mat || !s->attachMesh(n, m, mat)) {
            std::printf("FAIL: fixture wall: %s\n", engine->lastError().c_str());
            return 1;
        }
    }
    {
        const NodeId n = s->createNode();
        s->setNodeTransform(n, Vec3{0.0f, 0.0f, 0.3f}, Quat{}, Vec3{1, 1, 1});
        LightDesc l;
        l.type = LightType::Point;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 8.0f;
        l.range = 200.0f;
        l.castShadows = false;
        if (!s->setLight(n, l)) { std::printf("FAIL: fixture light\n"); return 1; }
    }
    enginetest::testCameraAt(gView, Vec3(0.0f, 0.0f, 8.0f));

    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdr = true;
        fx.tonemapFixed = true;
        fx.exposure = 0.0f;
        fx.ditherOff = true;
        fx.bloom = true;           // the shipped threshold, knee and amount
        gView->setPostFx(fx);
    }

    Image on, off;
    if (!shoot(on, 8)) { std::printf("FAIL: readPixels (bloom)\n"); return 1; }
    {
        PostFxDesc fx = gView->postFx();
        fx.bloomAmount = 0.0f;
        gView->setPostFx(fx);
    }
    if (!shoot(off, 4)) { std::printf("FAIL: readPixels (amount 0)\n"); return 1; }

    const std::vector<double> right = profile(on, off, true, +1), left = profile(on, off, true, -1);
    const std::vector<double> down = profile(on, off, false, +1), up = profile(on, off, false, -1);
    double pr, pl, pd, pu;
    const double hr = halfWidth(right, pr), hl = halfWidth(left, pl);
    const double hd = halfWidth(down, pd), hu = halfWidth(up, pu);
    std::printf("   excess peak (codes): right %.1f left %.1f down %.1f up %.1f\n", pr, pl, pd, pu);
    std::printf("   half-width at half peak (px): right %.1f left %.1f down %.1f up %.1f\n", hr, hl, hd, hu);
    std::printf("   reach (last px >= 1 code): right %d left %d down %d up %d\n",
                reach(right), reach(left), reach(down), reach(up));
    std::printf("   row profile (every 24 px, right):");
    for (size_t i = 0; i < right.size(); i += 24) std::printf(" %.0f", right[i]);
    std::printf("\n   column profile (every 24 px, down):");
    for (size_t i = 0; i < down.size(); i += 24) std::printf(" %.0f", down[i]);
    std::printf("\n");

    // ---- A  CONTROL ---------------------------------------------------------
    CHECK(code(off, kW / 2, kH / 2) >= 250,
          "A control: the hot spot is saturated (centre code %d)", code(off, kW / 2, kH / 2));
    CHECK(std::min({ pr, pl, pd, pu }) >= 8.0,
          "A control: it blooms — the halo's peak excess is >= 8 codes on every side "
          "(min %.1f)", std::min({ pr, pl, pd, pu }));

    // ---- B  ISOTROPIC -------------------------------------------------------
    {
        const double h = 0.5 * (hr + hl), v = 0.5 * (hd + hu);
        const double rel = std::fabs(h - v) / std::max(h, v);
        CHECK(rel <= 0.10,
              "B: the halo is as wide as it is tall — half-width at half peak %.1f px across, "
              "%.1f px down (%.1f %% apart, bar 10 %%)", h, v, 100.0 * rel);
    }

    // ---- C  NO BAND ---------------------------------------------------------
    {
        int worst = 0;
        for (unsigned y = kH / 2 - 1; y <= kH / 2; ++y)
            for (unsigned x : { 0u, 1u, 2u, kW - 3, kW - 2, kW - 1 })
                worst = std::max(worst, std::abs(code(on, x, y) - code(off, x, y)));
        CHECK(worst <= 1,
              "C: no band — at both ends of the hot spot's row the bloomed picture is the "
              "bloom-off picture within one code (worst %d)", worst);
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
