// A CUT-OUT IS GEOMETRY WITH HOLES, FOR THE RAYS TOO (REFLECT-MOVERS-2) —
// `gi.rt_alpha_tested`.
//
// THE GAP: an alpha-tested item (a fence, foliage: PbrAlphaMode::Cutout) used to
// be OUT of the ray tier's traced set — every bottom-level structure carried
// VK_GEOMETRY_OPAQUE_BIT_KHR and every ray gl_RayFlagsOpaqueEXT, so a cut-out
// would have intersected as a solid quad (audit C-16), and the answer was to
// leave it out: no ray shadow, no ray reflection, no card sun term. Since this
// lane its structure is NON-opaque and every ray query runs the candidate loop
// (jah_rq_alpha.glsl): the candidate triangle's UV from the geometry rows, the
// item's mask, the cutoff — confirmed only where the texture is solid.
//
// THE FIXTURE: a 2 m x 2 m FENCE (a two-sided quad, z = 0, x in [-1, 1], y in
// [0, 2]) whose albedo mask is eight vertical bars, half of them clear — 50 %
// coverage — standing on a carded floor, the sun behind it at 45 degrees, so
// its shadow falls on the floor at z in [-2, 0]. Arms, each against the same
// fence OPAQUE (the whole quad) and with NO fence:
//   1. THE RAY SHADOW: rays from a grid of floor points in the footprint towards
//      the sun against the casters (Scene::traceRays, the sun-contact mask): the
//      covered fraction must be the mask's 50 % +- 5 %, never the quad's 100 %.
//   2. THE CARD'S SUN TERM (rq_card_movers.comp's still mode, one ray a texel):
//      the floor's card texels in the footprint, read back (readCardAt) — the
//      shadowed fraction the same 50 % +- 5 %.
//   3. THE RAY REFLECTION: a mirror floor with the screen march off (rays alone,
//      a headset's chain): the pixels of the fence's reflection classified
//      against the no-fence and the opaque-fence pictures — the fence's share
//      the same 50 % +- 5 % (bars are resolved: every pixel reads one or the
//      other, few read between).
//   4. THE PICTURE's shadow (raster + the sun-contact ray, min of the two):
//      printed beside the ray's, the raster's own being the reference.
// `--cost` (not a gated row; under scripts/gpu-exclusive.sh with the clocks
// locked): a 10,000-cube world at 1920x1080 — the reflection, the gather's trace
// and the sun contact GPU ms — with ONE cut-out fence in it (the scene has an
// alpha table, so every ray query asks without the opaque flag and the loop is
// live) against the arm "reflect.alphaTested" = 0 (no table: every ray asks
// opaque, the pre-lane traversal), alternating 30-frame blocks in one process.
// A world with NO alpha-tested item has no table and asks opaque by
// construction — the pre-lane flags exactly.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "EnginePrivate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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

static const unsigned kWidth = 640;
static const unsigned kHeight = 360;
static const unsigned kBars = 8;          // four solid, four clear
static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

/// The fence: one quad in z = 0, x in [-1, 1], y in [0, 2], facing +z, UVs over it.
static MeshData fenceMesh()
{
    MeshData d;
    d.positions = { -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 2.0f, 0.0f, -1.0f, 2.0f, 0.0f };
    d.normals = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
    d.uvs = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f };
    d.indices = { 0, 1, 2, 0, 2, 3 };
    return d;
}

/// A quad in z = 0 over x in [x0, x1], y in [0, 2], UVs over it (the fence's shape).
static MeshData panelMesh(float x0, float x1)
{
    MeshData d = fenceMesh();
    d.positions = { x0, 0.0f, 0.0f, x1, 0.0f, 0.0f, x1, 2.0f, 0.0f, x0, 2.0f, 0.0f };
    return d;
}

/// A HORIZONTAL cut-out layer at height y over [-h, h]^2, the bars tiled `tiles` times.
static MeshData layerMesh(float y, float h, float tiles)
{
    MeshData d;
    d.positions = { -h, y, h, h, y, h, h, y, -h, -h, y, -h };
    d.normals = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
    d.uvs = { 0.0f, tiles, tiles, tiles, tiles, 0.0f, 0.0f, 0.0f };
    d.indices = { 0, 1, 2, 0, 2, 3 };
    return d;
}

