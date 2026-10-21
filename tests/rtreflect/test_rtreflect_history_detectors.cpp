// rtreflect.history_detectors — THE REFLECTION HISTORY'S TWO DETECTORS (PHOTON-5 stage 4;
// SPECS/audits/PHOTON_AUDIT_2026-10-21.md F4).
//
// The ray tier's reflection is a temporal mean (rq_reflect.comp): at rest a glossy texel
// remembers about thirty-two frames (the floor on 1/n, kReflectHistoryFloor). Two things
// make that memory WRONG rather than merely old, and each has a detector:
//   (a) THE RADIANCE MOVED: a lamp's intensity stepped. The surface and what it reflects
//       are the same, so no reprojection test fires, and the old radiance drained out of
//       a still surface's mean over the floor's ~94 frames. The host passes "the radiance
//       serial moved" in the pass's Params and the mean's count is capped that frame.
//   (b) THE EYE MOVED: a sideways slide moves a reflection across its surface. PAN-SMEAR-1's
//       parallax drop already holds the trail under a code (0.842 measured at the base); this
//       arm is its guard. (A fetch through the reflected image's point was built and measured
//       worse, 1.571, and dropped: PHOTON-5's report.)
//
// FIXTURES (High, rays on, 384 x 216):
//   (a) a glossy metal floor (perceptual roughness 0.2 - above the mirror ramp, where the
//       mean HAS a memory; a true mirror takes every sample whole) reflecting a matte box
//       lit by one lamp; the lamp's intensity doubles at frame 40. The floor patch where
//       the box is reflected is read in HDR every frame; the number is the frames from the
//       step until it holds 95 % of its settled change (24 at the base, 11 with the cap; the
//       law 2 x kWarmupFrames).
//   (b) PAN-SMEAR-1's pillar fixture (gi.reflect_motion, archived): a glossy floor, seven
//       emissive pillars, a 24-frame slide at 0.25 m a frame, the moving floor against the
//       floor the renderer settles to at the same pose, in 8-bit codes.
// The base numbers are printed and stated in the checks' comments.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static const unsigned kWidth = 384;
static const unsigned kHeight = 216;
static const int kWarmupFrames = 8;    // rq_reflect.comp's kWarmupFrames

static NodeId addBox(Scene *s, const PbrParams &p, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = s->createNode();
    s->attachMesh(n, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(p));
    enginetest::setNodeScale(s, n, scale);
    enginetest::setNodePosition(s, n, pos);
    return n;
}

/// Where a world point lands in the image of a lookAt camera (+Y up, the default 45 deg
/// vertical field of view), in pixels.
static bool project(const Vec3 &eye, const Vec3 &target, const Vec3 &w, float &px, float &py)
{
    auto sub = [](Vec3 a, Vec3 b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); };
    auto dot = [](Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    auto cross = [](Vec3 a, Vec3 b) { return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); };
    auto norm = [&](Vec3 a) { const float l = std::sqrt(dot(a, a)); return Vec3(a.x / l, a.y / l, a.z / l); };
    const Vec3 f = norm(sub(target, eye));
    const Vec3 r = norm(cross(f, Vec3(0, 1, 0)));
    const Vec3 u = cross(r, f);
    const Vec3 d = sub(w, eye);
    const float z = dot(d, f);
    if (z <= 0.0f) return false;
    const float t = std::tan(0.5f * 45.0f * 3.14159265f / 180.0f);
    const float aspect = float(kWidth) / float(kHeight);
    const float x = dot(d, r) / (z * t * aspect), y = dot(d, u) / (z * t);
    px = (x * 0.5f + 0.5f) * float(kWidth);
    py = (0.5f - y * 0.5f) * float(kHeight);
    return true;
}

static double patchLuma(const ImageF &img, int cx, int cy, int half)
{
    double s = 0.0;
    int n = 0;
    for (int y = cy - half; y <= cy + half; ++y)
        for (int x = cx - half; x <= cx + half; ++x) {
            const Colour c = img.at(unsigned(x), unsigned(y));
            s += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
            ++n;
        }
    return n ? s / n : 0.0;
}

static float meanDiff(const Image &a, const Image &b, unsigned y0, unsigned y1)
{
    double sum = 0.0;
    size_t n = 0;
    for (unsigned y = y0; y < y1 && y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const size_t i = (size_t(y) * a.width + x) * 4u;
            for (int c = 0; c < 3; ++c) { sum += std::fabs(double(a.rgba[i + c]) - double(b.rgba[i + c])); ++n; }
        }
    return n ? float(sum / double(n)) : 1e9f;
}

