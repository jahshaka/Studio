// A REFLECTED ASSET'S LEVEL CHANGE DOES NOT POP (REFLECT-LOD-POP-1) — `gi.reflect_lod_pop`.
//
// THE OWNER'S FINDING (smoke 2026-09-30): flying around two glossy hemispheres,
// their reflections of a scanned asset "pop noticeably" while the asset's own
// triangles show no visible change. The ray tier builds each instance's
// bottom-level structure from a LEVEL of its chain chosen by the quality
// currency (OgreScene::updateRayLevels), and a level change swaps the reflected
// geometry in one frame.
//
// THE FIXTURE (the owner's shape): two metal hemispheres (the shipped primitive,
// baked by the import's own entry point), roughness 0.05 and 0.3, on a matte
// floor, with a baked multi-level asset between them (the Matcaps sample's
// dragon, through MeshBake::buildFromFile + SceneMirror::toMeshData — what an
// import produces and what the engine receives). The camera dollies straight
// at the pair, `kDollyFactor` closer every frame, from `kFar` to `kNear`: the
// asset's ray level changes several times on the way.
//
// THE NUMBERS, frames-counted, 8-bit codes, exposure fixed (no HDR chain:
// PostFxDesc::hdr defaults to false, so nothing meters). The REGION of a dome is the pixels whose view ray meets the
// dome (off its silhouette) and whose MIRROR ray meets the asset's bounding
// sphere — where the asset's reflection is. Per frame: the region's mean
// absolute frame-to-frame change (DELTA) and its largest per-pixel change.
//   THE POP at a crossing frame (the asset's ray level changed) = DELTA there
//     minus the MEDIAN DELTA of the non-crossing frames within +-kWindow of it
//     (the dolly's own change is not stationary — a fixed fraction of the
//     distance is a fixed fraction of the projected size — so the reference is
//     local, atom.dolly_gate's lesson).
//   THE STILL GRAIN of the same region at the same pose = the mean frame-to-
//     frame DELTA of the camera parked there (after kSettle frames).
//   THE BRIEF'S BAR (the pop at every crossing <= the still grain there) is PRINTED as
//     `target:`, not gated: the local median's own spread (+-0.3 codes) exceeds the
//     glossy dome's still grain (~0.1), so a gate would red on the estimator.
//
// MEASURED (RTX 4080 SUPER, spikes/reflect-lod-pop-1/NOTES.md), the base: every
// crossing's pop is -0.36..+0.20 codes (the paired control -0.74..+0.26) against
// still grains of 1.0-2.3 (mirror) and 0.10-0.15 (glossy) — the Matcaps dragon (6
// levels) and the owner's scan (greek_temple_scan.glb, 5 levels) alike: THE RAY-LEVEL
// SWAP IS NOT VISIBLE. What IS: a voxel cascade's re-centre (the `rebuilt` column)
// on an asset with NO CARDS (`--no-cards`, a parsed model's shape) — +8 to +9 codes
// of region mean on the mirror dome in one frame (the reflection of the asset
// appears), identical on the chain and the control. `--skinned` (PHOTON-I-1, the
// AVATAR's class: rigged, card-less by rule, every hit a decode record) read the same
// re-centres at +1.7 / +2.0 / +2.9 / +1.7 codes on the mirror dome and within +-0.7 on
// the glossy one, the asset reflected on every frame — the carded asset +1.3..+2.1
// (spikes/photon-i-1). (Its sanity checks red: one level, no chain — an instrument.)
// The PAIRED CONTROL (trap 12, one process): the identical walk with a CHAIN-LESS
// twin of the asset (the same level-0 geometry and DAG, no chain: its ray level
// is 0 for ever) — printed beside the chain walk at the same poses, the chain's
// share of each crossing frame's change.
//
// A TARGET ROW (label photon-target, TESTING_GATE §1b): an INSTRUMENT, reported and
// never deciding a tier. No part turns its printed bar green by design — the pop
// line is a measurement; today's reading is above. Its two sanity checks (the control
// is a control; the walk crosses two levels) still gate its own exit code.
//
// `--cost`: 200 instances of the asset in a field, the camera flying through it
// for kCostFrames frames — the ray-level refits a frame and the ray tier's
// structure costs (tlasMs / blasMs / blasBuilds), printed for the lead (not gated).
// `--asset <file>`: the same walk over any model file (the owner's scan) —
// a measuring tool, not a suite arm.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "cluster_fixtures.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