/// Eight vertical bars across u, the even ones solid (alpha 255), the odd ones clear.
static TextureId barsTexture(Scene *s)
{
    const unsigned n = 64;
    std::vector<unsigned char> rgba(size_t(n) * n * 4u);
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x) {
            unsigned char *p = &rgba[(size_t(y) * n + x) * 4u];
            const bool solid = ((x * kBars / n) % 2u) == 0u;
            p[0] = 230; p[1] = 40; p[2] = 30; p[3] = solid ? 255 : 0;
        }
    return s->createTexture(n, n, rgba.data(), true);
}

/// The world point where the ray from floor point `p` towards the sun crosses the
/// fence plane (z = 0) — whether the MASK is solid there, the analytic answer.
static bool maskSolidAbove(float x, float z, float &yAt)
{
    // the sun travels along (0, -1, -1)/sqrt2: towards it is (0, 1, 1)/sqrt2
    const float t = -z;             // z + t = 0
    yAt = t;                        // y rises as fast as z
    if (x < -1.0f || x > 1.0f || yAt < 0.0f || yAt > 2.0f) return false;
    const float u = (x + 1.0f) * 0.5f;
    return (unsigned(u * float(kBars)) % 2u) == 0u;
}

struct Fixture {
    std::unique_ptr<Engine> engine;
    Engine *e = nullptr;
    View *view = nullptr;
    Scene *s = nullptr;
    NodeId floor = 0, fence = 0;
    MaterialId floorMatte = 0, floorMirror = 0, fenceCut = 0, fenceSolid = 0;
};

static std::vector<float> shadowRays(std::vector<std::pair<float, float>> &pts)
{
    std::vector<float> in;
    const float k = 1.0f / std::sqrt(2.0f);
    const unsigned mask = kRayMaskCaster;
    float bits;
    std::memcpy(&bits, &mask, sizeof(bits));
    for (int iz = 0; iz < 40; ++iz)
        for (int ix = 0; ix < 40; ++ix) {
            const float x = -0.975f + 1.95f * (float(ix) + 0.5f) / 40.0f;
            const float z = -1.95f + 1.9f * (float(iz) + 0.5f) / 40.0f;
            pts.push_back({ x, z });
            in.insert(in.end(), { x, 0.002f, z, 0.0f, 0.0f, k, k, 20.0f, bits, 0, 0, 0 });
        }
    return in;
}

/// Arm 1: the covered fraction of the footprint's rays (a hit on the fence's slot).
static float rayCoverage(Scene *s, float &analytic, int &agree)
{
    std::vector<std::pair<float, float>> pts;
    const std::vector<float> in = shadowRays(pts);
    std::vector<float> out;
    if (!s->traceRays(in, out) || out.size() < pts.size() * 4u) return -1.0f;
    int hits = 0, solid = 0;
    agree = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const bool hit = out[i * 4u + 3u] > 0.5f && out[i * 4u] > 0.0f;
        float yAt;
        const bool want = maskSolidAbove(pts[i].first, pts[i].second, yAt);
        hits += hit ? 1 : 0;
        solid += want ? 1 : 0;
        agree += hit == want ? 1 : 0;
    }
    analytic = float(solid) / float(pts.size());
    return float(hits) / float(pts.size());
}

/// The two-submesh arms: the covered fraction of rays whose sun path crosses the
/// fence plane inside x in [x0, x1] (the bars, or the solid panel beside them).
static float rayCoverageX(Scene *s, float x0, float x1, int &agree, int &count, bool bars)
{
    std::vector<float> in;
    std::vector<std::pair<float, float>> pts;
    const float k = 1.0f / std::sqrt(2.0f);
    const unsigned mask = kRayMaskCaster;
    float bits;
    std::memcpy(&bits, &mask, sizeof(bits));
    for (int iz = 0; iz < 40; ++iz)
        for (int ix = 0; ix < 40; ++ix) {
            const float x = x0 + 0.025f * (x1 - x0) + 0.95f * (x1 - x0) * (float(ix) + 0.5f) / 40.0f;
            const float z = -1.95f + 1.9f * (float(iz) + 0.5f) / 40.0f;
            pts.push_back({ x, z });
            in.insert(in.end(), { x, 0.002f, z, 0.0f, 0.0f, k, k, 20.0f, bits, 0, 0, 0 });
        }
    std::vector<float> out;
    count = int(pts.size());
    agree = 0;
    if (!s->traceRays(in, out) || out.size() < pts.size() * 4u) return -1.0f;
    int hits = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const bool hit = out[i * 4u + 3u] > 0.5f && out[i * 4u] > 0.0f;
        float yAt;
        const bool want = bars ? maskSolidAbove(pts[i].first, pts[i].second, yAt) : true;
        hits += hit ? 1 : 0;
        agree += hit == want ? 1 : 0;
    }
    return float(hits) / float(pts.size());
}

