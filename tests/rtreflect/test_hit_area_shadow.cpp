// rtreflect.hit_area_shadow (PHOTON-7, 7-H at the photon-7r merge; L9: every light blocked by geometry on every path) -
// AN AREA LIGHT SEEN IN A REFLECTION IS SHADOWED BY THE STRATA LAW, NEVER BY ONE RAY TO ITS CENTRE.
//
// A reflection ray's hit on a surface with no card takes its direct light at the hit (jah_rq_hit_direct.glsl): the
// analytic form factor times a visibility. For an area light that visibility was ONE ray to the rectangle's centre - a
// light behind a wall with a gap on its centre line reached the hit whole through the gap. It now takes the strata
// every other area-light reader takes (jah_area_strata.glsl: the cards, the pixel): one stratum a sample, drawn by its
// form factor, so the ratio sum( V f ) / sum( f ) is what the reflection converges to.
// THE FIXTURE: a 1 x 1 m area light 2.0 m over a matte floor (no cards: its hits read the direct at the hit), facing down;
// 0.2 m under it a SCREEN (an 8 x 12 m plate, four pieces) with a 0.1 m hole on the light's centre line, so every floor
// point under it sees the light through the hole only - at most 1.3 % of the rectangle's form factor at the spot under
// the hole (the hole's projection), nothing elsewhere; the centre ray passed the hole for a 1 x 1 m spot. A vertical
// mirror (a metal, roughness 0) at x = 2 under the screen; the camera between, under the screen, looking at the
// mirror, which shows the floor. The rays on, the reflection's sources Rays, a still picture, the GI's bounce OFF (the
// floor's radiance is the light's direct alone, so the analytic ratio is the whole of the screened arm's).
// TWO ARMS IN ONE PROCESS: the CONTROL (the screen hidden: the floor lit, the premise that the mirror shows the
// light's floor) and the SCREEN.
// THE LAW (R12: the form-factor ratio; it was "under 0.1 of the control, p99 under 0.25"): over the mirror's pixels that
// show the lit floor in the control (over a quarter of its brightest), each pixel's floor point found through the mirror
// and its ANALYTIC RATIO - the light's part seen through the hole over the whole (the hole's top and bottom squares
// projected onto the light's plane, intersected with it, form factors numerically) - the screened arm's mean against the
// control's times that ratio within q + 3 SE. DECLARED RED (2026-10-27): the shipped reflection at rest reads -32 %
// (0.000678 against 0.000991, 7.5 SE) - while the hit's strata are exact: with the reflection's 4,096-sample unfiltered
// reference (reflect.restReference, measured once, 8,632 frames) the same mean reads 0.001027, +3.5 %, 0.8 SE. So the
// light through a small gap is under-read by the reflection's history and filter (a sparse sample: the strata pick the
// hole's stratum ~1 % of the frames), not by the hit's direct. RED BEFORE 7-H: the spot under the hole read the light
// whole.
// A fixture (the engine alone, one offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                                                              \
    do {                                                                                                              \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); }                              \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; }                       \
    } while (0)

static const unsigned kWidth = 256, kHeight = 144;

namespace {
NodeId box(Scene *s, const Vec3 &at, const Vec3 &size, const PbrParams &p) {
    const NodeId node = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());   // no cards: a hit takes its direct at the hit
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}
float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