static void rayTier(Scene *s, View *view)
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.testProbeRegionMin = Vec3(-30.0f, -2.0f, -30.0f);
    gi.testProbeRegionMax = Vec3(30.0f, 10.0f, 30.0f);
    s->setGlobalIllumination(gi);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    fx.hdrReadback = true;
    view->setPostFx(fx);
}

/// (a) THE LAMP STEP: frames from the step until the reflection holds 95 % of its change.
static int lampStep(Engine *e, View *view)
{
    Scene *s = e->createScene("lampstep");
    view->setScene(s);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.03f, 0.03f, 0.03f));
    PbrParams floorP;
    floorP.albedo = Colour(0.85f, 0.85f, 0.85f);
    floorP.metalness = 1.0f;
    floorP.roughness = 0.2f;
    addBox(s, floorP, Vec3(0.0f, -0.1f, 0.0f), Vec3(40.0f, 0.2f, 40.0f));
    PbrParams boxP;
    boxP.albedo = Colour(0.8f, 0.8f, 0.8f);
    boxP.roughness = 0.9f;
    addBox(s, boxP, Vec3(0.0f, 1.0f, -3.0f), Vec3(3.0f, 2.0f, 1.0f));
    const NodeId lamp = s->createNode();
    s->setNodeTransform(lamp, Vec3(0.0f, 2.0f, 0.5f), Quat(), Vec3(1, 1, 1));
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1, 1, 1);
    l.intensity = 6.0f;
    l.range = 20.0f;
    l.castShadows = false;
    s->setLight(lamp, l);
    const Vec3 eye(0.0f, 1.2f, 6.0f), target(0.0f, 0.2f, -2.0f);
    enginetest::testCameraLookAt(view, eye, target);
    rayTier(s, view);

    // The box's front face centre (0, 1, -2.5) mirrored in the floor: where its reflection is.
    float px = 0, py = 0;
    project(eye, target, Vec3(0.0f, -1.0f, -2.5f), px, py);
    const int cx = int(px), cy = int(py);

    enginetest::frames(e, 120);   // the view warm, the mean converged at the old intensity
    std::vector<double> trace;
    ImageF img;
    for (int f = 0; f < 40; ++f) {
        enginetest::frames(e, 1);
        view->readPixelsHdr(img);
        trace.push_back(patchLuma(img, cx, cy, 3));
    }
    l.intensity = 12.0f;
    s->setLight(lamp, l);
    const size_t stepAt = trace.size();
    for (int f = 0; f < 220; ++f) {
        enginetest::frames(e, 1);
        view->readPixelsHdr(img);
        trace.push_back(patchLuma(img, cx, cy, 3));
    }
    double before = 0.0, after = 0.0;
    for (size_t i = stepAt - 10; i < stepAt; ++i) before += trace[i] / 10.0;
    for (size_t i = trace.size() - 20; i < trace.size(); ++i) after += trace[i] / 20.0;
    int reached = -1;
    for (size_t i = stepAt; i < trace.size(); ++i) {
        const double frac = (trace[i] - before) / std::max(after - before, 1e-9);
        if (frac >= 0.95) {
            bool holds = true;   // and stays there (one noisy frame is no arrival)
            for (size_t k = i; k < std::min(i + 5, trace.size()); ++k)
                holds = holds && (trace[k] - before) / std::max(after - before, 1e-9) >= 0.90;
            if (holds) { reached = int(i - stepAt) + 1; break; }
        }
    }
    std::printf("-- (a) lamp 6 -> 12 at frame 40: reflection patch (%d,%d) %.4f -> %.4f; first frames after "
                "the step:", cx, cy, before, after);
    for (size_t i = stepAt; i < stepAt + 12 && i < trace.size(); ++i)
        std::printf(" %.0f%%", 100.0 * (trace[i] - before) / std::max(after - before, 1e-9));
    std::printf("\n   95 %% of the change held after %d frames\n", reached);
    e->destroyScene(s);
    return reached;
}