/// Arm 2: the floor's card texels in the footprint that are shadowed.
static float cardCoverage(Scene *s, int &read)
{
    int shadowed = 0;
    read = 0;
    for (int iz = 0; iz < 24; ++iz)
        for (int ix = 0; ix < 24; ++ix) {
            const float x = -0.95f + 1.9f * (float(ix) + 0.5f) / 24.0f;
            const float z = -1.9f + 1.8f * (float(iz) + 0.5f) / 24.0f;
            CardSample c;
            if (!s->readCardAt(Vec3(x, 0.0f, z), Vec3(0.0f, 1.0f, 0.0f), c) || !c.ok) continue;
            ++read;
            if (c.shadow < 0.5f) ++shadowed;
        }
    return read ? float(shadowed) / float(read) : -1.0f;
}

/// The pixels of the fence's REFLECTION: the fence's footprint mirrored through
/// the floor (y -> -y), projected through the fixture camera, in a mask.
static const Vec3 kCamPos(0.0f, 1.2f, 5.0f);
static const Vec3 kCamTarget(0.0f, -0.6f, 0.0f);
static bool project(const Vec3 &p, float &px, float &py)
{
    Vec3 f(kCamTarget.x - kCamPos.x, kCamTarget.y - kCamPos.y, kCamTarget.z - kCamPos.z);
    float l = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    f = Vec3(f.x / l, f.y / l, f.z / l);
    Vec3 r(-f.z, 0.0f, f.x);
    l = std::sqrt(r.x * r.x + r.z * r.z);
    r = Vec3(r.x / l, 0.0f, r.z / l);
    const Vec3 u(r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x);
    const Vec3 d(p.x - kCamPos.x, p.y - kCamPos.y, p.z - kCamPos.z);
    const float z = d.x * f.x + d.y * f.y + d.z * f.z;
    if (z <= 0.0f) return false;
    const float t = std::tan(45.0f * 0.5f * 3.14159265f / 180.0f);
    const float aspect = float(kWidth) / float(kHeight);
    px = ((d.x * r.x + d.y * r.y + d.z * r.z) / (z * t * aspect) + 1.0f) * 0.5f * float(kWidth);
    py = (1.0f - (d.x * u.x + d.y * u.y + d.z * u.z) / (z * t)) * 0.5f * float(kHeight);
    return true;
}

/// Arm 3: in the reflected footprint (shrunk 4 px off its edges), each pixel is
/// the fence's when it is nearer the opaque-fence picture than the no-fence one.
static float reflectedCoverage(const Image &cut, const Image &none, const Image &solid, int &pixels,
                               float &contrast)
{
    float x0, y0, x1, y1;
    project(Vec3(-1.0f, -0.1f, 0.0f), x0, y0);      // the reflected fence's top-left...
    project(Vec3(1.0f, -1.9f, 0.0f), x1, y1);       // ...and bottom-right
    int fence = 0;
    pixels = 0;
    double c = 0.0;
    for (int y = int(y0) + 4; y < int(y1) - 4; ++y)
        for (int x = int(x0) + 4; x < int(x1) - 4; ++x) {
            if (x < 0 || y < 0 || x >= int(kWidth) || y >= int(kHeight)) continue;
            const size_t i = (size_t(y) * kWidth + size_t(x)) * 4u;
            double dn = 0.0, ds = 0.0, dsn = 0.0;
            for (int k = 0; k < 3; ++k) {
                dn += std::fabs(double(cut.rgba[i + k]) - double(none.rgba[i + k]));
                ds += std::fabs(double(cut.rgba[i + k]) - double(solid.rgba[i + k]));
                dsn += std::fabs(double(solid.rgba[i + k]) - double(none.rgba[i + k]));
            }
            ++pixels;
            c += dsn;
            if (ds < dn) ++fence;
        }
    contrast = pixels ? float(c / double(pixels) / 3.0) : 0.0f;
    return pixels ? float(fence) / float(pixels) : -1.0f;
}