static const unsigned kWidth = 960;
static const unsigned kHeight = 540;
static const float kFovDeg = 45.0f;
static const int kWarmFrames = 90;
static const int kSettle = 60;          // frames a parked pose settles before its grain is read
static const int kGrainFrames = 20;     // frame-to-frame pairs the still grain averages
static const int kWindow = 20;          // the crossing's local reference, +- frames
static const float kFar = 18.0f, kNear = 2.6f;
static const float kDollyFactor = 0.996f;   // the camera's distance, per frame
static const float kCamY = 1.3f;
static const Vec3 kTarget(0.0f, 0.55f, 0.0f);
static const float kDomeX = 1.75f;          // the two domes' centres, +-x
static const float kDomeR = 1.0f;
static const float kAssetExtent = 1.3f;     // the asset's largest axis, metres
static const int kCostInstances = 200;
static const int kCostFrames = 600;

struct V3 { float x, y, z; };
static V3 v3(const Vec3 &v) { return { v.x, v.y, v.z }; }
static V3 sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static V3 add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static V3 mul(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 norm(V3 a) { const float l = std::sqrt(dot(a, a)); return mul(a, 1.0f / l); }
static V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

/// The nearest t > 0 where o + t d meets the sphere (c, r); -1 when it does not.
static float hitSphere(V3 o, V3 d, V3 c, float r)
{
    const V3 oc = sub(o, c);
    const float b = dot(oc, d), cc = dot(oc, oc) - r * r;
    const float disc = b * b - cc;
    if (disc < 0.0f) return -1.0f;
    const float s = std::sqrt(disc);
    const float t0 = -b - s, t1 = -b + s;
    return t0 > 1e-4f ? t0 : (t1 > 1e-4f ? t1 : -1.0f);
}

struct Sphere { V3 c; float r; };

/// THE ASSET'S REFLECTION ON DOME `dome` seen from `eye` (the fixture camera's basis,
/// testCameraDescLookAt's, the engine's default vertical fov): the pixels whose view
/// ray meets the dome's upper half off its silhouette (the view ray and the normal
/// within ~78 degrees) with nothing of the asset in front, and whose mirror ray meets
/// the asset's bounding sphere.
static std::vector<unsigned> regionOf(const Vec3 &eyeV, const Sphere &dome, const Sphere &asset)
{
    const V3 eye = v3(eyeV);
    const V3 f = norm(sub(v3(kTarget), eye));
    const V3 r = norm(cross(f, V3{ 0.0f, 1.0f, 0.0f }));
    const V3 u = cross(r, f);
    const float t = std::tan(kFovDeg * 0.5f * 3.14159265f / 180.0f);
    const float aspect = float(kWidth) / float(kHeight);
    // THE DOME'S SCREEN RECTANGLE (its box's corners projected), so the test walks
    // its pixels and not the frame's.
    float x0 = float(kWidth), x1 = 0.0f, y0 = float(kHeight), y1 = 0.0f;
    for (int c = 0; c < 8; ++c) {
        const V3 q = { dome.c.x + (c & 1 ? dome.r : -dome.r), dome.c.y + (c & 2 ? dome.r : 0.0f),
                       dome.c.z + (c & 4 ? dome.r : -dome.r) };
        const V3 dq = sub(q, eye);
        const float z = dot(dq, f);
        if (z <= 0.05f) { x0 = 0.0f; x1 = float(kWidth); y0 = 0.0f; y1 = float(kHeight); break; }
        const float sx = (dot(dq, r) / (z * t * aspect) + 1.0f) * 0.5f * float(kWidth);
        const float sy = (1.0f - dot(dq, u) / (z * t)) * 0.5f * float(kHeight);
        x0 = std::min(x0, sx); x1 = std::max(x1, sx); y0 = std::min(y0, sy); y1 = std::max(y1, sy);
    }
    const unsigned xa = unsigned(std::max(0.0f, x0 - 2.0f)), xb = unsigned(std::min(float(kWidth), x1 + 2.0f));
    const unsigned ya = unsigned(std::max(0.0f, y0 - 2.0f)), yb = unsigned(std::min(float(kHeight), y1 + 2.0f));
    std::vector<unsigned> px;
    for (unsigned y = ya; y < yb; ++y)
        for (unsigned x = xa; x < xb; ++x) {
            const float nx = ((float(x) + 0.5f) / float(kWidth) * 2.0f - 1.0f) * t * aspect;
            const float ny = (1.0f - (float(y) + 0.5f) / float(kHeight) * 2.0f) * t;
            const V3 d = norm(add(f, add(mul(r, nx), mul(u, ny))));
            const float th = hitSphere(eye, d, dome.c, dome.r);
            if (th < 0.0f) continue;
            const V3 p = add(eye, mul(d, th));
            if (p.y < dome.c.y + 0.03f) continue;               // the lower half is under the floor
            const float ta = hitSphere(eye, d, asset.c, asset.r);
            if (ta > 0.0f && ta < th) continue;                 // the asset in front of the dome
            const V3 n = mul(sub(p, dome.c), 1.0f / dome.r);
            if (-dot(d, n) < 0.2f) continue;                    // the silhouette: motion owns it
            const V3 m = sub(d, mul(n, 2.0f * dot(d, n)));
            if (hitSphere(p, m, asset.c, asset.r) < 0.0f) continue;
            px.push_back(y * kWidth + x);
        }
    return px;
}

/// Mean absolute difference over the region (codes, RGB averaged) and the largest
/// per-pixel one (the channel mean of that pixel).
static void delta(const Image &a, const Image &b, const std::vector<unsigned> &px, float &mean, float &peak)
{
    mean = peak = 0.0f;
    if (px.empty() || a.width != b.width || a.height != b.height) return;
    double s = 0.0;
    for (unsigned i : px) {
        float p = 0.0f;
        for (int k = 0; k < 3; ++k)
            p += std::fabs(float(a.rgba[size_t(i) * 4u + k]) - float(b.rgba[size_t(i) * 4u + k]));
        p /= 3.0f;
        s += p;
        peak = std::max(peak, p);
    }
    mean = float(s / double(px.size()));
}

static float median(std::vector<float> v)
{
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static void writePpm(const Image &img, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (size_t i = 0; i < size_t(img.width) * img.height; ++i)
        std::fwrite(&img.rgba[i * 4u], 1, 3, f);
    std::fclose(f);
}

static Vec3 eyeAt(float dist) { return Vec3(0.0f, kCamY, dist); }

/// The first baked triangle mesh of a model file, normalised: `extent` metres on its
/// largest axis, its base on y = 0, centred on x/z. `centreOut` / `radiusOut` = its
/// bounding sphere in the scene (from the scaled box).
struct Placed { MeshData data; float scale = 1.0f; Vec3 offset; Sphere bound; };
static bool loadBaked(const std::string &path, float extent, Placed &out, bool keepChain = true)
{
    const std::vector<iris::MeshPtr> ms = clusterfix::loadModel(path);
    if (ms.empty()) return false;
    // THE LARGEST MESH of the file (a multi-mesh scan's main body).
    size_t best = 0, bestTris = 0;
    for (size_t i = 0; i < ms.size(); ++i) {
        const size_t tris = ms[i]->getIndexBuffer() ? size_t(ms[i]->getIndexBuffer()->dataSize) / 12u : 0u;
        if (tris > bestTris) { bestTris = tris; best = i; }
    }
    clusterfix::Fixture f;
    if (!clusterfix::bake(f, ms[best], path)) return false;
    out.data = f.data;
    if (!keepChain) {
        // THE CHAIN-LESS TWIN: level 0 only, its cards captured from level 0 (a card
        // names the level it was captured from, and a one-level mesh has only that).
        out.data.lodIndices.clear();
        out.data.lodBounds.clear();
        out.data.lodErrors.clear();
        for (MeshCardDesc &c : out.data.cards) c.lodLevel = 0;
    }
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t v = 0; v + 2 < out.data.positions.size(); v += 3)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], out.data.positions[v + size_t(k)]);
            hi[k] = std::max(hi[k], out.data.positions[v + size_t(k)]);
        }
    const float ext = std::max(hi[0] - lo[0], std::max(hi[1] - lo[1], hi[2] - lo[2]));
    out.scale = extent / ext;
    out.offset = Vec3(-0.5f * (lo[0] + hi[0]) * out.scale, -lo[1] * out.scale, -0.5f * (lo[2] + hi[2]) * out.scale);
    const float hx = 0.5f * (hi[0] - lo[0]) * out.scale, hy = 0.5f * (hi[1] - lo[1]) * out.scale,
                hz = 0.5f * (hi[2] - lo[2]) * out.scale;
    out.bound = { { 0.0f, hy, 0.0f }, std::sqrt(hx * hx + hy * hy + hz * hz) };
    return true;
}