/// (b) THE SLIDE: the moving floor against the settled floor, 8-bit codes.
static float slideTrail(Engine *e, View *view, float &stillOut)
{
    Scene *s = e->createScene("slide");
    view->setScene(s);
    s->setAmbient(Colour(0.45f, 0.55f, 0.70f), Colour(0.30f, 0.30f, 0.32f));
    PbrParams floorP;
    floorP.albedo = Colour(0.85f, 0.85f, 0.85f);
    floorP.metalness = 1.0f;
    floorP.roughness = 0.2f;
    addBox(s, floorP, Vec3(0.0f, -0.1f, 0.0f), Vec3(120.0f, 0.2f, 120.0f));
    const Colour cols[7] = { Colour(1.0f, 0.066f, 0.066f), Colour(0.066f, 1.0f, 0.066f),
                             Colour(0.066f, 0.133f, 1.0f), Colour(1.0f, 0.8f, 0.066f),
                             Colour(1.0f, 0.066f, 1.0f),   Colour(0.066f, 0.933f, 0.933f),
                             Colour(1.0f, 1.0f, 1.0f) };
    for (int i = 0; i < 7; ++i) {
        PbrParams p;
        p.albedo = Colour(0.05f, 0.05f, 0.05f);
        p.emissive = cols[i];
        p.roughness = 0.9f;
        addBox(s, p, Vec3(-12.0f + 4.0f * float(i), 1.5f, -2.0f), Vec3(1.0f, 3.0f, 1.0f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);
    rayTier(s, view);
    auto pose = [&](int frame) {
        const float x = -0.25f * float(frame);
        enginetest::testCameraLookAt(view, Vec3(x, 5.0f, 14.0f), Vec3(x, 1.0f, 0.0f));
    };
    // THE CONTROL: a still camera's two read-backs (the mean's own restlessness).
    pose(0);
    enginetest::frames(e, 84);
    Image a, b;
    view->readPixels(a);
    enginetest::frames(e, 45);
    view->readPixels(b);
    stillOut = meanDiff(a, b, kHeight / 3u, kHeight);
    for (int i = 1; i <= 24; ++i) { pose(i); enginetest::frames(e, 1); }
    Image moving, settled;
    view->readPixels(moving);
    enginetest::frames(e, 45);
    view->readPixels(settled);
    const float err = meanDiff(moving, settled, kHeight / 3u, kHeight);
    // THE WORST 16-COLUMN BAND of the floor: a trail sits beside each pillar's reflection, and the
    // floor's mean dilutes it over the floor between them.
    float worstBand = 0.0f;
    for (unsigned x0 = 0; x0 + 16u <= kWidth; x0 += 16u) {
        double sum = 0.0; size_t n = 0;
        for (unsigned y = kHeight / 3u; y < kHeight; ++y)
            for (unsigned x = x0; x < x0 + 16u; ++x) {
                const size_t i = (size_t(y) * kWidth + x) * 4u;
                for (int c = 0; c < 3; ++c) { sum += std::fabs(double(moving.rgba[i + c]) - double(settled.rgba[i + c])); ++n; }
            }
        worstBand = std::max(worstBand, float(sum / double(n)));
    }
    std::printf("-- (b) slide 0.25 m/frame x 24: floor moving-vs-settled %.3f codes, worst 16-column band %.3f "
                "(still control %.3f)\n", err, worstBand, stillOut);
    e->destroyScene(s);
    return err;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-rtreflect-history-detectors-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    // THE DEVICE ONLY EXISTS ONCE A VIEW DOES (the startup-order law).
    View *view = e->createOffscreenView("historydetectors", kWidth, kHeight, Colour(0.45f, 0.55f, 0.70f));
    if (!(e->rayQueryAvailable() && e->rayTracing())) {
        std::printf("ok: no ray queries on this machine - the row is about the ray tier\n");
        return 0;
    }

    const int frames95 = lampStep(e, view);
    // THE LAW (the lead's ruling on the measurement): 95 % of a radiance change holds within
    // 2 x kWarmupFrames frames. Measured: 24 at the base (the 1/32 floor's drain), 11 with the cap.
    CHECK_MSG(frames95 > 0 && frames95 <= 2 * kWarmupFrames,
              "(a) A LAMP STEP reaches the still glossy floor's reflection within 2 x kWarmupFrames = %d frames "
              "(%d; 24 at the base)", 2 * kWarmupFrames, frames95);

    float still = 0.0f;
    const float trail = slideTrail(e, view, still);
    // A GUARD (PAN-SMEAR-1's law, held): measured 0.842 at the base. A history fetched through the
    // reflected image's point under a moved eye was built and measured WORSE (1.571) - a glossy
    // lobe's mean length is its far rays' - and was dropped.
    CHECK_MSG(trail <= 1.0f, "(b) A SLIDE leaves no trail: the moving floor within 1 code of the settled (%.3f; "
              "0.842 at the base)", trail);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
