// rtreflect.odd_size — EVERY PIXEL OF THE REFLECTION IS WRITTEN EVERY FRAME (PHOTON-7 stage 6 commit 7; AB section 3 #19).
//
// THE LAW: the spatial filter writes every full-resolution pixel of `jahSsrReflection` each frame - at half resolution an
// odd-sized target's last column and last row have no trace texel of their own (the trace is floor( size / 2 )), and
// the joint-bilateral upsample answers them from the texels beside them; nothing of the resolve's scratch (its trace
// texels hold a hit distance in w) and nothing of an older frame survives into the picture.
// THE FIXTURE: an odd 257 x 145 view at High (half-resolution rays: 128 x 72 trace texels), a glossy metal floor
// (perceptual 0.2) reflecting a wide emissive wall. Over the floor's reflection, the remainder column against its
// neighbour must step no more than the neighbour pair beside it does (+ 2 codes), and the same for the remainder row; a
// remainder left to the probe reads the floor without the wall's reflection. Frames: 1 + 90 = 91.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
    } while (0)

static const unsigned kWidth = 257, kHeight = 145;

static float px(const Image &img, unsigned x, unsigned y, int c)
{
    return float(img.rgba[(size_t(y) * img.width + x) * 4u + unsigned(c)]);
}
/// The mean absolute step between two columns (or rows) over a span of the other axis, all channels.
static double step(const Image &img, bool columns, unsigned a, unsigned b, unsigned from, unsigned to)
{
    double s = 0.0;
    int n = 0;
    for (unsigned k = from; k < to; ++k)
        for (int c = 0; c < 3; ++c) {
            s += std::fabs(columns ? px(img, a, k, c) - px(img, b, k, c) : px(img, k, a, c) - px(img, k, b, c));
            ++n;
        }
    return n ? s / n : 0.0;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-odd-size-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("oddsize", kWidth, kHeight, Colour(0.05f, 0.05f, 0.06f), jahshaka::engine::OffscreenContract::Live);
    Scene *s = e->createScene("oddsize");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("SKIP: no ray-query device here - the reflection's write is the ray tier's\n");
        return 77;
    }
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.03f, 0.03f, 0.03f));
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    PbrParams fp;
    fp.albedo = Colour(0.9f, 0.9f, 0.9f);
    fp.metalness = 1.0f;
    fp.roughness = 0.2f;
    PbrParams wp;
    wp.albedo = Colour(0.0f, 0.0f, 0.0f);
    wp.emissive = Colour(0.9f, 0.6f, 0.2f);
    wp.roughness = 1.0f;
    const NodeId floor = s->createNode(), wall = s->createNode();
    CHECK(floor && wall && s->attachMesh(floor, cube, s->createPbrMaterial(fp)) &&
          s->attachMesh(wall, cube, s->createPbrMaterial(wp)), "the floor and the wall exist");
    enginetest::setNodeScale(s, floor, Vec3(40.0f, 0.2f, 40.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    enginetest::setNodeScale(s, wall, Vec3(40.0f, 6.0f, 0.5f));
    enginetest::setNodePosition(s, wall, Vec3(0.0f, 3.0f, -8.0f));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;   // (the bounces by feedback: PHOTON-7 stage 4 deleted the count)
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 32.0f, 64, 0.0f };
    CHECK(s->setGlobalIllumination(gi), "GI builds");
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 1;                                       // High: half-resolution rays
    view->setPostFx(fx);
    // looking down the floor toward the wall, its reflection under the floor's horizon and to the frame's right edge
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 6.0f), Vec3(0.0f, 0.4f, -4.0f));
    enginetest::frames(e, 90);
    Image img;
    CHECK(view->readPixels(img) && img.width == kWidth && img.height == kHeight, "the picture reads back");
    if (failures) { std::printf("rtreflect.odd_size: FAIL\n"); return 1; }
    // THE FLOOR'S REFLECTION: the lower half of the frame (the wall's reflection is the bright band under the horizon).
    const unsigned y0 = kHeight / 2 + 4, y1 = kHeight - 2;
    const double colRem = step(img, true, kWidth - 1, kWidth - 2, y0, y1);
    const double colPair = step(img, true, kWidth - 2, kWidth - 3, y0, y1);
    const double rowRem = step(img, false, kHeight - 1, kHeight - 2, kWidth / 4, kWidth * 3 / 4);
    const double rowPair = step(img, false, kHeight - 2, kHeight - 3, kWidth / 4, kWidth * 3 / 4);
    double lum = 0.0;
    for (unsigned y = y0; y < y1; ++y) lum += (px(img, kWidth - 2, y, 0) + px(img, kWidth - 2, y, 1)) / 2.0;
    lum /= double(y1 - y0);
    std::printf("   the remainder column steps %.2f codes from its neighbour (the neighbour pair %.2f); the remainder row "
                "%.2f (pair %.2f); the floor's reflection beside the remainder %.1f codes\n",
                colRem, colPair, rowRem, rowPair, lum);
    CHECK(lum > 20.0, "the floor beside the remainder reflects the wall (%.1f codes)", lum);
    CHECK(colRem <= colPair + 2.0, "the odd remainder COLUMN is written like its neighbours (%.2f against %.2f + 2)",
          colRem, colPair);
    CHECK(rowRem <= rowPair + 2.0, "the odd remainder ROW is written like its neighbours (%.2f against %.2f + 2)", rowRem,
          rowPair);
    std::printf("%s\n", failures ? "rtreflect.odd_size: FAIL" : "rtreflect.odd_size: PASS");
    return failures ? 1 : 0;
}
