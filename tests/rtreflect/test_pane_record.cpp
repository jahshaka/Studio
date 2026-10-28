// rtreflect.pane_record — A REFLECTION'S RECORD SEEN THROUGH GLASS IS SHADED THROUGH IT (PHOTON-7 10-4 fix round, the
// one read's F6; L9: THE RENDERER FOLLOWS THE RAY).
//
// THE DEFECT IT GUARDS: a traced reflection whose hit only the visibility-buffer decode can shade (a RECORD: a far copy's
// hit, a mover, a rig, a planar or a sharp metal, an uncached hit) was NO SAMPLE when the ray had crossed a pane on its way
// (rq_reflect.comp: "a record's write-back carries no throughput") - whatever stood behind glass in a mirror was never
// sampled. Now the record carries the crossed panes' own light and throughput (jah_rq_hit_record.glsl words 6-7) and the
// write-back adds the panes' light plus the throughput x the decoded light (rq_hit_composite.comp) - the gather's rule.
// THE FIXTURE (rtreflect.far_record's): a mirror (metal, roughness 0) 3 m in front of the camera; behind the camera, 93 m
// along the reflected rays, a 20 m block lit from +X - past the 50 m near length, so the far copies answer and the decode
// shades it (a record); a 30 x 30 m glass slab (Glass at alpha 0.5: T = 1024/2047 as the pane word stores it, albedo 0)
// 2 m behind the camera, across every reflected ray. FOUR ARMS, each at rest, from the HDR readback (linear radiance):
// the slab hidden or shown, the block shown or hidden.
// THE LAW: over the block's face (where the slab-less pictures with and without the block differ by over a quarter of
// their largest difference, eroded by 3 px), the slab passes T of what the block adds - the slab's own light and the sky
// behind the block are the same in the two slabbed arms, so they cancel: mean(slab, block) - mean(slab, no block) =
// T x (mean(block) - mean(no block)) within ONE CODE at that level (the display's sRGB code width there) plus the
// halves the record carries the panes in (2^-11 of the level).
// 256x144; frames 1 + 4 x 30 = 121.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
    } while (0)

static const unsigned kWidth = 256, kHeight = 144;

