// gi.gather_sliver (PHOTON-7 stage 10) - THE GRID ANSWERS EVERY PIXEL: a 1-px sliver of a surface seen past an occluder
// is answered by a probe on EVERY frame, and holds its surface's own irradiance.
//
// The screen-probe gather puts one probe a cell (16 px at High) at the frame's jittered pixel, and a cell whose pixels
// the grid's bilinear four cannot answer gets ONE TWIN (rq_probe_place.comp). The twin's test sampled four quarter
// points of the cell (offsets 2.5 and 10.5 at High), so a sliver between them was never tested: only the jitter put a
// probe on it, one frame in 16, and every other frame it took the fallback's share - gi.card_residency's x = 255
// column (a 1-px line of floor behind a pillar at the view's edge): 51 codes a settling frame, its held value 21-135
// codes against the floor's ~200. The test now reads EVERY pixel of the cell (Lumen's support test,
// ScreenProbeAdaptivePlacementMarkCS, over all of them) and the twin stands at the centre of the unanswered ones.
//
// THE FIXTURE: a 200 m sunlit floor and a 16 m wall 12 m ahead of a camera 8 m up (the wall fills the view; the
// floor's bounce is the wall's indirect), the field off, the environment black. Two BLACK plates half a metre ahead of
// the camera cover the view but for two 1-px columns of the wall: x = 128 (between the plates: an interior sliver) and
// x = 255 (past the second plate: an edge sliver, the residency row's case). The plates' shadow falls on the floor
// 5.5 m short of the wall (its bounce share there 0.14 % by the form factor) and what they hide from the wall is the
// black sky: THE TRUTH is the same view with no plates, the wall whole and probed everywhere.
// THE LAWS (High: 256 x 256, a probe per 16 px):
//   (1) EVERY FRAME ANSWERED: over 48 settling frames (gather.restOff: every frame the history's), every pixel of
//       both slivers has a probe answer on every frame - its answered share w (the history's mean of the frames a
//       probe answered) is 1 on every frame read. RED before: w under 1 on all 12,288 pixel-frames of each;
//   (2) ITS NOISE IS ITS SURFACE'S: each sliver's frame-to-frame sd (relative) within the range of the wall's own
//       pixels in the open region (x 8..88) on the same rows - RED before: 0.56 and 0.73 against 0.004..0.036;
//   (3) HELD WITHIN A CODE OF ITS TRUTH: at rest (held), each sliver column's mean irradiance within one code (1 %) of
//       the same column's in the plate-free view, every channel - RED before: 1.15 and 3.31 codes.
// And PRINTED: the placement's GPU ms (gather.place, its two passes) on a 1920 x 1080 view at High and Epic (the four
// quarter samples before: 0.0225 and 0.0297 ms; every pixel: 0.0655 and 0.168 ms), and the twins requested against the
// cap.
// A fixture (the engine alone, offscreen views). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
constexpr unsigned kW = 256u, kH = 256u;
constexpr float kEyeY = 8.0f, kPlateDepth = 0.5f, kFovDeg = 60.0f;
constexpr unsigned kSlits[2] = { 128u, 255u };
constexpr unsigned kOpenX0 = 8u, kOpenX1 = 88u;

NodeId addBox(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId node = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = albedo;
    p.metalness = 0.0f;
    p.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    s->setNodeTransform(node, pos, Quat(), scale);
    return node;
}

/// a pixel BOUNDARY's world x on the plates' plane (the camera at x 0 looking down -z, a square view)
float boundaryX(unsigned b)
{
    return (float(b) / float(kW / 2u) - 1.0f) * kPlateDepth * std::tan(0.5f * kFovDeg * 3.14159265f / 180.0f);
}

