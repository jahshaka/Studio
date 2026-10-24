// rtreflect.tilted_normal — A SHADING NORMAL FAR FROM THE SURFACE NEVER VOIDS THE FOLD (PHOTON-7 stage 6, READ_B finding 6).
//
// THE LAW: a reflected ray leaves the surface the depth buffer DREW, so its direction is held above that surface's
// GEOMETRIC plane (the W4 fold) and its origin lifted along it - whatever the shading normal says (a normal map, smoothed
// vertex normals over a coarse cut). The geometric normal is the depth's own reconstruction (jah_geometric_normal.glsl)
// and it falls back to the shading normal only where the reconstruction FAILS (no plane continues through the pixel) -
// never on an angle: a correct geometric normal more than 60 degrees from the shading one was discarded, the fold could
// never fire, and a lobe pointing under the true plane hit its own surface at t ~ 0.
// THE FIXTURE: a mirror floor (metal, roughness 0: the lobe's centre ray) seen 30 degrees down, before a large emissive
// green wall; two arms - its vertex normals straight up (the control: the mirror direction rises to the wall) and tilted
// 70 degrees sideways (the shading's mirror direction points 22 degrees UNDER the floor; folded about the drawn plane it
// rises to the wall too). Each arm's floor must reflect the wall: green over red and blue, at least half the control's.
// Frames: 1 + 2 x 30 = 61.
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

/// A FLOOR GRID (y = 0, +-10 m, 8 x 8 quads) whose every vertex normal is `n`.
static MeshData floorGrid(float nx, float ny, float nz)
{
    MeshData d;
    const int k = 8;
    const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
    for (int i = 0; i <= k; ++i)
        for (int j = 0; j <= k; ++j) {
            d.positions.insert(d.positions.end(), { -10.0f + 20.0f * float(j) / k, 0.0f, -10.0f + 20.0f * float(i) / k });
            d.normals.insert(d.normals.end(), { nx / l, ny / l, nz / l });
            d.uvs.insert(d.uvs.end(), { float(j) / k, float(i) / k });
        }
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < k; ++j) {
            const unsigned a = unsigned(i * (k + 1) + j), b = a + 1u, c = a + unsigned(k + 1), e = c + 1u;
            d.indices.insert(d.indices.end(), { a, c, b, b, c, e });   // facing +Y
        }
    return d;
}

/// The floor's mean green over red and blue in the image's lower middle (the floor, reflecting).
static double greenExcess(const Image &img)
{
    double s = 0.0;
    int n = 0;
    for (unsigned y = kHeight * 2 / 3; y < kHeight - 4; ++y)
        for (unsigned x = kWidth / 3; x < kWidth * 2 / 3; ++x) {
            const size_t i = (size_t(y) * img.width + x) * 4u;
            s += double(img.rgba[i + 1]) - std::max(double(img.rgba[i]), double(img.rgba[i + 2]));
            ++n;
        }
    return n ? s / n : 0.0;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-tilted-normal-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("tiltednormal", kWidth, kHeight, Colour(0.10f, 0.10f, 0.12f));
    Scene *s = e->createScene("tiltednormal");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("SKIP: no ray-query device here - the reflection's origin is the ray tier's\n");
        return 77;
    }
    s->setAmbient(Colour(0.10f, 0.10f, 0.12f), Colour(0.05f, 0.05f, 0.05f));
    PbrParams fp;
    fp.albedo = Colour(0.95f, 0.95f, 0.95f);
    fp.metalness = 1.0f;
    fp.roughness = 0.0f;
    const MaterialId floorMat = s->createPbrMaterial(fp);
    const NodeId flat = s->createNode(), tilted = s->createNode();
    // tilted 70 degrees from +Y toward +X
    const float t = 70.0f * 3.14159265f / 180.0f;
    CHECK(flat && tilted && s->attachMesh(flat, s->createMesh(floorGrid(0.0f, 1.0f, 0.0f)), floorMat) &&
          s->attachMesh(tilted, s->createMesh(floorGrid(std::sin(t), std::cos(t), 0.0f)), floorMat),
          "the two floors exist");
    PbrParams wp;
    wp.albedo = Colour(0.0f, 0.0f, 0.0f);
    wp.emissive = Colour(0.1f, 0.9f, 0.1f);
    wp.roughness = 1.0f;
    const NodeId wall = s->createNode();
    CHECK(wall && s->attachMesh(wall, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(wp)),
          "the green wall exists");
    enginetest::setNodeScale(s, wall, Vec3(60.0f, 30.0f, 0.5f));
    enginetest::setNodePosition(s, wall, Vec3(0.0f, 10.0f, -12.0f));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;   // (the bounces by feedback: PHOTON-7 stage 4 deleted the count)
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 32.0f, 64, 0.0f };
    CHECK(s->setGlobalIllumination(gi), "GI builds");
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    fx.reflectSources = ReflectSources::Rays;
    view->setPostFx(fx);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 3.464f), Vec3(0.0f, 0.0f, 0.0f));   // 30 degrees down
    double excess[2] = {};
    for (int arm = 0; arm < 2; ++arm) {
        s->setNodeVisible(flat, arm == 0);
        s->setNodeVisible(tilted, arm == 1);
        enginetest::frames(e, 30);
        Image img;
        CHECK(view->readPixels(img) && img.width == kWidth, "arm %d reads back", arm);
        excess[arm] = greenExcess(img);
        std::printf("   the %s floor reflects the wall: green over red and blue %.1f codes\n",
                    arm ? "TILTED (70 deg shading)" : "flat (control)", excess[arm]);
    }
    CHECK(excess[0] > 20.0, "the control's mirror floor reflects the green wall (%.1f codes)", excess[0]);
    CHECK(excess[1] >= 0.5 * excess[0],
          "the tilted floor's lobe is folded above the drawn plane and reflects the wall too (%.1f >= half of %.1f)",
          excess[1], excess[0]);
    std::printf("%s\n", failures ? "rtreflect.tilted_normal: FAIL" : "rtreflect.tilted_normal: PASS");
    return failures ? 1 : 0;
}