/// Arm 4: the picture's floor, lit / shadowed, in the footprint, seen from above
/// the fence's side — the fraction of the drop the opaque fence makes.
static float pictureShadow(const Image &cut, const Image &none, const Image &solid)
{
    // the footprint's middle: x in [-0.8, 0.8], z in [-1.6, -0.4] on the floor
    float xa, ya, xb, yb;
    project(Vec3(-0.8f, 0.0f, -1.6f), xa, ya);
    project(Vec3(0.8f, 0.0f, -0.4f), xb, yb);
    double num = 0.0, den = 0.0;
    for (int y = int(std::min(ya, yb)); y < int(std::max(ya, yb)); ++y)
        for (int x = int(std::min(xa, xb)); x < int(std::max(xa, xb)); ++x) {
            if (x < 0 || y < 0 || x >= int(kWidth) || y >= int(kHeight)) continue;
            const size_t i = (size_t(y) * kWidth + size_t(x)) * 4u;
            for (int k = 0; k < 3; ++k) {
                num += double(none.rgba[i + k]) - double(cut.rgba[i + k]);
                den += double(none.rgba[i + k]) - double(solid.rgba[i + k]);
            }
        }
    return den > 1.0 ? float(num / den) : -1.0f;
}

struct Fixture;
static int costMain(Fixture &f);
static int costMainImpl(Engine *e, Scene *s, View *view, MaterialId fenceCut, NodeId floor, MaterialId glossy)
{
    s->setNodeMaterial(floor, glossy);
    // THE 10K WORLD: a 100 x 100 lattice of 0.5 m cubes over a glossy floor, and one fence.
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    PbrParams cp; cp.albedo = Colour(0.6f, 0.6f, 0.62f); cp.metalness = 1.0f; cp.roughness = 0.3f;
    const MaterialId cubeMat = s->createPbrMaterial(cp);
    for (int z = 0; z < 100; ++z)
        for (int x = 0; x < 100; ++x) {
            const NodeId n = s->createNode();
            s->attachMesh(n, cube, cubeMat);
            enginetest::setNodeScale(s, n, Vec3(0.5f, 0.5f, 0.5f));
            enginetest::setNodePosition(s, n, Vec3(float(x) - 49.5f, 0.25f, -float(z) - 3.0f));
        }
    const NodeId fence = s->createNode();
    s->attachMesh(fence, s->createMesh(fenceMesh()), fenceCut);
    enginetest::setNodePosition(s, fence, Vec3(3.0f, 0.0f, 1.0f));
    view->resize(1920, 1080);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, 0.0f, -20.0f));
    SunContactDesc sc = s->sunContact();
    sc.enabled = true;
    s->setSunContact(sc);
    for (int i = 0; i < 240; ++i) e->renderOneFrame();
    // The three GPU readings are the monitor's rows (lane TEST-1): a capture for the arms.
    enginetest::GpuTimingWindow gpuTiming(e);
    double sum[2][3] = {}, n[2][3] = {};
    for (int round = 0; round < 24; ++round)
        for (int arm = 0; arm < 2; ++arm) {
            e->setArm("reflect.alphaTested", arm ? 0.0 : 1.0);
            for (int i = 0; i < 30; ++i) {
                e->renderOneFrame();
                if (i < 10) continue;   // the timestamps come back a few frames late
                const float v[3] = { s->rayQueryStatus().reflectMs, s->giStatus().gather.traceMs,
                                     s->sunContactStatus().gpuMs };
                for (int k = 0; k < 3; ++k)
                    if (v[k] > 0.0f) { sum[arm][k] += v[k]; n[arm][k] += 1.0; }
            }
        }
    e->setArm("reflect.alphaTested", 1.0);
    // THE FOLIAGE STACK (the fable read's H1): eight cut-out layers above the world,
    // every sun ray and most reflection rays through all of them — the candidate
    // loop's cliff. Paired the same way; the opaque arm ("reflect.alphaTested" 0) stops at
    // the first layer. Printed, never gated.
    std::vector<NodeId> stack;
    for (int l = 0; l < 8; ++l) {
        const NodeId n = s->createNode();
        s->attachMesh(n, s->createMesh(layerMesh(2.0f + 0.25f * float(l), 20.0f, 16.0f)), fenceCut);
        enginetest::setNodePosition(s, n, Vec3(0.0f, 0.0f, -10.0f));
        stack.push_back(n);
    }
    for (int i = 0; i < 240; ++i) e->renderOneFrame();
    double ssum[2][3] = {}, sn[2][3] = {};
    for (int round = 0; round < 24; ++round)
        for (int arm = 0; arm < 2; ++arm) {
            e->setArm("reflect.alphaTested", arm ? 0.0 : 1.0);
            for (int i = 0; i < 30; ++i) {
                e->renderOneFrame();
                if (i < 10) continue;
                const float v[3] = { s->rayQueryStatus().reflectMs, s->giStatus().gather.traceMs,
                                     s->sunContactStatus().gpuMs };
                for (int k = 0; k < 3; ++k)
                    if (v[k] > 0.0f) { ssum[arm][k] += v[k]; sn[arm][k] += 1.0; }
            }
        }
    e->setArm("reflect.alphaTested", 1.0);
    const char *what[3] = { "reflection", "gather trace", "sun contact" };
    for (int k = 0; k < 3; ++k) {
        const double live = sn[0][k] ? ssum[0][k] / sn[0][k] : -1.0, door = sn[1][k] ? ssum[1][k] / sn[1][k] : -1.0;
        std::printf("target: 1920x1080, an 8-layer cut-out FOLIAGE STACK over the world: the %s %.4f ms with the "
                    "candidate loop, %.4f ms opaque (the first layer stops the ray): %+.3f ms (no bar)\n",
                    what[k], live, door, live - door);
    }
    for (int k = 0; k < 3; ++k) {
        const double live = n[0][k] ? sum[0][k] / n[0][k] : -1.0, door = n[1][k] ? sum[1][k] / n[1][k] : -1.0;
        std::printf("target: 1920x1080, 10k cubes + one fence: the %s %.4f ms with the alpha loop live, %.4f ms "
                    "opaque (reflect.alphaTested 0): %+.2f %% (bar 2 %%) [%.0f / %.0f frames]\n",
                    what[k], live, door, door > 0.0 ? 100.0 * (live - door) / door : 0.0, n[0][k], n[1][k]);
    }
    return 0;
}