struct Run {
    bool ok = false;
    int settle = -1;
    unsigned framesRead = 0u;
    /// (1): per sliver, the pixel-frames with no probe answer; (2): per sliver, its pixels' mean relative sd, and the
    /// open region's range of per-pixel relative sd on the same rows
    unsigned unanswered[2] = { 0u, 0u };
    double slitSd[2] = { 0.0, 0.0 }, openSdMin = 1e9, openSdMax = 0.0;
    /// (3): the held picture's column means per sliver (rgb) and the pixels in it
    double held[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
    unsigned heldPixels[2] = { 0u, 0u };
    unsigned adaptive = 0u, cap = 0u, requested = 0u;
};

void frames(Engine *e, int n, int &count) { enginetest::frames(e, n); count += n; }

bool runOnce(Engine *e, bool plates, Run &out, int &count)
{
    const char *name = plates ? "sliver" : "sliver_truth";
    View *view = e->createOffscreenView(name, kW, kH, Colour(0, 0, 0));
    Scene *scene = e->createScene(name);
    if (!view || !scene) return false;
    view->setScene(scene);
    {
        PostFxDesc fx;   // the chain, so the gather runs
        fx.allowOffscreen = true;
        fx.ssr = 1;
        view->setPostFx(fx);
    }
    view->setOffscreenContract(OffscreenContract::StillPicture);
    scene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    const Colour white(0.8f, 0.8f, 0.8f), black(0.0f, 0.0f, 0.0f);
    // (200 m wide: the wall's bounce flat across the view's +-7 m - at 30 m its fall toward the floor's end moved the
    // edge column's truth, which the border's bracket reads 7 px in on average, 1.1 % off the point itself)
    addBox(scene, white, Vec3(0.0f, -0.1f, -10.0f), Vec3(200.0f, 0.2f, 20.0f));    // the floor, z -20..0
    addBox(scene, white, Vec3(0.0f, 8.0f, -12.1f), Vec3(200.0f, 16.0f, 0.2f));     // the wall, its face at z -12
    if (plates) {
        // their FRONT faces on the pixel boundaries 96 | 128 and 129 | 255 (half a pixel from every centre)
        const float zc = -kPlateDepth - 0.001f, t = 0.002f, h = 0.8f;
        const float a0 = boundaryX(96u), a1 = boundaryX(128u), b0 = boundaryX(129u), b1 = boundaryX(255u);
        addBox(scene, black, Vec3(0.5f * (a0 + a1), kEyeY, zc), Vec3(a1 - a0, h, t));
        addBox(scene, black, Vec3(0.5f * (b0 + b1), kEyeY, zc), Vec3(b1 - b0, h, t));
    }
    enginetest::addDirectionalLight(scene, Vec3(0.0f, -0.8f, -0.6f), 3.0f);
    CameraDesc cam;
    cam.position = Vec3(0.0f, kEyeY, 0.0f);   // identity: looking down -z
    cam.fovDegrees = kFovDeg;
    cam.nearClip = 0.05f;
    view->setCamera(cam);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.bounces = true;
    gi.cards = false;   // the hits read the voxels: the gather's placement alone is the subject
    gi.updateBudget = 0;
    if (!scene->setGlobalIllumination(gi)) return false;
    // the GI at rest: the chain owes nothing for 30 frames
    int quiet = 0, f = 0;
    for (; f < 900 && quiet < 30; ++f) {
        frames(e, 1, count);
        const GiStatus g = scene->published().gi;
        quiet = !g.chainOwed && !g.feedbackOwed ? quiet + 1 : 0;
    }
    out.settle = quiet >= 30 ? f - quiet : -1;

    // (1)(2): 48 settling frames, every frame the history's, each gathered frame's E/pi read back
    e->setArm("gather.irradianceReadback", 1.0);
    e->setArm("gather.restOff", 1.0);
    std::vector<double> sum(size_t(kW) * kH, 0.0), sq(size_t(kW) * kH, 0.0);
    unsigned last = 0u;
    for (int k = 0; k < 52; ++k) {
        frames(e, 1, count);
        const GatherStatus &gs = scene->published().gi.gather;
        if (k < 4 || gs.irradiance.size() != size_t(kW) * kH * 4u || gs.irradianceFrame == last) continue;
        last = gs.irradianceFrame;
        ++out.framesRead;
        out.adaptive = std::max(out.adaptive, gs.adaptive);
        out.cap = gs.adaptiveCap;
        out.requested = std::max(out.requested, gs.adaptiveRequested);
        for (size_t i = 0; i < size_t(kW) * kH; ++i) {
            const double v = gs.irradiance[i * 4u + 1u];
            sum[i] += v;
            sq[i] += v * v;
            for (int s = 0; s < 2; ++s)
                if (plates && i % kW == kSlits[s] && gs.irradiance[i * 4u + 3u] < 0.999f) ++out.unanswered[s];
        }
    }
    const auto relSd = [&](size_t i) {
        const double n = std::max(double(out.framesRead), 1.0), m = sum[i] / n;
        return m > 0.0 ? std::sqrt(std::max(sq[i] / n - m * m, 0.0)) / m : 1.0;
    };
    for (int s = 0; s < 2; ++s) {
        double acc = 0.0;
        for (unsigned y = 0; y < kH; ++y) acc += relSd(size_t(y) * kW + kSlits[s]);
        out.slitSd[s] = acc / double(kH);
    }
    for (unsigned y = 0; y < kH; ++y)
        for (unsigned x = kOpenX0; x <= kOpenX1; ++x) {
            const double r = relSd(size_t(y) * kW + x);
            out.openSdMin = std::min(out.openSdMin, r);
            out.openSdMax = std::max(out.openSdMax, r);
        }

    // (3): the rest mean, held - its picture read back
    e->setArm("gather.restOff", 0.0);
    int held = 0;
    for (int k = 0; k < 120 && held < 4; ++k) {
        frames(e, 1, count);
        const GatherStatus &gs = scene->published().gi.gather;
        held = gs.restFrames > gs.settleFrames + 1u ? held + 1 : 0;
    }
    const GatherStatus &gs = scene->published().gi.gather;
    if (held >= 4 && gs.irradiance.size() == size_t(kW) * kH * 4u)
        for (int s = 0; s < 2; ++s) {
            for (unsigned y = 0; y < kH; ++y) {
                const size_t i = (size_t(y) * kW + kSlits[s]) * 4u;
                for (int c = 0; c < 3; ++c) out.held[s][c] += gs.irradiance[i + size_t(c)];
                ++out.heldPixels[s];
            }
            for (int c = 0; c < 3; ++c) out.held[s][c] /= std::max(double(out.heldPixels[s]), 1.0);
        }
    e->setArm("gather.irradianceReadback", 0.0);
    out.ok = out.settle >= 0 && held >= 4 && out.framesRead >= 32u;
    e->destroyView(view);
    e->destroyScene(scene);
    return out.ok;
}

/// THE PRICE: the placement's two passes (the monitor's gather.place) on a 1920 x 1080 view of the plated scene
float placeMs(Engine *e, GiQuality q, int &count, unsigned &requested, unsigned &cap)
{
    View *view = e->createOffscreenView("sliver_price", 1920u, 1080u, Colour(0, 0, 0));
    Scene *scene = e->createScene("sliver_price");
    if (!view || !scene) return -1.0f;
    view->setScene(scene);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 1;
    view->setPostFx(fx);
    view->setOffscreenContract(OffscreenContract::StillPicture);
    scene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    addBox(scene, Colour(0.8f, 0.8f, 0.8f), Vec3(0.0f, -0.1f, -10.0f), Vec3(30.0f, 0.2f, 20.0f));
    addBox(scene, Colour(0.8f, 0.8f, 0.8f), Vec3(0.0f, 8.0f, -12.1f), Vec3(30.0f, 16.0f, 0.2f));
    enginetest::addDirectionalLight(scene, Vec3(0.0f, -0.8f, -0.6f), 3.0f);
    CameraDesc cam;
    cam.position = Vec3(0.0f, 3.0f, 0.0f);
    cam.fovDegrees = kFovDeg;
    view->setCamera(cam);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = q;
    gi.ddgi = GiToggle::Off;
    gi.bounces = true;
    gi.cards = false;
    if (!scene->setGlobalIllumination(gi)) return -1.0f;
    e->setArm("gather.restOff", 1.0);
    frames(e, 8, count);
    float ms = -1.0f;
    {
        enginetest::GpuTimingWindow window(e);
        float best = 1e9f;
        for (int k = 0; k < 8; ++k) {
            frames(e, 1, count);
            const GatherStatus &gs = scene->published().gi.gather;
            if (gs.placeMs >= 0.0f) best = std::min(best, gs.placeMs);
            requested = gs.adaptiveRequested;
            cap = gs.adaptiveCap;
        }
        ms = best < 1e9f ? best : -1.0f;
    }
    e->setArm("gather.restOff", 0.0);
    e->destroyView(view);
    e->destroyScene(scene);
    return ms;
}
}  // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-gather-sliver-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int count = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));

    Run sl, tr;
    const bool okS = runOnce(e, true, sl, count);
    const bool okT = runOnce(e, false, tr, count);
    std::printf("   the plated view: GI at rest in %d frames, %u gathered frames read | twins %u of a cap %u (asked %u)\n",
                sl.settle, sl.framesRead, sl.adaptive, sl.cap, sl.requested);
    std::printf("   the truth (no plates): GI at rest in %d frames\n", tr.settle);
    CHECK(okS && okT, "both views came to rest and were read");
    const char *what[2] = { "x 128 (between the plates)", "x 255 (the view's edge)" };
    for (int s = 0; s < 2; ++s)
        std::printf("   (1)(2) the sliver at %s: %u of %u pixel-frames unanswered | its mean relative sd %.4f, the open "
                    "wall's pixels %.4f..%.4f\n", what[s], sl.unanswered[s], sl.framesRead * kH, sl.slitSd[s],
                    sl.openSdMin, sl.openSdMax);
    CHECK(okS && sl.unanswered[0] == 0u && sl.unanswered[1] == 0u,
          "(1) EVERY FRAME ANSWERED: every pixel of both slivers has a probe answer on every settling frame");
    CHECK(okS && sl.slitSd[0] <= sl.openSdMax && sl.slitSd[1] <= sl.openSdMax,
          "(2) ITS NOISE IS ITS SURFACE'S: each sliver's frame-to-frame sd within the open wall's range");
    bool within = okS && okT;
    for (int s = 0; s < 2; ++s) {
        double worst = 0.0;
        for (int c = 0; c < 3; ++c) {
            const double t = std::max(tr.held[s][c], 5.9604645e-6);
            worst = std::max(worst, std::fabs(sl.held[s][c] - tr.held[s][c]) / (0.01 * t));
        }
        within = within && worst <= 1.0;
        std::printf("   (3) the sliver at %s, held: %.5f %.5f %.5f against its truth %.5f %.5f %.5f (%u px) - %.2f codes\n",
                    what[s], sl.held[s][0], sl.held[s][1], sl.held[s][2], tr.held[s][0], tr.held[s][1], tr.held[s][2],
                    sl.heldPixels[s], worst);
    }
    CHECK(within, "(3) HELD WITHIN A CODE OF ITS TRUTH: each sliver column's held irradiance within 1 % of the plate-free view's");

    unsigned rqH = 0u, capH = 0u, rqE = 0u, capE = 0u;
    const float msH = placeMs(e, GiQuality::High, count, rqH, capH);
    const float msE = placeMs(e, GiQuality::Epic, count, rqE, capE);
    std::printf("   THE PRICE (1920 x 1080, gather.place's two passes): High %.4f ms (twins asked %u of a cap %u), Epic "
                "%.4f ms (%u of %u)\n", double(msH), rqH, capH, double(msE), rqE, capE);

    std::printf("\nframes %d\n%s (%d failure%s)\n", count, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
