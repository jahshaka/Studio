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
// mirror, which shows the floor. The rays on, the reflection's sources Rays, a still picture.
// TWO ARMS IN ONE PROCESS: the CONTROL (the screen hidden: the floor lit, the premise that the mirror shows the
// light's floor) and the SCREEN.
// THE LAW: over the mirror's pixels that show the lit floor in the control (over a quarter of its brightest), the
// screen arm's mean under 0.1 of the control's and its 99th percentile under 0.25 of the control's mean - the strata's
// truth is ~0.013 plus the bounce under the screen. RED BEFORE 7-H: the spot under the hole read the light whole.
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
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    enginetest::testCameraLookAt(v, Vec3(1.0f, 0.8f, 0.0f), Vec3(2.0f, 0.6f, 0.0f));
    e->waitForAsyncShaders();

    std::vector<float> pic[2];
    for (int arm = 0; arm < 2; ++arm) {
        for (NodeId n : screen) s->setNodeVisible(n, arm == 1);
        int rest = -1;
        for (int f = 0, quiet = 0; f < 400; ++f) {
            run(1);
            const GiStatus g = s->published().gi;
            quiet = !g.chainOwed && !g.feedbackOwed && !g.cascades.empty() ? quiet + 1 : 0;
            if (quiet >= 12) { rest = f - quiet; break; }
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
    // THE MIRROR'S LIT FLOOR in the control: below the picture's horizon row (the floor), over a quarter of its peak
    float peak = 0.0f;
    for (unsigned y = kHeight / 2; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) peak = std::max(peak, pic[0][size_t(y) * kWidth + x]);
    std::vector<float> screened;
    double sumC = 0.0, sumS = 0.0;
    unsigned spotX = 0, spotY = 0;
    float spotV = -1.0f;
    for (unsigned y = kHeight / 2; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) {
            const size_t i = size_t(y) * kWidth + x;
            if (!(pic[0][i] > 0.25f * peak)) continue;
            sumC += pic[0][i];
            sumS += pic[1][i];
            screened.push_back(pic[1][i]);
            if (pic[1][i] > spotV) { spotV = pic[1][i]; spotX = x; spotY = y; }
        }
    const size_t n = screened.size();
    std::sort(screened.begin(), screened.end());
    const double meanC = n ? sumC / double(n) : 0.0, meanS = n ? sumS / double(n) : 0.0;
    const double p99 = n ? double(screened[std::min(n - 1, size_t(0.99 * double(n)))]) : 0.0;
    std::printf("   the mirror's lit floor (control > 0.25 x its peak %.4f): %zu px; the control's mean %.4f; the "
                "screen's mean %.4f (%.4f of it), p99 %.4f (%.4f), max %.4f (%.4f) at (%u, %u)\n",
                double(peak), n, meanC, meanS, meanC > 0 ? meanS / meanC : 0.0, p99, meanC > 0 ? p99 / meanC : 0.0,
                double(spotV), meanC > 0 ? double(spotV) / meanC : 0.0, spotX, spotY);
    CHECK(n > 1500u && meanC > 1e-3, "premise: the mirror shows the light's floor (%zu px, mean %.4f)", n, meanC);
    CHECK(meanS < 0.1 * meanC, "the screened floor in the mirror reads under 0.1 of the lit one (%.4f) - the light "
          "reaches it through the hole only (L9)", meanC > 0 ? meanS / meanC : 0.0);
    CHECK(p99 < 0.25 * meanC, "no spot of it reads the light through the gap whole: p99 %.4f of the lit mean (< 0.25)",
          meanC > 0 ? p99 / meanC : 0.0);
    v->setScene(nullptr);
    e->destroyView(v);
    e->destroyScene(s);
    run(2);
    std::printf("\nrtreflect.hit_area_shadow: %d frames, %s\n", frames, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
