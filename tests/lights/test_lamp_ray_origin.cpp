// lights.lamp_ray_origin — A LAMP's VISIBILITY RAY NEVER HITS THE SURFACE IT STARTS ON (GATHER-HANG-1 stage 3).
//
// THE DEFECT (spikes/black-specks/DIAGNOSIS.md): the Forward+ lamp's visibility ray (jah_lamp_ray.glsl) left a drawn
// surface lifted by its float bound alone (~4e-6 m at 8 m), while the ray-triangle test on a triangle whose corners lie
// tens of metres away rounds at that triangle's scale: a self-hit at t ~ 1e-6 answered "occluded", the lamp's whole term
// 0, and with no other light the pixel stored exactly +0 - black specks in a near-white highlight.
// THE LAW (the spec's stage 3): every ray that starts on a drawn surface is lifted by the ONE origin law - the point's
// own error plus the drawn surface's bound (jah_ray_origin.glsl) - so no pixel of a lamp's lit footprint on an open
// plane reads exactly 0.
//
// THE FIXTURE (ours; the post chain hdr.bloom_isotropic's bloom-off picture): a matte plane of TWO triangles facing +z,
// a point light 0.3 m in front of it that casts no shadow, the camera 8 m back on the light's axis at 960 x 540, no
// ambient, GI off. Its lit footprint: every pixel within kFootprintPx of the centre (0.0123 m a pixel on the plane, so
// ~1.1 m; the light reaches it at >= 1/1.3 of its 1-m intensity) - a speck is a footprint pixel whose three channels
// read exactly 0.
// THE ARMS, ONE PROCESS, each its own picture after its frames:
//   (1) the ray tier on, an 80 m plane, the Atom draw path - 0 specks (the base: 24)
//   (2) the ray tier on, a 200 m plane (the Basic template's floor is 100 m) - 0 specks
//   (3) the ray tier on, the STOCK draw path (the Atom door off), the 80 m and the 200 m plane - 0 specks
//   (4) NEGATIVE CONTROL: the ray tier off (no lamp ray) - 0 specks, and every footprint pixel lit (the footprint is
//       the light's: a 0 there is a speck, never the scene's own dark)
//   (5) NEGATIVE CONTROL: the 80 m plane cut into 8 x 8 quads of 10 m (the same picture by physics; the test's rounding
//       at a 10 m triangle's scale) - 0 specks
// Plus the route: the scene traces on this machine (rayTracingResolved) in the ray arms.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                           std::printf("\n"); ++failures; } \
                           else { std::printf("  ok: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {

constexpr unsigned kW = 960, kH = 540;
constexpr int kFootprintPx = 90;
constexpr int kFrames = 8;   // a mesh or a door changed: the structures rebuilt, the chain re-made, the picture settled

/// A plane at z = 0 facing +z, `half` metres each way, cut into n x n quads (two triangles each).
MeshData planeMesh(float half, int n)
{
    MeshData d;
    const float step = 2.0f * half / float(n);
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            d.positions.insert(d.positions.end(), { -half + step * float(i), -half + step * float(j), 0.0f });
            d.normals.insert(d.normals.end(), { 0.0f, 0.0f, 1.0f });
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const unsigned a = unsigned(j * (n + 1) + i), b = a + 1u, c = a + unsigned(n + 1) + 1u, e = c - 1u;
            d.indices.insert(d.indices.end(), { a, b, c, a, c, e });
        }
    return d;
}

int code(const Image &img, int x, int y)
{
    const size_t i = (size_t(y) * img.width + size_t(x)) * 4;
    return std::max({ int(img.rgba[i]), int(img.rgba[i + 1]), int(img.rgba[i + 2]) });
}

struct Footprint { int specks = 0, darkest = 255, pixels = 0; };

Footprint footprint(const Image &img)
{
    Footprint f;
    const int cx = int(kW / 2), cy = int(kH / 2);
    for (int y = cy - kFootprintPx; y < cy + kFootprintPx; ++y)
        for (int x = cx - kFootprintPx; x < cx + kFootprintPx; ++x) {
            const int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > kFootprintPx * kFootprintPx) continue;
            const int c = code(img, x, y);
            ++f.pixels;
            if (c == 0) ++f.specks;
            else f.darkest = std::min(f.darkest, c);
        }
    return f;
}

}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-lamp-ray-origin-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    View *view = engine->createOffscreenView("lampray", kW, kH, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *s = engine->createScene("lampray");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    view->setShadows(false);

    PbrParams p;
    p.albedo = Colour(0.5f, 0.5f, 0.5f);
    p.metalness = 0.0f;
    p.roughness = 1.0f;
    const MaterialId mat = s->createPbrMaterial(p);
    const NodeId plane = s->createNode();
    const MeshId plane80 = s->createMesh(planeMesh(40.0f, 1));
    const MeshId plane200 = s->createMesh(planeMesh(100.0f, 1));
    const MeshId quads80 = s->createMesh(planeMesh(40.0f, 8));
    if (!mat || !plane || !plane80 || !plane200 || !quads80 || !s->attachMesh(plane, plane80, mat)) {
        std::printf("FAIL: fixture plane: %s\n", engine->lastError().c_str());
        return 1;
    }
    {
        const NodeId n = s->createNode();
        s->setNodeTransform(n, Vec3{ 0.0f, 0.0f, 0.3f }, Quat{}, Vec3{ 1, 1, 1 });
        LightDesc l;
        l.type = LightType::Point;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 8.0f;
        l.range = 200.0f;
        l.castShadows = false;
        if (!s->setLight(n, l)) { std::printf("FAIL: fixture light\n"); return 1; }
    }
    enginetest::testCameraAt(view, Vec3(0.0f, 0.0f, 8.0f));
    {
        PostFxDesc fx;   // hdr.bloom_isotropic's chain, its bloom-off picture (amount 0)
        fx.allowOffscreen = true;
        fx.hdr = true;
        fx.tonemapFixed = true;
        fx.exposure = 0.0f;
        fx.ditherOff = true;
        fx.bloom = true;
        fx.bloomAmount = 0.0f;
        view->setPostFx(fx);
    }

    const auto shoot = [&](const char *arm, Footprint &out) {
        enginetest::frames(engine.get(), kFrames);
        Image img;
        if (!view->readPixels(img)) { std::printf("FAIL: readPixels (%s)\n", arm); return false; }
        out = footprint(img);
        std::printf("== %s: %d specks (exactly 0) of %d footprint pixels, the darkest lit %d, centre %d\n", arm, out.specks,
                    out.pixels, out.darkest, code(img, int(kW / 2), int(kH / 2)));
        return true;
    };
    const auto remesh = [&](MeshId m) { return s->detachMesh(plane) && s->attachMesh(plane, m, mat); };

    Footprint a1, a2, a3, a3b, a4, a5;
    if (!shoot("(1) rays on, 80 m plane, Atom", a1)) return 1;
    const bool traced = s->rayTracingResolved();
    if (!remesh(plane200) || !shoot("(2) rays on, 200 m plane, Atom", a2)) return 1;
    s->setAtomDrawEnabled(false);
    if (!shoot("(3) rays on, 200 m plane, stock", a3b)) return 1;
    if (!remesh(plane80) || !shoot("(3) rays on, 80 m plane, stock", a3)) return 1;
    s->setAtomDrawEnabled(true);
    s->setRayTracing(RayTracingMode::Off);
    if (!shoot("(4) rays off, 80 m plane", a4)) return 1;
    s->setRayTracing(RayTracingMode::Auto);
    if (!remesh(quads80) || !shoot("(5) rays on, 80 m plane in 10 m quads", a5)) return 1;

    CHECK(traced, "the route: the scene traces on this machine (rayTracingResolved) - the lamp ray runs");
    CHECK(a4.specks == 0 && a4.darkest >= 64,
          "(4) control, rays off: the footprint is lit (darkest %d >= 64) and holds no exactly-0 pixel (%d)", a4.darkest,
          a4.specks);
    CHECK(a5.specks == 0, "(5) control, 10 m quads: no exactly-0 pixel (%d)", a5.specks);
    CHECK(a1.specks == 0, "(1) THE LAW, 80 m plane, Atom: no lamp ray hits the plane it starts on - %d specks", a1.specks);
    CHECK(a2.specks == 0, "(2) THE LAW, 200 m plane, Atom: %d specks", a2.specks);
    CHECK(a3.specks == 0, "(3) THE LAW, 80 m plane, the stock path: %d specks", a3.specks);
    CHECK(a3b.specks == 0, "(3) THE LAW, 200 m plane, the stock path: %d specks", a3b.specks);

    std::printf(failures ? "lights.lamp_ray_origin: FAILED (%d)\n" : "lights.lamp_ray_origin: PASSED\n", failures);
    return failures ? 1 : 0;
}