static int costMain(Fixture &f)
{
    PbrParams g; g.albedo = Colour(0.9f, 0.9f, 0.9f); g.metalness = 1.0f; g.roughness = 0.2f;
    return costMainImpl(f.e, f.s, f.view, f.fenceCut, f.floor, f.s->createPbrMaterial(g));
}

int main(int argc, char **argv)
{
    Fixture f;
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-rt-alpha-tested-ogre.log";
    f.engine = Engine::create(cfg, err);
    if (!f.engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    f.engine->setFixedFrameDelta(1.0f / 60.0f);
    f.e = f.engine.get();
    f.view = f.e->createOffscreenView("rtalpha", kWidth, kHeight, Colour(0.45f, 0.55f, 0.70f));
    f.s = f.e->createScene("rtalpha");
    if (!f.view || !f.s) { std::printf("FAIL: view/scene\n"); return 1; }
    f.view->setScene(f.s);
    if (!f.e->rayQueryAvailable() || !f.e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.rt_alpha_tested is about the tier; skipping\n");
        return 0;
    }
    Scene *s = f.s;
    s->setAmbient(Colour(0.25f, 0.3f, 0.4f), Colour(0.15f, 0.15f, 0.16f));

    // THE FLOOR: carded, 8 m x 8 m, its top at y = 0; matte for the shadow arms.
    f.floor = s->createNode();
    {
        PbrParams p; p.albedo = Colour(0.8f, 0.8f, 0.8f); p.roughness = 0.8f;
        f.floorMatte = s->createPbrMaterial(p);
        PbrParams m; m.albedo = Colour(0.9f, 0.9f, 0.9f); m.metalness = 1.0f; m.roughness = 0.02f;
        f.floorMirror = s->createPbrMaterial(m);
        MeshData md = enginetest::unitCubeMesh();
        md.cards = enginetest::boxCards(0.5f);
        if (!(f.floor && s->attachMesh(f.floor, s->createMesh(md), f.floorMatte))) {
            std::printf("FAIL: the floor\n"); return 1;
        }
        enginetest::setNodeScale(s, f.floor, Vec3(8.0f, 0.2f, 8.0f));
        enginetest::setNodePosition(s, f.floor, Vec3(0.0f, -0.1f, 0.0f));
    }
    // THE FENCE: a Cutout material over the bars, and its opaque twin.
    f.fence = s->createNode();
    TextureId bars = 0;
    {
        PbrParams p; p.albedo = Colour(1.0f, 1.0f, 1.0f); p.roughness = 0.7f;
        p.alphaMode = PbrAlphaMode::Cutout; p.alphaCutoff = 0.5f;
        f.fenceCut = s->createPbrMaterial(p);
        PbrParams o = p; o.alphaMode = PbrAlphaMode::Opaque;
        f.fenceSolid = s->createPbrMaterial(o);
        bars = barsTexture(s);
        if (!(bars && s->setPbrTexture(f.fenceCut, PbrTextureSlot::Albedo, bars) &&
              s->setPbrTexture(f.fenceSolid, PbrTextureSlot::Albedo, bars))) {
            std::printf("FAIL: the bars texture\n"); return 1;
        }
        if (!(f.fence && s->attachMesh(f.fence, s->createMesh(fenceMesh()), f.fenceCut))) {
            std::printf("FAIL: the fence\n"); return 1;
        }
        s->setNodeFaceCull(f.fence, FaceCull::TwoSided);   // a fence is seen from both sides
    }
    // THE SUN behind the fence at 45 degrees, casting.
    {
        const NodeId sun = s->createNode();
        const float a = 3.14159265f / 8.0f;   // half of 45 degrees, about +X: -Y turns to -Z
        s->setNodeTransform(sun, Vec3(0, 0, 0), Quat(std::sin(a), 0.0f, 0.0f, std::cos(a)), Vec3(1, 1, 1));
        LightDesc l;
        l.type = LightType::Directional;
        l.intensity = 2.0f / 3.14159265f;
        l.castShadows = true;
        if (!s->setLight(sun, l)) { std::printf("FAIL: the sun\n"); return 1; }
    }
    f.view->setShadows(true);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 16.0f, 128, 0.0f };
    gi.cards = true;
    gi.cardResidencyRadius = 40.0f;
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: gi\n"); return 1; }
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    f.view->setPostFx(fx);
    if (argc > 1 && std::string(argv[1]) == "--cost") return costMain(f);
    // THE SHADOW ARMS look at the footprint from the fence's far side, high.
    enginetest::testCameraLookAt(f.view, Vec3(0.0f, 5.0f, -6.0f), Vec3(0.0f, 0.0f, -1.0f));

    const auto settle = [&]() { render(f.e, 60); };
    const auto wear = [&](int which) {       // 0 none, 1 cut-out, 2 opaque
        s->setNodeVisible(f.fence, which != 0);
        if (which) s->setNodeMaterial(f.fence, which == 1 ? f.fenceCut : f.fenceSolid);
        settle();
    };

    std::printf("    instances in the traced set: ");
    float rc[3] = {}, cc[3] = {}, analytic = 0.0f;
    int agree[3] = {}, cardRead[3] = {};
    Image pic[3];
    for (int w : { 0, 2, 1 }) {
        wear(w);
        std::printf("%s %d  ", w == 0 ? "none" : w == 1 ? "cut-out" : "opaque", s->rayQueryStatus().instances);
        rc[w] = rayCoverage(s, analytic, agree[w]);
        cc[w] = cardCoverage(s, cardRead[w]);
        f.view->readPixels(pic[w]);
    }
    std::printf("\n");
    std::printf("RESULT ray shadow coverage: cut-out %.3f  opaque %.3f  none %.3f  (the mask's analytic %.3f; "
                "cut-out rays agreeing with the mask %d/1600)\n", rc[1], rc[2], rc[0], analytic, agree[1]);
    std::printf("RESULT card sun term shadowed: cut-out %.3f  opaque %.3f  none %.3f  (%d/%d/%d texels read)\n",
                cc[1], cc[2], cc[0], cardRead[1], cardRead[2], cardRead[0]);
    // the picture's shadow in the cut-out: shift to a camera that sees the fence's
    // shadow in front of it — the picture is raster + the sun-contact ray, min of the two
    // (arm 4 reads it from the same frames as arms 1-2's camera)
    std::printf("RESULT picture shadow (raster + contact ray): cut-out %.3f of the opaque fence's drop\n",
                pictureShadow(pic[1], pic[0], pic[2]));

    // ARM 3: the mirror floor, rays alone.
    s->setNodeMaterial(f.floor, f.floorMirror);
    {
        PostFxDesc afx = fx;
        afx.ssrScreenMarch = false;
        f.view->setPostFx(afx);
    }
    enginetest::testCameraLookAt(f.view, kCamPos, kCamTarget);
    Image refl[3];
    for (int w : { 0, 2, 1 }) {
        wear(w);
        render(f.e, 60);
        f.view->readPixels(refl[w]);
    }
    int px = 0;
    float contrast = 0.0f;
    const float rcov = reflectedCoverage(refl[1], refl[0], refl[2], px, contrast);
    std::printf("RESULT ray reflection: the fence's share of its reflected footprint %.3f (%d px; opaque vs none "
                "%.1f codes)\n", rcov, px, contrast);
    if (const char *dump = getenv("JAH_RT_ALPHA_DUMP")) {
        for (int w = 0; w < 3; ++w) {
            const std::string p = std::string(dump) + "/refl" + std::to_string(w) + ".ppm";
            if (FILE *fp = std::fopen(p.c_str(), "wb")) {
                std::fprintf(fp, "P6\n%u %u\n255\n", refl[w].width, refl[w].height);
                for (size_t i = 0; i < size_t(refl[w].width) * refl[w].height; ++i)
                    std::fwrite(&refl[w].rgba[i * 4u], 1, 3, fp);
                std::fclose(fp);
            }
            const std::string q = std::string(dump) + "/shadow" + std::to_string(w) + ".ppm";
            if (FILE *fp = std::fopen(q.c_str(), "wb")) {
                std::fprintf(fp, "P6\n%u %u\n255\n", pic[w].width, pic[w].height);
                for (size_t i = 0; i < size_t(pic[w].width) * pic[w].height; ++i)
                    std::fwrite(&pic[w].rgba[i * 4u], 1, 3, fp);
                std::fclose(fp);
            }
        }
    }

    // THE BARS (the lane's acceptance): each arm within +-5 % of the mask's
    // coverage and nowhere near the whole quad's.
    CHECK_MSG(rc[2] > 0.95f && rc[0] < 0.01f,
              "the controls: the opaque fence shadows the footprint (%.3f), no fence nothing (%.3f)", rc[2], rc[0]);
    CHECK_MSG(std::fabs(rc[1] - analytic) <= 0.05f,
              "the cut-out's RAY SHADOW is its mask's coverage (%.3f vs %.3f +- 0.05), not the quad's (%.3f)",
              rc[1], analytic, rc[2]);
    CHECK_MSG(cardRead[1] > 100 && cc[2] > 0.9f && std::fabs(cc[1] - analytic) <= 0.05f,
              "the CARD's sun term sees the holes (%.3f shadowed vs the mask's %.3f +- 0.05; opaque %.3f)", cc[1],
              analytic, cc[2]);
    CHECK_MSG(px > 500 && contrast > 10.0f && std::fabs(rcov - 0.5f) <= 0.05f,
              "the RAY REFLECTION shows the fence with its holes (%.3f of its footprint, bar 0.5 +- 0.05)", rcov);
    CHECK_MSG(agree[1] >= 1568, "the cut-out's shadow rays land on the mask ray for ray (%d/1600 agree, bar 0.98)",
              agree[1]);

    // THE TWO-SUBMESH FENCE (the fable read's D1): one item, the bars (cut-out) and a
    // solid panel beside them (opaque, the same albedo map — a tree's trunk), in BOTH
    // submesh orders. Each ray is tested against ITS OWN submesh's datablock and row:
    // the bars carry the mask's coverage, the panel all of it.
    s->setNodeMaterial(f.floor, f.floorMatte);

    // A TEXTURE RE-UPLOADED IN PLACE RE-MAKES THE MASK (ALPHA-MASK-IDENTITY-1). The
    // mask's identity was (pointer, name, size, test) and the card sun-term trigger
    // keyed on the material word: neither moves when updateTexture writes new texels
    // into the same texture, so the rays and the cards kept the OLD holes. Both now
    // key on the texture's upload generation: the bars written SOLID in place must
    // shadow like the opaque fence, and the bars written back like the bars.
    {
        enginetest::testCameraLookAt(f.view, Vec3(0.0f, 5.0f, -6.0f), Vec3(0.0f, 0.0f, -1.0f));
        f.view->setPostFx(fx);
        wear(1);
        const unsigned n = 64;
        std::vector<unsigned char> solid(size_t(n) * n * 4u);
        for (size_t i = 0; i < size_t(n) * n; ++i) {
            solid[i * 4u] = 230; solid[i * 4u + 1u] = 40; solid[i * 4u + 2u] = 30; solid[i * 4u + 3u] = 255;
        }
        const bool wrote = s->updateTexture(bars, n, n, solid.data());
        settle();
        int a = 0, cr = 0;
        float an = 0.0f;
        const float raySolid = rayCoverage(s, an, a);
        const float cardSolid = cardCoverage(s, cr);
        // ...and back to the bars, in place again.
        std::vector<unsigned char> barsRgba(size_t(n) * n * 4u);
        for (unsigned y = 0; y < n; ++y)
            for (unsigned x = 0; x < n; ++x) {
                unsigned char *p = &barsRgba[(size_t(y) * n + x) * 4u];
                p[0] = 230; p[1] = 40; p[2] = 30; p[3] = ((x * kBars / n) % 2u) == 0u ? 255 : 0;
            }
        const bool wroteBack = s->updateTexture(bars, n, n, barsRgba.data());
        settle();
        const float rayBars = rayCoverage(s, an, a);
        const float cardBars = cardCoverage(s, cr);
        std::printf("RESULT in-place re-upload: solid -> ray %.3f card %.3f; bars again -> ray %.3f card %.3f\n",
                    raySolid, cardSolid, rayBars, cardBars);
        CHECK_MSG(wrote && raySolid > 0.95f && cardSolid > 0.9f,
                  "a mask re-uploaded SOLID in place: the rays (%.3f) and the cards (%.3f) see no holes", raySolid,
                  cardSolid);
        CHECK_MSG(wroteBack && std::fabs(rayBars - analytic) <= 0.05f && std::fabs(cardBars - analytic) <= 0.05f,
                  "...and the bars re-uploaded in place: the holes are back (ray %.3f, card %.3f vs %.3f +- 0.05)",
                  rayBars, cardBars, analytic);
    }
    s->setNodeVisible(f.fence, false);
    auto *os = static_cast<jahshaka::engine::detail::OgreScene *>(s);
    for (int order = 0; order < 2; ++order) {
        const MeshId bars = s->createMesh(fenceMesh()), panel = s->createMesh(panelMesh(1.2f, 2.2f));
        const bool trunkFirst = order == 0;
        const MeshId into = trunkFirst ? panel : bars, from = trunkFirst ? bars : panel;
        if (!os->appendSubmesh(into, from)) {
            std::printf("FAIL: appendSubmesh: %s\n", f.e->lastError().c_str());
            ++failures;
            break;
        }
        const NodeId two = s->createNode();
        const bool attached = two && s->attachMesh(two, into, f.fenceSolid) &&
                              os->setSubItemMaterial(two, trunkFirst ? 1u : 0u, f.fenceCut);
        render(f.e, 60);
        int ab = 0, nb = 0, ap = 0, np = 0;
        const float cb = rayCoverageX(s, -1.0f, 1.0f, ab, nb, true);
        const float cp = rayCoverageX(s, 1.2f, 2.2f, ap, np, false);
        const char *name = trunkFirst ? "the solid panel first, the bars second" : "the bars first, the solid panel second";
        std::printf("RESULT two-submesh fence (%s): bars %.3f (%d/%d agree), panel %.3f (%d/%d agree)\n", name, cb, ab,
                    nb, cp, ap, np);
        CHECK_MSG(attached && std::fabs(cb - analytic) <= 0.05f && ab >= int(0.98f * float(nb)),
                  "two submeshes, %s: the cut-out submesh's shadow is its mask's (%.3f vs %.3f +- 0.05; %d/%d rays on "
                  "the mask)", name, cb, analytic, ab, nb);
        CHECK_MSG(attached && cp > 0.999f, "two submeshes, %s: the opaque submesh shadows all of its footprint (%.3f)",
                  name, cp);
        s->removeNode(two);
        render(f.e, 2);
    }
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