static unsigned slotOf(Scene *s, NodeId node)
{
    const GpuSceneStatus st = s->gpuSceneStatus();
    for (unsigned i = 0; i < st.slotCount; ++i) {
        GpuSceneEntry e;
        if (s->gpuSceneEntry(i, e) && e.nodeId == node) return i;
    }
    return 0xFFFFFFFFu;
}

static unsigned rayLevelOf(Scene *s, NodeId node)
{
    const unsigned slot = slotOf(s, node);
    GpuSceneEntry e;
    return slot != 0xFFFFFFFFu && s->gpuSceneEntry(slot, e) ? e.rayLevel : 0xFFFFFFFFu;
}

struct Frame {
    float dist = 0.0f;
    unsigned level = 0;
    unsigned long long refits = 0;
    float mean[2] = { 0, 0 }, peak[2] = { 0, 0 };
    unsigned pixels[2] = { 0, 0 };
    std::string rebuilt;    ///< the voxel cascades re-voxelised this frame (diagnosis)
};

/// THE COST ARM (--cost): kCostInstances copies of the asset scattered over a
/// 60 x 60 m field, the camera flying a straight line through it at 0.15 m a frame
/// and back. Printed: the ray-level walks' evaluations and refits a frame, the TLAS
/// and BLAS builds, and the ray tier's structure GPU milliseconds.
static int costMain(Engine *e, const Placed &asset)
{
    View *view = e->createOffscreenView("lodpop-cost", 1920, 1080, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("lodpop-cost");
    if (!view || !s) { std::printf("FAIL: cost view/scene\n"); return 1; }
    view->setScene(s);
    s->setAmbient(Colour(0.45f, 0.55f, 0.70f), Colour(0.30f, 0.30f, 0.32f));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);
    const MeshId mesh = s->createMesh(asset.data);
    PbrParams ap; ap.albedo = Colour(0.8f, 0.5f, 0.3f); ap.roughness = 0.5f;
    const MaterialId am = s->createPbrMaterial(ap);
    PbrParams gp; gp.albedo = Colour(0.9f, 0.9f, 0.9f); gp.metalness = 1.0f; gp.roughness = 0.1f;
    const NodeId floor = s->createNode();
    s->attachMesh(floor, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(gp));
    enginetest::setNodeScale(s, floor, Vec3(80.0f, 0.2f, 80.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    unsigned seed = 12345u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
    for (int i = 0; i < kCostInstances; ++i) {
        const NodeId n = s->createNode();
        s->attachMesh(n, mesh, am);
        enginetest::setNodeScale(s, n, Vec3(asset.scale, asset.scale, asset.scale));
        enginetest::setNodePosition(s, n, Vec3(asset.offset.x + (rnd() - 0.5f) * 60.0f, asset.offset.y,
                                               asset.offset.z + (rnd() - 0.5f) * 60.0f));
    }
    GiParams gi; gi.mode = GiMode::Vct; gi.quality = GiQuality::High; gi.numBounces = 1;
    s->setGlobalIllumination(gi);
    PostFxDesc fx; fx.allowOffscreen = true; fx.ssr = 2;
    view->setPostFx(fx);
    float z = 32.0f, dz = -0.15f;
    auto place = [&]() { enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, z), Vec3(0.0f, 1.2f, z + (dz < 0 ? -10.0f : 10.0f))); };
    place();
    for (int i = 0; i < kWarmFrames; ++i) e->renderOneFrame();
    const GpuSceneStatus g0 = s->gpuSceneStatus();
    const RayQueryStatus r0 = s->rayQueryStatus();
    unsigned long long maxRefits = 0, prevRefits = g0.rayLevelRefits;
    for (int f = 0; f < kCostFrames; ++f) {
        z += dz;
        if (z < -32.0f || z > 32.0f) dz = -dz;
        place();
        e->renderOneFrame();
        const GpuSceneStatus g = s->gpuSceneStatus();
        maxRefits = std::max(maxRefits, g.rayLevelRefits - prevRefits);
        prevRefits = g.rayLevelRefits;
    }
    const GpuSceneStatus g1 = s->gpuSceneStatus();
    const RayQueryStatus r1 = s->rayQueryStatus();
    std::printf("target: cost, %d instances flying %d frames at 1920x1080: ray-level walks %llu, evals %.2f/frame, "
                "refits %.3f/frame (max %llu in one frame); TLAS builds %llu refits %llu, BLAS builds %llu "
                "(one mesh shared by every instance: its per-(mesh, level) BLAS are built at first use, "
                "before the flight); tlasMs last reading %.4f, blasMs last reading %.4f (sticky: the last "
                "measured batch); gpuscene scan %.3f ms\n",
                kCostInstances, kCostFrames, g1.rayLevelWalks - g0.rayLevelWalks,
                double(g1.rayLevelEvals - g0.rayLevelEvals) / kCostFrames,
                double(g1.rayLevelRefits - g0.rayLevelRefits) / kCostFrames, maxRefits,
                r1.tlasBuilds - r0.tlasBuilds, r1.tlasRefits - r0.tlasRefits, r1.blasBuilds - r0.blasBuilds,
                double(r1.tlasMs), double(r1.blasMs), g1.lastScanMicros / 1000.0);
    return 0;
}