static NodeId addBox(Scene *s, MeshId cube, MaterialId mat, const Vec3 &at, const Vec3 &size)
{
    const NodeId n = s->createNode();
    if (!n || !s->attachMesh(n, cube, mat)) return 0;
    enginetest::setNodeScale(s, n, size);
    enginetest::setNodePosition(s, n, at);
    return n;
}
static double srgbEncode(double L) { return L <= 0.0031308 ? 12.92 * L : 1.055 * std::pow(L, 1.0 / 2.4) - 0.055; }
static double srgbDecode(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
static double oneCodeAt(double L)
{
    const double code = std::min(std::floor(srgbEncode(std::max(L, 0.0)) * 255.0), 254.0);
    return srgbDecode((code + 1.0) / 255.0) - srgbDecode(code / 255.0);
}
static double luma(const ImageF &img, unsigned x, unsigned y)
{
    const Colour c = img.at(x, y);
    return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
}

int main()
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-pane-record-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    int frames = 1;
    View *view = e->createOffscreenView("panerecord", kWidth, kHeight, Colour(0.30f, 0.40f, 0.55f),
                                        OffscreenContract::Live);
    Scene *s = e->createScene("panerecord");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("SKIP: no ray-query device here - the reflection's records are the ray tier's\n");
        return 77;
    }
    s->setAmbient(Colour(0.30f, 0.40f, 0.55f), Colour(0.02f, 0.02f, 0.02f));
    enginetest::addDirectionalLight(s, Vec3(-1.0f, -0.15f, 0.6f), 3.0f);   // from +X, onto the block's face the mirror sees
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    PbrParams mp;
    mp.albedo = Colour(0.95f, 0.95f, 0.95f);
    mp.metalness = 1.0f;
    mp.roughness = 0.0f;
    const NodeId mirror = addBox(s, cube, s->createPbrMaterial(mp), Vec3(0.0f, 0.0f, -3.0f), Vec3(14.0f, 10.0f, 0.02f));
    PbrParams sp;
    sp.albedo = Colour(0.85f, 0.85f, 0.85f);
    sp.roughness = 0.9f;
    PbrParams gp;
    gp.albedo = Colour(0.0f, 0.0f, 0.0f);
    gp.roughness = 0.5f;
    gp.alphaMode = PbrAlphaMode::Glass;
    gp.alpha = 0.5f;
    const NodeId slab = addBox(s, cube, s->createPbrMaterial(gp), Vec3(0.0f, 0.0f, 2.0f), Vec3(30.0f, 30.0f, 0.02f));
    const NodeId block = addBox(s, cube, s->createPbrMaterial(sp), Vec3(0.0f, 0.0f, 95.0f), Vec3(20.0f, 20.0f, 20.0f));
    CHECK(mirror && block && slab, "the mirror, the block and the slab exist");
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 16.0f, 64, 0.0f };
    CHECK(s->setGlobalIllumination(gi), "GI builds (one 16 m cascade: a 50 m near length)");
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.hdrReadback = true;
    fx.ssr = 2;
    fx.reflectSources = ReflectSources::Rays;
    view->setPostFx(fx);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -3.0f));

    // THE FOUR ARMS: [slab][block]
    ImageF img[2][2];
    bool read = true;
    for (int withSlab = 0; withSlab < 2; ++withSlab)
        for (int withBlock = 0; withBlock < 2; ++withBlock) {
            s->setNodeVisible(slab, withSlab != 0);
            s->setNodeVisible(block, withBlock != 0);
            e->waitForAsyncShaders();
            enginetest::frames(e, 30);
            frames += 30;
            read = view->readPixelsHdr(img[withSlab][withBlock]) && img[withSlab][withBlock].width == kWidth &&
                   img[withSlab][withBlock].height == kHeight && read;
        }
    CHECK(read, "the four pictures read back");
    if (failures) { std::printf("rtreflect.pane_record: FAIL (%d frames)\n", frames); return 1; }
    // THE DISK: where the slab-less pictures differ by over a quarter of their largest difference, eroded by 3 px
    std::vector<double> diff(size_t(kWidth) * kHeight);
    double maxDiff = 0.0;
    for (unsigned y = 0; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) {
            const double d = std::fabs(luma(img[0][1], x, y) - luma(img[0][0], x, y));
            diff[size_t(y) * kWidth + x] = d;
            maxDiff = std::max(maxDiff, d);
        }
    std::vector<unsigned> px;
    for (unsigned y = 3; y + 3 < kHeight; ++y)
        for (unsigned x = 3; x + 3 < kWidth; ++x) {
            bool in = true;
            for (int dy = -3; dy <= 3 && in; ++dy)
                for (int dx = -3; dx <= 3 && in; ++dx)
                    in = diff[size_t(int(y) + dy) * kWidth + size_t(int(x) + dx)] > 0.25 * maxDiff;
            if (in) px.push_back(y * kWidth + x);
        }
    CHECK(px.size() > 500u, "the block's face holds %zu pixels (its largest difference %.6f)", px.size(), maxDiff);
    const auto mean = [&](const ImageF &im) {
        double sum = 0.0;
        for (unsigned i : px) sum += luma(im, i % kWidth, i / kWidth);
        return px.empty() ? 0.0 : sum / double(px.size());
    };
    const double hole = mean(img[0][1]) - mean(img[0][0]), through = mean(img[1][1]) - mean(img[1][0]);
    const double T = 1024.0 / 2047.0, want = T * hole;
    const double bar = oneCodeAt(want) + want / 2048.0;
    std::printf("== the block's light in the mirror: %.6f without the slab, %.6f through it; T x %.6f (bar %.6f); the "
                "slab's own light and the sky behind %.6f\n", hole, through, want, bar, mean(img[1][0]));
    CHECK(hole > 0.01, "the mirror shows the block (a record: the far copies answer past the near length)");
    CHECK(std::fabs(through - want) <= bar,
          "(F6) THROUGH THE SLAB the mirror shows T of the block's light (%.6f against %.6f, within %.6f)", through, want,
          bar);
    std::printf("rtreflect.pane_record: %s (%d failures, %d frames)\n", failures ? "FAIL" : "PASS", failures, frames);
    return failures ? 1 : 0;
}