// THE FIXTURE'S GEOMETRY for the analytic ratio: the light's rectangle (1 x 1 m at y 2.05 over (-1, 0), facing down) and
// the hole (0.1 m square at x -1, z 0, through the screen's 0.05 m, y 1.80 .. 1.85)
const double kLx = -1.0, kLy = 2.05, kLz = 0.0, kHalfL = 0.5, kHoleHalf = 0.05, kHoleLo = 1.80, kHoleHi = 1.85;
/// the form factor of the axis-aligned rectangle [x0, x1] x [z0, z1] at height y, facing down, at the floor point P
/// (facing up): cos cos / (pi r^2) over it, n x n midpoints (the integrand is smooth over a rectangle this size)
double rectFF(double px, double pz, double x0, double x1, double z0, double z1, double y, int n)
{
    if (!(x1 > x0) || !(z1 > z0)) return 0.0;
    double sum = 0.0;
    const double dx = (x1 - x0) / n, dz = (z1 - z0) / n;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k) {
            const double qx = x0 + (i + 0.5) * dx - px, qz = z0 + (k + 0.5) * dz - pz, r2 = qx * qx + y * y + qz * qz;
            sum += y * y / (3.14159265358979 * r2 * r2) * dx * dz;
        }
    return sum;
}
/// THE ANALYTIC RATIO at the floor point P: the light's part seen through the hole over the whole - the hole's top and
/// bottom squares projected from P onto the light's plane (each an axis-aligned square), their intersection with the
/// rectangle, its form factor over the rectangle's
double holeRatio(double px, double pz)
{
    double x0 = kLx - kHalfL, x1 = kLx + kHalfL, z0 = kLz - kHalfL, z1 = kLz + kHalfL;
    for (const double yh : { kHoleLo, kHoleHi }) {
        const double k = kLy / yh;   // P + (hole point - P) k reaches y = kLy (P on y = 0)
        x0 = std::max(x0, px + (kLx - kHoleHalf - px) * k);
        x1 = std::min(x1, px + (kLx + kHoleHalf - px) * k);
        z0 = std::max(z0, pz + (kLz - kHoleHalf - pz) * k);
        z1 = std::min(z1, pz + (kLz + kHoleHalf - pz) * k);
    }
    const double full = rectFF(px, pz, kLx - kHalfL, kLx + kHalfL, kLz - kHalfL, kLz + kHalfL, kLy, 64);
    return full > 0.0 ? rectFF(px, pz, x0, x1, z0, z1, kLy, 32) / full : 0.0;
}
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-hit-area-shadow-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    Scene *s = e->createScene("hit_area_shadow");
    View *v = e->createOffscreenView("hit_area_shadow", kWidth, kHeight, Colour(0, 0, 0), OffscreenContract::StillPicture);
    if (!s || !v) { std::printf("FAIL: the scene / view\n"); return 1; }
    v->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("SKIP: no ray-query device here - the hit's direct light is the ray tier's\n");
        return 77;
    }
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 2;
        fx.reflectSources = ReflectSources::Rays;
        fx.hdrReadback = true;
        v->setPostFx(fx);
    }
    PbrParams matte;
    matte.albedo = Colour(0.8f, 0.8f, 0.8f);
    matte.roughness = 0.9f;
    PbrParams dark = matte;
    dark.albedo = Colour(0.3f, 0.3f, 0.3f);
    PbrParams mirror;
    mirror.albedo = Colour(0.95f, 0.95f, 0.95f);
    mirror.metalness = 1.0f;
    mirror.roughness = 0.0f;
    box(s, Vec3(-2.0f, -0.05f, 0.0f), Vec3(8.0f, 0.1f, 12.0f), matte);   // the floor, x -6 .. 2
    box(s, Vec3(2.05f, 0.9f, 0.0f), Vec3(0.1f, 1.8f, 12.0f), mirror);    // the mirror, under the screen
    // THE SCREEN at y 1.8 (0.05 thick), its 0.1 m hole at (-1, 1.8, 0)
    const NodeId screen[4] = {
        box(s, Vec3(-3.525f, 1.825f, 0.0f), Vec3(4.95f, 0.05f, 12.0f), dark),     // x -6 .. -1.05
        box(s, Vec3(0.525f, 1.825f, 0.0f), Vec3(2.95f, 0.05f, 12.0f), dark),      // x -0.95 .. 2
        box(s, Vec3(-1.0f, 1.825f, -3.025f), Vec3(0.1f, 0.05f, 5.95f), dark),     // z -6 .. -0.05
        box(s, Vec3(-1.0f, 1.825f, 3.025f), Vec3(0.1f, 0.05f, 5.95f), dark) };    // z 0.05 .. 6
    {
        const NodeId lamp = s->createNode();
        LightDesc l;
        l.type = LightType::Area;
        l.rectWidth = 1.0f;
        l.rectHeight = 1.0f;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 8.0f;
        l.range = 20.0f;
        s->setNodeTransform(lamp, Vec3(-1.0f, 2.05f, 0.0f), Quat(), Vec3(1, 1, 1));   // facing down, over the hole
        s->setLight(lamp, l);
    }
    s->setAmbient(Colour(0.0f, 0.0f, 0.0f), Colour(0.0f, 0.0f, 0.0f));
    // NO BOUNCE (the floor's radiance the area light's direct alone: the analytic ratio is the whole of the screened arm's)
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = false;
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    const Vec3 eye(1.0f, 0.8f, 0.0f), at(2.0f, 0.6f, 0.0f);
    enginetest::testCameraLookAt(v, eye, at);
    e->waitForAsyncShaders();
    std::printf("   MEASURING ARM reflect.restNoStop: %d\n", int(e->setArm("reflect.restNoStop", 1.0)));

    std::vector<float> pic[2];
    for (int arm = 0; arm < 2; ++arm) {
        for (NodeId n : screen) s->setNodeVisible(n, arm == 1);
        int rest = -1;
        for (int f = 0, quiet = 0; f < 400; ++f) {
            run(1);
            const GiStatus g = s->published().gi;
            quiet = !g.chainOwed && !g.feedbackOwed && !g.cascades.empty() ? quiet + 1 : 0;
            if (quiet >= 12) { rest = f + 1 - quiet; break; }   // (the first quiet frame's index: 0 when quiet from the start)
        }
        run(64);   // the reflection's samples (one a texel a frame) at the GI's rest
        ImageF img;
        const bool read = v->readPixelsHdr(img) && img.width == kWidth && img.height == kHeight;
        std::printf("   the %s arm: the GI at rest after %d frames, read %d\n", arm ? "SCREEN" : "control", rest,
                    int(read));
        CHECK(rest >= 0 && read, "%s arm: at rest and read back", arm ? "the screen" : "the control");
        if (!read) continue;
        pic[arm].resize(size_t(kWidth) * kHeight);
        for (unsigned y = 0; y < kHeight; ++y)
            for (unsigned x = 0; x < kWidth; ++x) pic[arm][size_t(y) * kWidth + x] = lum(img.at(x, y));
    }
    if (pic[0].empty() || pic[1].empty()) {
        std::printf("\nrtreflect.hit_area_shadow: %d frames, FAIL\n", frames);
        return 1;
    }
    // THE MIRROR'S LIT FLOOR in the control (below the picture's horizon row, over a quarter of its peak), each pixel's
    // floor point found through the mirror (the plane x = 2.0: the view ray reflected, on to y = 0) and its analytic
    // ratio - the light seen through the hole over the whole
    float peak = 0.0f;
    for (unsigned y = kHeight / 2; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) peak = std::max(peak, pic[0][size_t(y) * kWidth + x]);
    const double fx0 = at.x - eye.x, fy0 = at.y - eye.y, fz0 = at.z - eye.z;
    const double fl = std::sqrt(fx0 * fx0 + fy0 * fy0 + fz0 * fz0);
    const double F[3] = { fx0 / fl, fy0 / fl, fz0 / fl };
    double R[3] = { -F[2], 0.0, F[0] };
    const double rl = std::sqrt(R[0] * R[0] + R[2] * R[2]);
    R[0] /= rl;
    R[2] /= rl;
    const double U[3] = { R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0] };
    const double tanY = std::tan(22.5 * 3.14159265358979 / 180.0), tanX = tanY * double(kWidth) / double(kHeight);
    double sumC = 0.0, sumS = 0.0, sumRef = 0.0, sumD2 = 0.0, ratioMax = 0.0;
    size_t n = 0;
    std::vector<double> dev;
    for (unsigned y = kHeight / 2; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) {
            const size_t i = size_t(y) * kWidth + x;
            if (!(pic[0][i] > 0.25f * peak)) continue;
            const double nx = (2.0 * (x + 0.5) / kWidth - 1.0) * tanX, ny = (1.0 - 2.0 * (y + 0.5) / kHeight) * tanY;
            double d[3] = { F[0] + R[0] * nx + U[0] * ny, F[1] + R[1] * nx + U[1] * ny, F[2] + R[2] * nx + U[2] * ny };
            if (!(d[0] > 0.0)) continue;
            const double tm = (2.0 - eye.x) / d[0];
            const double my = eye.y + d[1] * tm, mz = eye.z + d[2] * tm;
            if (my < 0.0 || my > 1.8 || std::fabs(mz) > 6.0) continue;   // not the mirror
            d[0] = -d[0];
            if (!(d[1] < 0.0)) continue;
            const double tf = -my / d[1];
            const double px = 2.0 + d[0] * tf, pz = mz + d[2] * tf;
            if (px < -6.0 || px > 2.0) continue;
            const double ratio = holeRatio(px, pz), ref = double(pic[0][i]) * ratio;
            ratioMax = std::max(ratioMax, ratio);
            sumC += pic[0][i];
            sumS += pic[1][i];
            sumRef += ref;
            dev.push_back(double(pic[1][i]) - ref);
            ++n;
        }
    const double meanC = n ? sumC / double(n) : 0.0, meanS = n ? sumS / double(n) : 0.0;
    const double meanRef = n ? sumRef / double(n) : 0.0;
    for (const double dv : dev) sumD2 += (dv - (meanS - meanRef)) * (dv - (meanS - meanRef));
    const double seMean = n > 1 ? std::sqrt(sumD2 / double(n - 1) / double(n)) : 0.0;
    const double q = meanRef > 0.0 ? std::ldexp(1.0, int(std::floor(std::log2(meanRef))) - 10) : 0.0;
    std::printf("   the mirror's lit floor (control > 0.25 x its peak %.4f): %zu px, the control's mean %.4f; the screened "
                "mean %.6f against the analytic %.6f (the hole's ratio at each pixel's floor point, its largest %.4f): "
                "%+.6f, %.2f SE (%.6f), q %.6f\n", double(peak), n, meanC, meanS, meanRef, ratioMax, meanS - meanRef,
                seMean > 0 ? std::fabs(meanS - meanRef) / seMean : 0.0, seMean, q);
    CHECK(n > 1500u && meanC > 1e-3, "premise: the mirror shows the light's floor (%zu px, mean %.4f)", n, meanC);
    CHECK(std::fabs(meanS - meanRef) <= q + 3.0 * seMean,
          "THE STRATA'S RATIO: the screened floor in the mirror the control's times the analytic form-factor ratio through "
          "the hole, the mean within q + 3 SE (L9)");
    v->setScene(nullptr);
    e->destroyView(v);
    e->destroyScene(s);
    run(2);
    std::printf("\nrtreflect.hit_area_shadow: %d frames, %s\n", frames, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