int main(int argc, char **argv)
{
    const char *dumpDir = getenv("JAH_LOD_POP_DUMP");
    std::string assetPath = std::string(CLUSTER_FIXTURE_DIR) + "/matcaps_dragon.obj";
    bool cost = false;
    // `--no-cards`: the asset with no surface-cache cards (a PARSED model's shape — every
    // hit on it voxel-answered). A diagnosis switch, not a suite arm.
    bool noCards = false;
    // `--skinned`: the asset skinned whole to one bone (an AVATAR's class: a rigged item has
    // no cards BY RULE, and every ray hit on it is a hit-list record the decode shades).
    bool skinned = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--cost") cost = true;
        else if (a == "--asset" && i + 1 < argc) assetPath = argv[++i];
        else if (a == "--no-cards") noCards = true;
        else if (a == "--skinned") skinned = true;
    }
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-reflect-lod-pop-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    Placed asset, twin, dome;
    if (!loadBaked(assetPath, kAssetExtent, asset) || !loadBaked(assetPath, kAssetExtent, twin, false)) {
        std::printf("FAIL: the asset %s\n", assetPath.c_str()); return 1;
    }
    if (skinned) asset = twin;   // a rigged item draws one level (attachSkinnedMesh)
    if (noCards || skinned) { asset.data.cards.clear(); twin.data.cards.clear(); }
    if (skinned)
        for (Placed *pl : { &asset, &twin }) {
            const size_t nv = pl->data.positions.size() / 3u;
            pl->data.blendIndices.assign(nv * 4u, 0);
            pl->data.blendWeights.assign(nv * 4u, 0.0f);
            for (size_t i = 0; i < nv; ++i) pl->data.blendWeights[i * 4u] = 1.0f;
        }
    std::printf("    asset %s: %zu triangles, %zu coarser levels, bounds (mesh units, x scale %.4f):",
                assetPath.c_str(), asset.data.indices.size() / 3u, asset.data.lodBounds.size(), asset.scale);
    for (float b : asset.data.lodBounds) std::printf(" %.5f", b);
    std::printf("\n");
    CHECK_MSG(!asset.data.lodBounds.empty(), "the asset carries a chain (%zu levels)", asset.data.lodBounds.size());
    // THE TIER IS KNOWN ONCE A VIEW EXISTS (gi.reflect_mover's order).
    View *view = e->createOffscreenView("lodpop", kWidth, kHeight, Colour(0.45f, 0.55f, 0.70f));
    if (!(e->rayQueryAvailable() && e->rayTracing())) {
        std::printf("ok: no ray queries on this machine — gi.reflect_lod_pop is about the tier; skipping\n");
        return 0;
    }
    if (cost) return costMain(e, asset);
    if (!loadBaked(std::string(JAHSHAKA_TEST_SOURCE_DIR) + "/app/content/primitives/hemisphere.obj", 1.0f, dome)) {
        std::printf("FAIL: the hemisphere\n"); return 1;
    }

    Scene *s = e->createScene("lodpop");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    s->setAmbient(Colour(0.45f, 0.55f, 0.70f), Colour(0.30f, 0.30f, 0.32f));
    enginetest::addDirectionalLight(s, Vec3(-0.4f, -1.0f, -0.5f), 2.0f);

    const NodeId floor = s->createNode();
    PbrParams fp; fp.albedo = Colour(0.5f, 0.5f, 0.5f); fp.roughness = 0.9f;
    s->attachMesh(floor, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(fp));
    enginetest::setNodeScale(s, floor, Vec3(60.0f, 0.2f, 60.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));

    // THE DOMES: the shipped hemisphere, its flat base on the floor. `loadBaked` put the
    // primitive's base on y = 0 at 2 m wide; the sphere's centre is the base's.
    const MeshId domeMesh = s->createMesh(dome.data);
    const float domeRough[2] = { 0.05f, 0.3f };
    Sphere domes[2];
    for (int k = 0; k < 2; ++k) {
        const NodeId n = s->createNode();
        PbrParams p; p.albedo = Colour(0.95f, 0.95f, 0.95f); p.metalness = 1.0f; p.roughness = domeRough[k];
        s->attachMesh(n, domeMesh, s->createPbrMaterial(p));
        const float sc = dome.scale * 2.0f * kDomeR;
        enginetest::setNodeScale(s, n, Vec3(sc, sc, sc));
        const float x = k ? kDomeX : -kDomeX;
        enginetest::setNodePosition(s, n, Vec3(x + dome.offset.x * 2.0f * kDomeR, dome.offset.y * 2.0f * kDomeR,
                                               dome.offset.z * 2.0f * kDomeR));
        domes[k] = { { x, 0.0f, 0.0f }, kDomeR };
    }

    // THE ASSET and its chain-less twin (the control), one visible at a time.
    PbrParams ap; ap.albedo = Colour(0.85f, 0.45f, 0.2f); ap.roughness = 0.5f;
    const MaterialId am = s->createPbrMaterial(ap);
    const NodeId nodes[2] = { s->createNode(), s->createNode() };
    const MeshId meshes[2] = { s->createMesh(asset.data), s->createMesh(twin.data) };
    SkeletonDesc rig;
    rig.id = "gi.reflect_lod_pop skinned asset rig v1";
    { BoneDesc root; root.name = "root"; rig.bones.push_back(root); }
    for (int k = 0; k < 2; ++k) {
        if (!(nodes[k] && meshes[k] &&
              (skinned ? s->attachSkinnedMesh(nodes[k], meshes[k], am, rig) : s->attachMesh(nodes[k], meshes[k], am)))) {
            std::printf("FAIL: the asset node: %s\n", e->lastError().c_str()); return 1;
        }
        enginetest::setNodeScale(s, nodes[k], Vec3(asset.scale, asset.scale, asset.scale));
        enginetest::setNodePosition(s, nodes[k], asset.offset);
    }
    const Sphere assetBound = asset.bound;

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: gi\n"); return 1; }
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;

    struct Walk { std::vector<Frame> frames; std::vector<int> crossings; std::vector<float> grain[2]; };
    auto walk = [&](int which, bool march) {
        Walk w;
        s->setNodeVisible(nodes[0], which == 0);
        s->setNodeVisible(nodes[1], which == 1);
        PostFxDesc f2 = fx; f2.ssrScreenMarch = march; view->setPostFx(f2);
        float dist = kFar;
        enginetest::testCameraLookAt(view, eyeAt(dist), kTarget);
        for (int i = 0; i < kWarmFrames; ++i) e->renderOneFrame();
        Image prev;
        view->readPixels(prev);
        unsigned prevLevel = rayLevelOf(s, nodes[which]);
        std::vector<unsigned> region[2];
        std::vector<unsigned long long> cascadeRebuilds;
        while (dist > kNear) {
            dist *= kDollyFactor;
            const Vec3 eye = eyeAt(dist);
            enginetest::testCameraLookAt(view, eye, kTarget);
            e->renderOneFrame();
            Image img;
            if (!view->readPixels(img)) { std::printf("FAIL: readPixels\n"); ++failures; break; }
            Frame fr;
            fr.dist = dist;
            fr.level = rayLevelOf(s, nodes[which]);
            fr.refits = s->gpuSceneStatus().rayLevelRefits;
            {
                const GiStatus gs = s->giStatus();
                for (size_t c = 0; c < gs.cascades.size(); ++c) {
                    if (c >= cascadeRebuilds.size()) cascadeRebuilds.resize(c + 1, 0ull);
                    if (gs.cascades[c].rebuilds != cascadeRebuilds[c]) fr.rebuilt += std::to_string(c);
                    cascadeRebuilds[c] = gs.cascades[c].rebuilds;
                }
            }
            for (int k = 0; k < 2; ++k) {
                // THE UNION of this pose's region and the last one's: the reflection moved.
                std::vector<unsigned> now = regionOf(eye, domes[k], assetBound), both;
                std::set_union(now.begin(), now.end(), region[k].begin(), region[k].end(), std::back_inserter(both));
                delta(prev, img, both, fr.mean[k], fr.peak[k]);
                fr.pixels[k] = unsigned(now.size());
                region[k] = now;
            }
            if (const char *want = getenv("JAH_LOD_POP_FRAMES")) {
                const std::string key = "," + std::string(want) + ",";
                if (dumpDir && key.find("," + std::to_string(w.frames.size()) + ",") != std::string::npos)
                    writePpm(img, std::string(dumpDir) + "/frame" + std::to_string(w.frames.size()) + "-w" +
                                      std::to_string(which) + (march ? "-march" : "-rays") + ".ppm");
            }
            if (fr.level != prevLevel) {
                w.crossings.push_back(int(w.frames.size()));
                if (dumpDir && which == 0) {
                    writePpm(prev, std::string(dumpDir) + "/cross" + std::to_string(w.crossings.size()) +
                                       (march ? "-march" : "-rays") + "-a.ppm");
                    writePpm(img, std::string(dumpDir) + "/cross" + std::to_string(w.crossings.size()) +
                                      (march ? "-march" : "-rays") + "-b.ppm");
                }
            }
            prevLevel = fr.level;
            w.frames.push_back(fr);
            prev = img;
        }
        return w;
    };
    // THE STILL GRAIN at a pose: parked, settled, then the mean frame-to-frame change.
    // THE CHAIN'S scene, not the control's: walk(1) ran last and left the twin shown.
    auto grainAt = [&](float dist, float out[2]) {
        s->setNodeVisible(nodes[0], true);
        s->setNodeVisible(nodes[1], false);
        const Vec3 eye = eyeAt(dist);
        enginetest::testCameraLookAt(view, eye, kTarget);
        for (int i = 0; i < kSettle; ++i) e->renderOneFrame();
        std::vector<unsigned> region[2] = { regionOf(eye, domes[0], assetBound), regionOf(eye, domes[1], assetBound) };
        Image prev, img;
        view->readPixels(prev);
        double acc[2] = { 0, 0 };
        for (int i = 0; i < kGrainFrames; ++i) {
            e->renderOneFrame();
            view->readPixels(img);
            for (int k = 0; k < 2; ++k) { float m, p; delta(prev, img, region[k], m, p); acc[k] += m; }
            prev = img;
        }
        for (int k = 0; k < 2; ++k) out[k] = float(acc[k] / kGrainFrames);
    };

    const char *domeName[2] = { "mirror dome 0.05", "glossy dome 0.3" };
    for (int arm = 0; arm < 2; ++arm) {
        const bool march = arm == 0;
        const char *armName = march ? "march + rays (the desktop default)" : "rays alone (a headset's chain)";
        const Walk chain = walk(0, march);
        const Walk ctrl = walk(1, march);
        std::printf("  == %s: %zu frames, %zu crossings (the chain), %zu (the control)\n", armName,
                    chain.frames.size(), chain.crossings.size(), ctrl.crossings.size());
        CHECK_MSG(ctrl.crossings.empty(), "%s: the control is a control (no ray-level change on the chain-less twin)",
                  armName);
        CHECK_MSG(chain.crossings.size() >= 2u, "%s: the walk crosses at least two ray levels (%zu)", armName,
                  chain.crossings.size());
        for (int c : chain.crossings) {
            const Frame &fr = chain.frames[size_t(c)];
            float grain[2];
            grainAt(fr.dist, grain);
            for (int k = 0; k < 2; ++k) {
                std::vector<float> ref;
                for (int j = std::max(0, c - kWindow); j <= std::min(int(chain.frames.size()) - 1, c + kWindow); ++j) {
                    bool near = false;
                    for (int c2 : chain.crossings) near = near || (j >= c2 && j <= c2 + 3);
                    if (!near) ref.push_back(chain.frames[size_t(j)].mean[k]);
                }
                const float med = median(ref);
                const float pop = fr.mean[k] - med;
                const float after = c + 1 < int(chain.frames.size()) ? chain.frames[size_t(c) + 1].mean[k] - med : 0.0f;
                const float paired = size_t(c) < ctrl.frames.size() ? fr.mean[k] - ctrl.frames[size_t(c)].mean[k] : 0.0f;
                std::printf("    crossing L%u->L%u at %.2f m, %s (%u px): delta %.3f (peak %.1f), local median %.3f, "
                            "POP %.3f (next frame %+.3f; paired with the control %+.3f), still grain %.3f\n",
                            c ? chain.frames[size_t(c) - 1].level : 99u, fr.level, fr.dist, domeName[k], fr.pixels[k],
                            fr.mean[k], fr.peak[k], med, pop, after, paired, grain[k]);
                // PRINTED, NOT GATED (the measurement, spikes/reflect-lod-pop-1): the local-median
                // estimator's own spread under this dolly (+-0.3 codes) is larger than the glossy
                // dome's still grain (0.1), so "pop <= grain" would gate the estimator, not the swap.
                if (fr.pixels[k] >= 50u)
                    std::printf("target: %s, %s, crossing at %.2f m: the pop %.3f codes <= the still grain %.3f%s\n",
                                armName, domeName[k], fr.dist, pop, grain[k], pop <= grain[k] ? "" : " (over)");
            }
        }
        if (dumpDir) {
            const std::string p = std::string(dumpDir) + (march ? "/walk-march.tsv" : "/walk-rays.tsv");
            if (FILE *f = std::fopen(p.c_str(), "w")) {
                std::fprintf(f, "frame\tdist\tlevel\trefits\tmean0\tpeak0\tmean1\tpeak1\tctrl0\tctrl1\trebuilt\n");
                for (size_t i = 0; i < chain.frames.size(); ++i) {
                    const Frame &a = chain.frames[i];
                    const Frame b = i < ctrl.frames.size() ? ctrl.frames[i] : Frame();
                    std::fprintf(f, "%zu\t%.3f\t%u\t%llu\t%.3f\t%.1f\t%.3f\t%.1f\t%.3f\t%.3f\t%s\n", i, a.dist, a.level,
                                 a.refits, a.mean[0], a.peak[0], a.mean[1], a.peak[1], b.mean[0], b.mean[1],
                                 a.rebuilt.empty() ? "-" : a.rebuilt.c_str());
                }
                std::fclose(f);
            }
        }
    }

    std::printf(failures ? "FAIL: %d check(s) failed\n" : "PASS: gi.reflect_lod_pop\n", failures);
    return failures ? 1 : 0;
}
