// A MESH WITH MORE THAN EIGHT LOD LEVELS SHADES RIGHT AT RAY HITS — `gi.ray_levels`.
//
// THE DEFECT (architecture audit 2026-09-30, D1): the GPU scene keeps EIGHT levels a
// mesh (GpuScene::kLevelsPerMesh — the level table, the geometry rows, the hit
// record's 3-bit level), but the ray tier picked a near and a far level up to
// kTlasLevels - 1 = 15. A near level >= 8 had no geometry row (the hit decoded
// against level 0's), a far copy past 8 indexed the NEXT mesh's rows, and the hit
// record's level wrapped. Silent: no suite had such a mesh.
//
// THE FIX: the ray tier traces a mesh at no level past the GPU scene's last
// (OgreRayQuery.cpp kRayLevels) — a deeper chain is traced at level 7 at most.
//
// THE DISCRIMINATOR: a sphere whose every level is THE SAME TRIANGLES IN A DIFFERENT
// ORDER (level i rotates the triangle list by i x 97). Any level shades the same
// surface — unless a hit's primitive index is decoded against another level's rows,
// which lands on another triangle. The DEEP sphere (12 levels) and the CONTROL (8
// levels, the same construction) are reflected by a mirror floor, rays only (no
// march), their measured bounds tiny so the ray tier asks for the coarsest level:
// the two pictures must agree.
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

static const unsigned kWidth = 640, kHeight = 360;
static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

static MeshData permutedSphere(int levels, int rings = 24, int segments = 48)
{
    MeshData d;
    const float kPi = 3.14159265358979f;
    for (int r = 0; r <= rings; ++r) {
        const float th = float(r) / float(rings) * kPi;
        for (int sg = 0; sg < segments; ++sg) {
            const float ph = float(sg) / float(segments) * 2.0f * kPi;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { 0.5f * x, 0.5f * y, 0.5f * z });
            d.normals.insert(d.normals.end(), { x, y, z });
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int sg = 0; sg < segments; ++sg) {
            const unsigned a = unsigned(r * segments + sg), b = unsigned(r * segments + (sg + 1) % segments);
            const unsigned c = a + unsigned(segments), e = b + unsigned(segments);
            d.indices.insert(d.indices.end(), { a, b, c, b, e, c });
        }
    const size_t tris = d.indices.size() / 3u;
    for (int l = 1; l < levels; ++l) {
        std::vector<unsigned> lv(d.indices.size());
        const size_t shift = (size_t(l) * 97u) % tris;
        for (size_t t = 0; t < tris; ++t)
            for (int k = 0; k < 3; ++k) lv[t * 3u + size_t(k)] = d.indices[((t + shift) % tris) * 3u + size_t(k)];
        d.lodIndices.push_back(lv);
        d.lodBounds.push_back(1e-6f * float(l));
        d.lodErrors.push_back(1e-6f * float(l));
    }
    return d;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-ray-levels-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("raylevels", kWidth, kHeight, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("raylevels");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    if (!(e->rayQueryAvailable() && e->rayTracing())) {
        std::printf("ok: no ray queries on this machine — gi.ray_levels is about the tier; skipping\n");
        return 0;
    }
    s->setAmbient(Colour(0.45f, 0.55f, 0.70f), Colour(0.30f, 0.30f, 0.32f));
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    const NodeId floor = s->createNode();
    PbrParams fp; fp.albedo = Colour(0.9f, 0.9f, 0.9f); fp.metalness = 1.0f; fp.roughness = 0.02f;
    if (!(floor && s->attachMesh(floor, cube, s->createPbrMaterial(fp)))) { std::printf("FAIL: floor\n"); return 1; }
    enginetest::setNodeScale(s, floor, Vec3(30.0f, 0.2f, 30.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));

    const MeshId deep = s->createMesh(permutedSphere(12));
    const MeshId control = s->createMesh(permutedSphere(8));
    // A neighbour in the mesh table after both, so a row read past the deep mesh's
    // block lands on real (different) geometry rather than on zeros.
    const MeshId neighbour = s->createMesh(enginetest::unitCubeMesh());
    PbrParams sp; sp.albedo = Colour(1.0f, 0.45f, 0.1f); sp.roughness = 0.35f;
    const MaterialId sm = s->createPbrMaterial(sp);
    const NodeId ball = s->createNode(), other = s->createNode();
    // A MOVER: a hit on it is always DECODED from its rows (jahHitAlwaysDecodes), the
    // read the cap is about; a still item's hit may be shaded from the volume alone.
    s->setNodeMovable(ball, true);
    CHECK_MSG(deep && control && neighbour && ball && other && s->attachMesh(ball, deep, sm) &&
                  s->attachMesh(other, neighbour, sm),
              "a 12-level sphere, an 8-level twin and a neighbour mesh");
    enginetest::setNodeScale(s, ball, Vec3(2.0f, 2.0f, 2.0f));
    enginetest::setNodePosition(s, ball, Vec3(0.0f, 1.2f, -2.0f));
    enginetest::setNodePosition(s, other, Vec3(0.0f, -40.0f, 0.0f));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 2.0f);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 20.0f, 128, 0.0f };
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: gi\n"); return 1; }
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    fx.ssrScreenMarch = false;   // the rays alone: every reflected pixel is a hit decode
    view->setPostFx(fx);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.0f, 6.0f), Vec3(0.0f, 0.2f, 0.0f));

    Image picDeep, picControl;
    render(e, 120);
    view->readPixels(picDeep);
    const RayQueryStatus rqDeep = s->rayQueryStatus();
    s->attachMesh(ball, control, sm);
    render(e, 120);
    view->readPixels(picControl);
    Image picControl2;
    render(e, 120);
    view->readPixels(picControl2);
    // The floor's half of the image (the reflection): mean and worst per-pixel worst channel.
    auto diff = [&](const Image &a, const Image &b, int &worst) {
        double sum = 0.0;
        int n = 0;
        worst = 0;
        for (unsigned y = kHeight / 2u; y < kHeight; ++y)
            for (unsigned x = 0; x < kWidth; ++x) {
                const size_t p = (size_t(y) * kWidth + x) * 4u;
                int d = 0;
                for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(int(a.rgba[p + c]) - int(b.rgba[p + c])));
                sum += d;
                worst = std::max(worst, d);
                ++n;
            }
        return n ? sum / n : 0.0;
    };
    int worst = 0, worstCtl = 0;
    const double mean = diff(picDeep, picControl, worst);
    const double noise = diff(picControl, picControl2, worstCtl);
    std::printf("RESULT the reflection of a 12-level sphere vs its 8-level twin: mean %.3f codes, worst %d; "
                "the twin against itself 120 frames on: mean %.3f, worst %d (level BLASes %d, instances %d)\n",
                mean, worst, noise, worstCtl, rqDeep.levelBlasCount, rqDeep.instances);
    CHECK_MSG(mean <= noise + 0.25, "a mesh deeper than the GPU scene's eight levels shades at ray hits as its "
                                    "eight-level twin does (mean %.3f codes over the floor; bar the twin's own "
                                    "%.3f + 0.25; worst %d)", mean, noise, worst);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
