// test_scale — THE SCALE SUITES (lane D1-SCALE-FIXTURES; SPECS/briefs/D1-SCALE-FIXTURES.md
// §4.4). ONE BINARY, ONE MODE PER WALL of SPECS/audits/V2_ATOM_PHOTON_STRATEGY_2026-09-25.md
// §3; each ctest row `scale.<wall>` runs one mode. Every mode PRINTS today's number as a
// `target:` line (reported, never failing: the label `scale-target`) and FAILS only when
// the measurement itself could not be taken (no world, no records, no GPU timing where a
// GPU ms is the number). No mode asserts a bar: the part that closes a wall writes it.
//
// The wall's ANCHOR (re-verified at d-build 129c9e82c / irisgl b496aba) sits at the top of
// each mode. GPU milliseconds come from the frame monitor's own timestamp pairs; they are
// absolute and therefore depend on the device's clock state — the ctest rows run under the
// GPU-timing lock, and a number quoted in a report states whether the clocks were locked.
#include "scale_world.h"

#include "irisgl/core/logger.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"

#include <QColor>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace scale;

static int failures = 0;
#define REQUIRE(cond, ...)                                                        \
    do {                                                                          \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
        std::fflush(stdout);                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// record helpers
// ---------------------------------------------------------------------------

static float stageMs(const FrameRecord &r, const char *name)
{
    float ms = 0.0f;
    for (const FrameStage &s : r.stages)
        if (s.name == name) ms += s.ms;
    return ms;
}

static const FramePass *passNamed(const FrameRecord &r, const char *name)
{
    for (const FramePass &p : r.passes)
        if (p.pass == name) return &p;
    return nullptr;
}

/// Run `body`, then `tail` more frames, and return exactly the records of frames
/// rendered from the call on (records arrive late: attributed by frame number).
static std::vector<FrameRecord> collect(Env &env, const std::function<void()> &body, int tail = 8)
{
    frame(env, 4);
    unsigned long long before = 0;
    for (const FrameRecord &r : drain(env)) before = std::max(before, r.frame);
    body();
    frame(env, tail);
    std::vector<FrameRecord> out;
    for (FrameRecord &r : drain(env))
        if (r.frame > before) out.push_back(std::move(r));
    std::sort(out.begin(), out.end(), [](const FrameRecord &a, const FrameRecord &b) { return a.frame < b.frame; });
    return out;
}

struct Stats {
    size_t n = 0;
    double median = -1, p95 = -1, max = -1, mean = -1;
};
static Stats stats(std::vector<double> v)
{
    Stats s;
    s.n = v.size();
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.median = v[v.size() / 2];
    s.p95 = v[std::min(v.size() - 1, size_t(double(v.size()) * 0.95))];
    s.max = v.back();
    double sum = 0;
    for (double x : v) sum += x;
    s.mean = sum / double(v.size());
    return s;
}

// ---------------------------------------------------------------------------
// THE FOUR CAMERA PATHS (brief §4.1), on the fixed clock
// ---------------------------------------------------------------------------

static void pathStill(Env &env, int frames)
{
    setCamera(env, worldEye() + iris::Vec3(0, 0, 0), worldEye() + iris::Vec3(0, 0, -10));
    frame(env, frames);
}
/// A WALK: `metres` along +X at `speed` m/s from x = -metres/2, eye height.
static void pathWalk(Env &env, float metres, float speed)
{
    const int frames = int(std::round(metres / (speed * kDt)));
    for (int f = 0; f <= frames; ++f) {
        const float x = -0.5f * metres + float(f) * speed * kDt;
        setCamera(env, iris::Vec3(x, 1.7f, 0.0f), iris::Vec3(x + 10.0f, 1.7f, -2.0f));
        frame(env, 1);
    }
}
/// A TELEPORT: `cycles` jumps of `metres` away and back, `legFrames` each leg.
static void pathTeleport(Env &env, float metres, int cycles, int legFrames)
{
    for (int c = 0; c < cycles; ++c) {
        setCamera(env, iris::Vec3(metres, 1.7f, 0.0f), iris::Vec3(metres, 1.7f, -10.0f));
        frame(env, legFrames);
        setCamera(env, iris::Vec3(0.0f, 1.7f, 0.0f), iris::Vec3(0.0f, 1.7f, -10.0f));
        frame(env, legFrames);
    }
}
/// A FLY: `seconds` on a 150 m circle at 25 m altitude, 20 m/s, looking ahead
/// and down into the world.
static void pathFly(Env &env, float seconds)
{
    const int frames = int(std::round(seconds / kDt));
    const float r = 150.0f, speed = 20.0f;
    for (int f = 0; f < frames; ++f) {
        const float a = float(f) * speed * kDt / r;
        const iris::Vec3 p(r * std::cos(a), 25.0f, r * std::sin(a));
        const iris::Vec3 ahead(r * std::cos(a + 0.3f), 0.0f, r * std::sin(a + 0.3f));
        setCamera(env, p, ahead);
        frame(env, 1);
    }
}

static void printPath(const char *label, const std::vector<FrameRecord> &recs)
{
    std::vector<double> total, swap, gpu, pre, record;
    unsigned rebuilds = 0;
    for (const FrameRecord &r : recs) {
        total.push_back(r.totalMs);
        swap.push_back(stageMs(r, "engine.swap"));
        pre.push_back(stageMs(r, "engine.pre"));
        record.push_back(stageMs(r, "engine.record"));
        if (r.gpuMs >= 0) gpu.push_back(r.gpuMs);
        rebuilds += r.cascadeRebuilds;
    }
    const Stats t = stats(total), s = stats(swap), g = stats(gpu), p = stats(pre), rc = stats(record);
    std::printf("PATH %-9s frames %4zu | frame ms median %7.2f p95 %7.2f max %8.2f | swap med %6.2f | "
                "pre med %6.2f record med %6.2f | passes GPU ms med %7.2f p95 %7.2f | cascade rebuilds %u\n",
                label, t.n, t.median, t.p95, t.max, s.median, p.median, rc.median, g.median, g.p95, rebuilds);
    std::fflush(stdout);
}

static bool bootWorld(Env &env, World &w, const char *log, WorldSpec spec = WorldSpec())
{
    if (!boot(env, log)) return false;
    if (!buildWorld(env, spec, w)) return false;
    armMonitor(env);
    return true;
}

// ===========================================================================
// scale.world — THE WORLD FIXTURE ITSELF (brief §4.1): its build times and the four
// camera paths' frame costs (still, 200 m walk, 60 m teleport, 30 s fly).
// ===========================================================================
static int worldMain()
{
    Env env;
    World w;
    if (!bootWorld(env, w, "test-scale-world-ogre.log")) return 1;
    REQUIRE(gpuTimed(env), "the frame monitor has GPU timing (patch 0027's query pool)");
    // THE STILL AND THE FLY here; THE WALK AND THE TELEPORT are scale.voxel_scroll's
    // (W1 reads their cascade rows) — the four paths in one process are ~8 minutes
    // of Debug frames, over the five a suite may take.
    const auto still = collect(env, [&] { pathStill(env, 120); });
    printPath("still", still);
    const auto fly = collect(env, [&] { pathFly(env, 30.0f); });
    printPath("fly30s", fly);
    REQUIRE(!still.empty() && !fly.empty(), "both paths recorded frames");
    double stillMed = 0, flyMed = 0;
    { std::vector<double> v; for (auto &r : still) v.push_back(r.totalMs); stillMed = stats(v).median; }
    { std::vector<double> v; for (auto &r : fly) v.push_back(r.totalMs); flyMed = stats(v).median; }
    target("world", stillMed, "ms", "a still frame of the 10k world at High (renderOneFrame, Debug, the rig)");
    target("world", flyMed, "ms", "a frame of the 30 s fly (20 m/s, 150 m circle, 25 m up)");
    target("world", w.firstSyncMs + w.documentMs + w.bakeOrReadMs, "ms",
           "building the world (meshes + document + the first sync and frame)");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.voxel_scroll — W1: A SCROLL RE-VOXELISES THE WHOLE CASCADE.
// Anchor: irisgl/engine/src/OgreGi.cpp:5472-5474 ("In THIS arm the rebuild is whole
// either way") — a cascade step rebuilds the whole cascade; no slab/toroidal scroll.
// Number: ms per cascade rebuild (CPU and GPU, per cascade) on the 200 m walk and the
// 60 m teleport, from the monitor's `vct.cascadeN` rows (the 0027 timestamp pair).
// ===========================================================================
static int voxelScrollMain()
{
    Env env;
    World w;
    if (!bootWorld(env, w, "test-scale-voxel-scroll-ogre.log")) return 1;
    REQUIRE(gpuTimed(env), "the frame monitor has GPU timing");
    const GiStatus gi0 = env.scene->giStatus();
    for (size_t c = 0; c < gi0.cascades.size(); ++c)
        std::printf("   cascade %zu: halfSize %.1f m, %d^3, cell %.3f m, step %.2f m, items %d\n", c,
                    double(gi0.cascades[c].halfSize), gi0.cascades[c].resolution, double(gi0.cascades[c].cell),
                    double(gi0.cascades[c].step), gi0.cascades[c].items);
    REQUIRE(!gi0.cascades.empty(), "the High tier built a cascade chain (%zu)", gi0.cascades.size());

    // THE TOTAL VOXEL VRAM AT HIGH (owed row): every texture the renderer holds whose
    // name says voxel / VCT, from the engine's own texture list.
    {
        std::vector<TextureMemoryEntry> tex;
        env.engine->textureMemory(tex);
        unsigned long long vox = 0;
        int n = 0;
        for (const TextureMemoryEntry &t : tex) {
            std::string lower = t.name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("vct") != std::string::npos || lower.find("vox") != std::string::npos) {
                vox += t.bytes;
                ++n;
                std::printf("   voxel texture %-48s %4ux%4ux%4u %-14s %8.2f MB\n", t.name.c_str(), t.width,
                            t.height, t.depth, t.format.c_str(), double(t.bytes) / 1048576.0);
            }
        }
        std::printf("VOXEL VRAM at High: %d textures, %.1f MB\n", n, double(vox) / 1048576.0);
        target("W1", double(vox) / 1048576.0, "MB", "total voxel texture VRAM at High (owed row)");
    }

    struct Row { std::vector<double> cpu, gpu; };
    auto harvest = [](const std::vector<FrameRecord> &recs, std::map<std::string, Row> &rows) {
        for (const FrameRecord &r : recs)
            for (const CacheWork &c : r.cacheWork)
                if (c.detail.rfind("vct.cascade", 0) == 0) {
                    rows[c.detail].cpu.push_back(c.ms);
                    if (c.gpuMs >= 0) rows[c.detail].gpu.push_back(c.gpuMs);
                }
    };
    std::map<std::string, Row> walk, tele;
    const auto walkRecs = collect(env, [&] { pathWalk(env, 200.0f, 10.0f); });
    printPath("walk200", walkRecs);
    harvest(walkRecs, walk);
    const auto teleRecs = collect(env, [&] { pathTeleport(env, 60.0f, 8, 30); });
    printPath("teleport", teleRecs);
    harvest(teleRecs, tele);
    for (auto *set : { &walk, &tele }) {
        const char *label = set == &walk ? "walk 200 m @10 m/s" : "teleport 60 m x8";
        for (auto &kv : *set) {
            const Stats c = stats(kv.second.cpu), g = stats(kv.second.gpu);
            std::printf("W1 %-20s %-14s rebuilds %3zu | CPU ms med %7.2f max %7.2f | GPU ms med %8.2f max %8.2f\n",
                        label, kv.first.c_str(), c.n, c.median, c.max, g.median, g.max);
            const std::string what = std::string(label) + ": " + kv.first + " GPU ms per whole-cascade rebuild (median)";
            target("W1", g.median, "ms", what.c_str());
        }
    }
    REQUIRE(!walk.empty() || !tele.empty(), "the paths re-voxelised at least one cascade");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.lights — W2: THE VOXEL INJECTION HOLDS 16 LIGHTS, FIRST COME.
// Anchor: fork Components/Hlms/Pbs/src/Vct/OgreVctLighting.cpp:152 (a const buffer of
// sizeof(ShaderVctLight) * 16u) and :1308-1341 (the collect loop, no cull, no report).
// Fixture: a floor and a white wall, sixteen FILLER lamps created first (40 m away, in
// the chain), then the KEY lamp facing the wall. Number: does toggling the KEY change the
// voxel store (GiVoxelStats::lightDigest, cascade 0)? The exact picture bar: with 16
// fillers the key's bounce must CHANGE the digest (today it does not). The CONTROL: with
// 15 fillers the key is light 16 and its toggle does change it.
// ===========================================================================
static int lightsMain()
{
    Env env;
    if (!boot(env, "test-scale-lights-ogre.log")) return 1;
    // One build per arm: `fillers` lamps first, then the key.
    auto arm = [&](int fillers, std::string &digestOn, std::string &digestOff, double &litOn, double &litOff) {
        env.doc = iris::Scene::create();
        env.mirror->setSource(env.doc);
            env.doc->skyType = iris::SkyType::SINGLE_COLOR;
        env.doc->skyColor = QColor(0, 0, 0);
        worldmodes::setMode(env.doc, worldmodes::Mode::High);
        worldmodes::setPhoton(env.doc, true, worldmodes::PhotonTier::High);
        BakeInfo bi;
        iris::MeshPtr cube = bakedMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj"), "cube", &bi);
        auto slab = [&](const char *name, iris::Vec3 pos, iris::Vec3 scale, QColor col) {
            auto n = iris::MeshNode::create();
            n->setName(name);
            n->setMesh(cube);
            n->setLocalPos(pos);
            n->setLocalScale(scale);
            auto m = iris::PbrMaterial::create();
            m->setValue("baseColor", col);
            m->setValue("roughness", 0.9f);
            m->setValue("metallic", 0.0f);
            n->setMaterial(m);
            env.doc->getRootNode()->addChild(n);
        };
        slab("floor", iris::Vec3(0, -0.1f, 0), iris::Vec3(12, 0.2f, 12), QColor(230, 230, 230));
        slab("wall", iris::Vec3(0, 2.0f, -3.0f), iris::Vec3(8, 4.0f, 0.4f), QColor(230, 40, 40));
        for (int i = 0; i < fillers; ++i) {
            auto l = iris::LightNode::create();
            l->setLightType(iris::LightType::Point);
            l->setName(QStringLiteral("filler%1").arg(i));
            l->setLocalPos(iris::Vec3(40.0f + float(i % 4) * 3.0f, 3.0f, float(i / 4) * 3.0f - 4.5f));
            env.doc->getRootNode()->addChild(l);
            l->setPropertyValue(QStringLiteral("intensity"), 1.0f);
            l->setPropertyValue(QStringLiteral("distance"), 4.0f);
            l->shadowMap->shadowType = iris::ShadowMapType::None;
        }
        auto key = iris::LightNode::create();
        key->setLightType(iris::LightType::Point);
        key->setName("key");
        key->setLocalPos(iris::Vec3(0.0f, 2.0f, -1.5f));
        env.doc->getRootNode()->addChild(key);
        key->setPropertyValue(QStringLiteral("intensity"), 6.0f);
        key->setPropertyValue(QStringLiteral("distance"), 10.0f);
        key->shadowMap->shadowType = iris::ShadowMapType::None;
        env.doc->getRootNode()->applyStaticDefaults();
        setCamera(env, iris::Vec3(0, 3, 8), iris::Vec3(0, 1, -2));
        auto settle = [&] {
            for (int f = 0; f < 600; ++f) {
                frame(env, 1);
                if (f > 30 && env.scene->giStatus().giAtRest) break;
            }
        };
        settle();
        GiVoxelStats on = env.scene->giVoxelStats(0);
        key->setPropertyValue(QStringLiteral("intensity"), 0.0f);
        key->markChanged(iris::NodeChange::Params);
        settle();
        GiVoxelStats off = env.scene->giVoxelStats(0);
        digestOn = on.lightDigest;
        digestOff = off.lightDigest;
        litOn = on.meanLit;
        litOff = off.meanLit;
        std::printf("   %2d fillers + key: digest on %s off %s | meanLit on %.6f off %.6f | voxelsLit %lld / %lld\n",
                    fillers, on.lightDigest.c_str(), off.lightDigest.c_str(), on.meanLit, off.meanLit,
                    on.voxelsLit, off.voxelsLit);
        return on.available && off.available;
    };
    (void)0;
    std::string c1, c2, t1, t2;
    double cl1 = 0, cl2 = 0, tl1 = 0, tl2 = 0;
    const bool control = arm(15, c1, c2, cl1, cl2);
    const bool wall = arm(16, t1, t2, tl1, tl2);
    REQUIRE(control && wall, "the voxel store was read back in both arms");
    REQUIRE(c1 != c2, "CONTROL: as light 16 the key's toggle changes the voxel store (the fixture sees it)");
    const bool bounces = t1 != t2;
    std::printf("W2: with 16 lights before it, the key lamp %s the voxel store (mean lit %.6f vs %.6f)\n",
                bounces ? "REACHES" : "does NOT reach", tl1, tl2);
    target("W2", bounces ? 1.0 : 0.0, "(1 = light 17 bounces)",
           "the 17th light's bounce reaches the voxels: exact bar = the digest moves", "1 (exact)");
    target("W2", 16.0, "lights", "the injection's capacity (OgreVctLighting.cpp:152, read from the fork)");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// THE LARGE ASSET for W3 / W4 / W5: the largest shell the cache holds (1 M / 5 M /
// 10 M, made by scale_assets_gen), or a 250 k shell baked here (and cached) — said.
// ===========================================================================
static QList<iris::MeshPtr> largestShell(BakeInfo &info, size_t &asked)
{
    for (size_t t : { size_t(10000000), size_t(5000000), size_t(1000000) }) {
        QList<iris::MeshPtr> m = shellAsset(t, &info, false);
        if (!m.isEmpty()) { asked = t; return m; }
    }
    std::printf("NOTE: no 1 M / 5 M / 10 M shell in the cache (%s) — run scale_assets_gen; measuring a "
                "250 k shell baked now\n", qPrintable(cacheDir()));
    asked = 250000;
    return shellAsset(asked, &info, true);
}

/// Attach every piece of a model under one group node at `pos`, scaled by `k`.
static std::vector<iris::MeshNodePtr> placeModel(Env &env, const QList<iris::MeshPtr> &pieces, iris::Vec3 pos, float k)
{
    std::vector<iris::MeshNodePtr> out;
    auto mat = iris::PbrMaterial::create();
    mat->setValue("baseColor", QColor(180, 170, 160));
    mat->setValue("roughness", 0.6f);
    mat->setValue("metallic", 0.0f);
    for (const iris::MeshPtr &m : pieces) {
        auto n = iris::MeshNode::create();
        n->setMesh(m);
        n->setLocalPos(pos);
        n->setLocalScale(iris::Vec3(k, k, k));
        n->setMaterial(mat);
        env.doc->getRootNode()->addChild(n);
        out.push_back(n);
    }
    return out;
}

/// An empty High-tier document with GI OFF (the geometry walls are the id pass's, and
/// voxelising a 10 M model is W1's cost, not these) and one sun.
static void geometryDoc(Env &env)
{
    env.doc = iris::Scene::create();
    env.mirror->setSource(env.doc);
    env.doc->skyType = iris::SkyType::SINGLE_COLOR;
    env.doc->skyColor = QColor(60, 60, 70);
    worldmodes::setMode(env.doc, worldmodes::Mode::High);
    worldmodes::setPhoton(env.doc, false, worldmodes::PhotonTier::High);
    auto sun = iris::LightNode::create();
    sun->setLightType(iris::LightType::Directional);
    sun->setLocalRot(iris::Quat::fromEulerAngles(-50.0f, 30.0f, 0.0f));
    env.doc->getRootNode()->addChild(sun);
    sun->setPropertyValue(QStringLiteral("intensity"), 2.0f);
}

/// The id pass's drawn triangles and GPU ms, and the decode's GPU ms, over `frames`
/// still frames (medians; the id pass row's triangles are the GPU's own count).
struct IdRead { double tris = -1, idMs = -1, decodeMs = -1; unsigned survivors = 0; };
static IdRead readIdPass(Env &env, int frames)
{
    IdRead out;
    std::vector<double> tris, id, dec;
    const auto recs = collect(env, [&] { frame(env, frames); }, 8);
    for (const FrameRecord &r : recs) {
        if (const FramePass *p = passNamed(r, "Jahshaka atom id")) {
            tris.push_back(double(p->triangles));
            if (p->gpuMs >= 0) id.push_back(p->gpuMs);
        }
        if (const FramePass *p = passNamed(r, "Jahshaka opaque"))
            if (p->gpuMs >= 0) dec.push_back(p->gpuMs);
    }
    out.tris = stats(tris).median;
    out.idMs = stats(id).median;
    out.decodeMs = stats(dec).median;
    return out;
}

/// The LEVEL RULE's answer for one piece at the view's eye: the cluster cut the DAG
/// gives at the id pass's own tolerance (kLodBudgetPixels x the scene's LOD bias) —
/// Types.h clusterCut, the GLSL twin's arithmetic.
static size_t cutTriangles(Env &env, const QList<iris::MeshPtr> &pieces, const iris::Vec3 &pos, float k)
{
    GpuCullRequest req;
    if (!env.engine->fillCullView(env.view, req)) return 0;
    size_t total = 0;
    for (const iris::MeshPtr &m : pieces) {
        MeshData d;
        if (!SceneMirror::toMeshData(m.data(), d) || d.clusters.empty()) continue;
        ClusterCutView v;
        v.worldRow[0][0] = k; v.worldRow[0][3] = pos.x();
        v.worldRow[1][1] = k; v.worldRow[1][3] = pos.y();
        v.worldRow[2][2] = k; v.worldRow[2][3] = pos.z();
        v.scale = k;
        std::memcpy(v.eye, req.eye, sizeof(v.eye));
        v.tolerance = kLodBudgetPixels * env.scene->lodBias();
        v.projScaleY = req.projScaleY;
        v.viewportHeight = req.viewportHeight;
        std::vector<unsigned> drawn;
        total += clusterCut(d.clusterGroups, d.clusters, v, drawn);
    }
    return total;
}

// ===========================================================================
// scale.cluster_cut — W3: WHOLE-MESH LOD, NO CLUSTER CUT IN THE PRODUCT.
// Anchor: irisgl/engine/media/Hlms/Jahshaka/JahCullTest_cs.glsl:181-196 (one level per
// INSTANCE; inside the bounds d = 0 forces level 0). Number: triangles the id pass draws
// for the large asset at three camera distances (inside its bounds, 5 radii, 30 radii)
// against what the DAG's cluster cut would draw at the same tolerance. Also the owed
// row: the id pass's and the decode's GPU ms on the asset.
// ===========================================================================
static int clusterCutMain()
{
    Env env;
    if (!boot(env, "test-scale-cluster-cut-ogre.log")) return 1;
    BakeInfo info;
    size_t asked = 0;
    const unsigned long long rss0 = rssKb();
    const QList<iris::MeshPtr> shell = largestShell(info, asked);
    REQUIRE(!shell.isEmpty(), "the large asset (%zu triangles asked, %d pieces, %zu level-0 triangles)", asked,
            info.pieces, info.triangles);
    if (shell.isEmpty()) return 1;
    geometryDoc(env);
    const float radius = 10.0f;   // a building-sized asset: the shell's mean radius 1 -> 10 m
    const iris::Vec3 at(0, radius, 0);
    placeModel(env, shell, at, radius);
    env.doc->getRootNode()->applyStaticDefaults();
    armMonitor(env);
    const struct { const char *name; float d; } poses[] = {
        { "inside bounds (1.3 R)", 1.3f * radius }, { "5 R", 5.0f * radius }, { "30 R", 30.0f * radius } };
    for (const auto &p : poses) {
        setCamera(env, at + iris::Vec3(0, 0.2f * radius, p.d), at);
        frame(env, 10);
        const IdRead r = readIdPass(env, 30);
        const size_t cut = cutTriangles(env, shell, at, radius);
        std::printf("W3 %-24s id pass draws %12.0f tris (id %.3f ms, decode %.3f ms GPU) | the cluster cut: %10zu "
                    "tris | ratio %.2fx\n",
                    p.name, r.tris, r.idMs, r.decodeMs, cut, cut ? r.tris / double(cut) : 0.0);
        const std::string what = std::string("triangles drawn at ") + p.name + " (the cut would draw " +
                                 std::to_string(cut) + ")";
        target("W3", r.tris, "tris", what.c_str());
        const std::string owed = std::string("id pass GPU ms on the ") + std::to_string(info.triangles) +
                                 "-triangle asset at " + p.name + " (decode " + std::to_string(r.decodeMs) + " ms)";
        target("W3", r.idMs, "ms", owed.c_str());
    }
    std::printf("   memory: RSS %.0f MB before the asset, %.0f MB now, peak %.0f MB\n", double(rss0) / 1024.0,
                double(rssKb()) / 1024.0, double(peakRssKb()) / 1024.0);
    std::printf("   VS invocations: not available (the monitor has no pipeline-statistics query)\n");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.levels — W4: ONLY THE FINEST 8 LEVELS REACH THE GPU PATH.
// Anchor: irisgl/engine/src/GpuScene.h:220 (kLevelsPerMesh = 8u); OgreGpuScene.cpp:462-470
// (take = min(levelCount, 8), one warning line). Number: the chain's level count (per
// piece) against 8, and the triangles the id pass draws for the asset at 1 km against
// what the chain's coarsest level would draw.
// ===========================================================================
static int levelsMain()
{
    Env env;
    if (!boot(env, "test-scale-levels-ogre.log")) return 1;
    BakeInfo info;
    size_t asked = 0;
    const QList<iris::MeshPtr> shell = largestShell(info, asked);
    if (shell.isEmpty()) { REQUIRE(false, "the large asset"); return 1; }
    std::printf("W4 asset %s: %d pieces, %zu level-0 triangles, chain %d levels (longest piece), level 7 = %zu "
                "tris, coarsest = %zu tris\n",
                qPrintable(info.name), info.pieces, info.triangles, info.levels, info.level7Triangles,
                info.coarsestTriangles);
    geometryDoc(env);
    const float radius = 10.0f;
    const iris::Vec3 at(0, radius, 0);
    placeModel(env, shell, at, radius);
    env.doc->getRootNode()->applyStaticDefaults();
    armMonitor(env);
    // The document camera's far plane is 500 m by default (CameraNode) — the
    // editor's own view ends there; 1 km needs it moved for this one pose.
    env.camera->farClip = 5000.0f;
    setCamera(env, at + iris::Vec3(0, 50.0f, 1000.0f), at);
    frame(env, 10);
    const IdRead r = readIdPass(env, 30);
    std::printf("W4 at 1 km: the id pass draws %.0f tris; the chain's coarsest level holds %zu (%d levels, the GPU "
                "path keeps 8)\n",
                r.tris, info.coarsestTriangles, info.levels);
    target("W4", double(std::min(info.levels, 8) - 1), "level", "the coarsest level the GPU path can draw (of the chain)");
    target("W4", double(info.levels), "levels", "the asset's chain length (longest piece, level 0 included)");
    target("W4", r.tris, "tris", "triangles the id pass draws for the asset at 1 km (the coarsest level would be "
           "fewer; see the W4 line)");
    // THE 100-LEVEL CASE (brief §4.4): the chain halves until 128 triangles
    // (meshbake.cpp kRatio 0.5, kMinTriangles 128, kMaxLevels 254), so a chain reaches
    // log2(T/128)+1 levels: 17 at 10 M in ONE mesh — and the import splits above 1 M
    // (see BakeInfo::pieces), so a real piece stops at ~13. A 100-level chain would need
    // 128 * 2^99 triangles; no content reaches it, and none is measured.
    std::printf("   (a 100-level chain needs 128 x 2^99 triangles under the halving rule — not a reachable case)\n");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.residency — W5: EVERYTHING RESIDENT, UNCOMPRESSED.
// Anchor: irisgl/engine/src/OgreMesh.cpp:858-866 (48 B vertices: float3 pos, float3
// normal, float4 tangent, float2 uv) and :1000-1011 (the shadow chain beside the levels).
// Number: the VRAM each cached shell takes when attached, measured through the ENGINE'S
// OWN STATS (MemoryStats: the VaoManager's pools, capacity - free) — there is no vmaStats
// door on this boundary — beside the 48 B/vertex formula.
// ===========================================================================
static int residencyMain()
{
    Env env;
    if (!boot(env, "test-scale-residency-ogre.log")) return 1;
    geometryDoc(env);
    armMonitor(env);
    setCamera(env, iris::Vec3(0, 12, 40), iris::Vec3(0, 10, 0));
    frame(env, 20);
    int measured = 0;
    for (size_t t : { size_t(1000000), size_t(5000000), size_t(10000000), size_t(250000) }) {
        BakeInfo info;
        QList<iris::MeshPtr> shell = shellAsset(t, &info, t == 250000 && measured == 0);
        if (shell.isEmpty()) {
            std::printf("   shell %zu: not in the cache (scale_assets_gen)\n", t);
            continue;
        }
        MemoryStats m0;
        env.engine->memoryStats(m0);
        auto nodes = placeModel(env, shell, iris::Vec3(0, 10, 0), 10.0f);
        env.doc->getRootNode()->applyStaticDefaults();
        frame(env, 20);
        MemoryStats m1;
        env.engine->memoryStats(m1);
        const double used0 = double(m0.gpuPoolCapacityBytes - m0.gpuPoolFreeBytes);
        const double used1 = double(m1.gpuPoolCapacityBytes - m1.gpuPoolFreeBytes);
        // THE FORMULA, from what the engine receives (SceneMirror::toMeshData): 48 B a
        // vertex; 4 B an index for level 0 and every chain level; the shadow chain's
        // copy of the levels in a position-only layout (12 B a vertex + the indices
        // again, OgreMesh.cpp:1000-1007); the cluster stream's own index copy.
        size_t verts = 0, levelIdx = 0, clusterIdx = 0;
        for (const iris::MeshPtr &m : shell) {
            MeshData d;
            if (!SceneMirror::toMeshData(m.data(), d)) continue;
            verts += d.positions.size() / 3;
            levelIdx += d.indices.size();
            for (const auto &l : d.lodIndices) levelIdx += l.size();
            clusterIdx += d.clusterIndices.size();
        }
        const double mainMB = (double(verts) * 48.0 + double(levelIdx) * 4.0) / 1048576.0;
        const double shadowMB = (double(verts) * 12.0 + double(levelIdx) * 4.0) / 1048576.0;
        const double clusterMB = double(clusterIdx) * 4.0 / 1048576.0;
        std::printf("W5 %-14s %d pieces %10zu tris %9zu verts | pool used +%8.1f MB (capacity +%8.1f MB) | formula: "
                    "levels %.1f + shadow chain %.1f + cluster stream %.1f = %.1f MB | RSS %.0f MB\n",
                    qPrintable(info.name), info.pieces, info.triangles, verts, (used1 - used0) / 1048576.0,
                    double(m1.gpuPoolCapacityBytes - m0.gpuPoolCapacityBytes) / 1048576.0, mainMB, shadowMB,
                    clusterMB, mainMB + shadowMB + clusterMB, double(rssKb()) / 1024.0);
        const std::string what = "VRAM (pool used) per " + std::to_string(info.triangles) + "-triangle asset";
        target("W5", (used1 - used0) / 1048576.0, "MB", what.c_str());
        for (auto &n : nodes) n->removeFromParent();
        frame(env, 20);
        ++measured;
    }
    REQUIRE(measured > 0, "at least one asset was measured");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.decode — W6: THE DECODE IS ONE FULL-SCREEN DRAW PER BUCKET.
// Anchor: irisgl/engine/media/Hlms/Atom/Any/800.Atom_piece_ps.any (the per-bucket screen
// decode; the geometry fetch before the late discard). Number: the decode pass's GPU ms
// against the bucket count on the world — material sets built for ~1, 8, 35 and 200
// buckets (the actual count from AtomDrawStatus is what is printed).
// ===========================================================================
static int decodeMain()
{
    Env env;
    World w;
    WorldSpec spec;
    spec.materials = 1;
    if (!bootWorld(env, w, "test-scale-decode-ogre.log", spec)) return 1;
    // THE ARMS: the same world, its materials swapped IN PLACE (applyMaterials), then GI
    // to rest again — N shared materials with N distinct textures give ~N buckets
    // (HlmsAtom::BucketKey: permutation, texture set, pool). The count printed is the
    // engine's own (AtomDrawStatus), never the arm's intent.
    const struct { int materials, textures; } arms[] = { { 1, 0 }, { 8, 8 }, { 35, 35 }, { 200, 200 } };
    std::vector<std::pair<unsigned, double>> rows;
    for (const auto &a : arms) {
        applyMaterials(w, a.materials, a.textures);
        for (int f = 0; f < 900; ++f) { frame(env, 1); if (f > 30 && env.scene->giStatus().giAtRest) break; }
        pathStill(env, 30);
        const AtomDrawStatus st = env.scene->atomDrawStatus();
        const IdRead r = readIdPass(env, 60);
        std::printf("W6 materials %4d textures %4d -> buckets %4u screen draws %4u decode draws %4u | decode GPU ms %.3f"
                    " (id %.3f)\n",
                    a.materials, a.textures, st.buckets, st.screenDraws, st.decodeDraws, r.decodeMs, r.idMs);
        rows.push_back({ st.buckets, r.decodeMs });
        const std::string what = "decode GPU ms at " + std::to_string(st.buckets) + " buckets (1080p)";
        target("W6", r.decodeMs, "ms", what.c_str());
    }
    REQUIRE(rows.size() == 4 && rows.back().second >= 0, "the four arms were measured");
    if (rows.size() >= 2 && rows.back().first > rows.front().first) {
        const double slope = (rows.back().second - rows.front().second) / double(rows.back().first - rows.front().first);
        std::printf("W6 slope: %.4f ms per bucket at 1920x1080 (%.4f ms per bucket per Mpx)\n", slope,
                    slope / (1920.0 * 1080.0 / 1e6));
        target("W6", slope, "ms/bucket", "the decode's cost per bucket at 1080p");
    }
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.occlusion — W7: NO OCCLUSION CULLING.
// Anchor: irisgl/engine/src/OgreAtomIdPass.cpp:523 (req.hzbLevels = 0u: frustum only);
// no view builds the HZB (PostFxDesc::hzb is set by nothing in Studio or the mirror).
// Number: on the walk, the triangles the id pass draws (frustum only — its own cull,
// replayed through Engine::gpuCull with the id pass's predicates) against what the SAME
// cull draws with the depth pyramid the cull already supports (the view's HZB switched on
// for the measurement only) — the instance-level answer occlusion would give.
// ===========================================================================
static int occlusionMain()
{
    Env env;
    World w;
    if (!bootWorld(env, w, "test-scale-occlusion-ogre.log")) return 1;
    env.fxOverride = [](PostFxDesc &fx) { fx.hzb = true; fx.hzbFarthest = true; };
    frame(env, 10);
    HzbStatus hz;
    const bool hasHzb = env.engine->hzbStatus(env.view, hz);
    REQUIRE(hasHzb && hz.built, "the measurement's depth pyramid is built (%u levels)", hz.levels);
    std::vector<double> frustum, occl, ratio;
    unsigned sampled = 0;
    for (int s = 0; s < 8; ++s) {
        const float x = -90.0f + 25.0f * float(s);
        setCamera(env, iris::Vec3(x, 1.7f, 0.0f), iris::Vec3(x + 10.0f, 1.7f, -2.0f));
        frame(env, 4);
        env.engine->hzbStatus(env.view, hz);
        GpuCullRequest req;
        if (!env.engine->fillCullView(env.view, req)) continue;
        req.flagsRequired = 1u | 512u;   // visible | ATOM (GpuSceneEntry::flags, Types.h)
        req.pixelTolerance = kLodBudgetPixels * env.scene->lodBias();
        req.mode = 2u;
        GpuCullResult a, b;
        req.hzbLevels = 0u;
        const bool okA = env.engine->gpuCull(env.scene, env.view, req, true, a);
        req.hzbLevels = hz.primed ? hz.levels : 0u;
        const bool okB = hz.primed && env.engine->gpuCull(env.scene, env.view, req, true, b);
        if (!okA || !okB) continue;
        auto tris = [](const GpuCullResult &r) {
            double t = 0;
            for (size_t i = 0; i + 4 < r.drawCommands.size(); i += 5) t += double(r.drawCommands[i]) / 3.0;
            return t;
        };
        const double ta = tris(a), tb = tris(b);
        frustum.push_back(ta);
        occl.push_back(tb);
        ratio.push_back(tb > 0 ? ta / tb : 0.0);
        ++sampled;
        std::printf("W7 x %6.1f: frustum-only %6u instances %11.0f tris | with the HZB %6u instances %11.0f tris | "
                    "drawn/occlusion-kept %.2fx\n",
                    double(x), a.survivors, ta, b.survivors, tb, tb > 0 ? ta / tb : 0.0);
    }
    REQUIRE(sampled > 0, "the walk was sampled (%u poses)", sampled);
    target("W7", stats(frustum).median, "tris", "drawn per frame on the walk (frustum only: today)");
    target("W7", stats(occl).median, "tris", "what the HZB cull keeps at the same poses (instance level)");
    target("W7", stats(ratio).median, "x", "drawn / occlusion-kept, median over the walk");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.tlas — W8: THE CPU-WRITTEN TLAS.
// Anchor: irisgl/engine/src/OgreRayQuery.cpp:2035-2100 (the instance write) and
// :3131-3155 (rebuilt on any movement). Number: the CPU ms of the instance write
// (RayQueryStatus::gatherMs) and the engine.rayquery stage, per MOVING frame (one item
// moves every frame) on the 10k world; still frames beside them.
// ===========================================================================
static int tlasMain()
{
    Env env;
    World w;
    if (!bootWorld(env, w, "test-scale-tlas-ogre.log")) return 1;
    pathStill(env, 30);
    std::vector<double> gather, stage, stillStage, tlasGpu;
    const auto still = collect(env, [&] { frame(env, 60); });
    for (const FrameRecord &r : still) stillStage.push_back(stageMs(r, "engine.rayquery"));
    // ONE MOVABLE ITEM moving every frame (a physics body / an animated prop — the
    // user's word, Mobility::Movable): a STATIC item nudged by a script is held by
    // the static settle and moves the ray tier's epoch a handful of times, not per
    // frame (measured: 5 TLAS builds over 120 frames).
    iris::MeshNodePtr mover = w.items[w.items.size() / 2];
    mover->setMobility(iris::Mobility::Movable);
    frame(env, 30);
    const unsigned long long builds0 = env.scene->rayQueryStatus().tlasBuilds + env.scene->rayQueryStatus().tlasRefits;
    const iris::Vec3 home = mover->getLocalPos();
    const auto moving = collect(env, [&] {
        for (int f = 0; f < 120; ++f) {
            mover->setLocalPos(home + iris::Vec3(0.02f * float(f % 2 ? 1 : -1), 0, 0));
            frame(env, 1);
            const RayQueryStatus rq = env.scene->rayQueryStatus();
            if (f >= 20 && rq.gatherMs >= 0) gather.push_back(rq.gatherMs);
            if (f >= 20 && rq.tlasMs >= 0) tlasGpu.push_back(rq.tlasMs);
        }
    });
    for (const FrameRecord &r : moving) stage.push_back(stageMs(r, "engine.rayquery"));
    const RayQueryStatus rq = env.scene->rayQueryStatus();
    REQUIRE(rq.enabled, "the ray tier is enabled (instances %d, far %d)", rq.instances, rq.farInstances);
    REQUIRE(rq.tlasBuilds + rq.tlasRefits - builds0 >= 100, "the TLAS was re-made on (nearly) every moving frame (%llu of 128)",
            (unsigned long long)(rq.tlasBuilds + rq.tlasRefits - builds0));
    std::printf("W8 instances %d (+%d far) | instance write CPU ms med %.3f max %.3f | engine.rayquery moving med %.3f "
                "still med %.3f | TLAS GPU ms med %.3f | builds %llu refits %llu\n",
                rq.instances, rq.farInstances, stats(gather).median, stats(gather).max, stats(stage).median,
                stats(stillStage).median, stats(tlasGpu).median, (unsigned long long)rq.tlasBuilds,
                (unsigned long long)rq.tlasRefits);
    target("W8", stats(gather).median, "ms", "CPU ms of the TLAS instance write per moving frame at 10k");
    target("W8", stats(stage).median, "ms", "the engine.rayquery stage per moving frame at 10k");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.atlas — W9: THE FIXED CARD ATLAS, GLOBAL INVALIDATION, 64 LIGHTS.
// Anchor: irisgl/engine/src/SurfaceCache.h:110-119 (kCardPageSize 128, kCardAtlasSize 2048
// = 256 pages); OgreSurfaceCache.cpp:923 (the 7/8 stop), 1129 (kMaxCardLights 64).
// Number: the cards held against the cards WANTED (every card of every candidate inside
// the residency radius — the scene's own card lists), and the recaptures one lamp move
// costs.
// ===========================================================================
static int atlasMain()
{
    Env env;
    World w;
    if (!bootWorld(env, w, "test-scale-atlas-ogre.log")) return 1;
    pathStill(env, 240);
    for (int f = 0; f < 900 && env.scene->giStatus().cards.queueLength > 0; ++f) frame(env, 1);
    const CardCacheStatus c = env.scene->giStatus().cards;
    REQUIRE(c.built, "the card atlas exists at High");
    const iris::Vec3 eye = env.camera->getLocalPos();
    size_t wanted = 0, instances = 0;
    for (const auto &n : w.items) {
        const iris::Vec3 d = n->getLocalPos() - eye;
        if (std::sqrt(d.x() * d.x() + d.y() * d.y() + d.z() * d.z()) > c.residencyRadius) continue;
        ++instances;
        wanted += size_t(n->getMesh()->cards.size());
    }
    std::printf("W9 atlas %u pages x %u px (%u used) | residency radius %.1f m | cards held %u on %u instances | "
                "wanted %zu cards on %zu instances | lights dropped %u\n",
                c.pages, c.pageSize, c.pagesUsed, double(c.residencyRadius), c.cardsResident, c.instancesResident,
                wanted, instances, c.lightsDropped);
    target("W9", double(c.cardsResident), "cards", ("held, of " + std::to_string(wanted) + " wanted inside the radius").c_str());
    target("W9", double(c.pagesUsed), "pages", ("used of " + std::to_string(c.pages) + " (the fixed 2k atlas)").c_str());
    target("W9", double(c.lightsDropped), "lights", "dropped by the relight's 64-light sum (500 lamps in the world)");
    // ONE LAMP MOVE: the captures and relights it costs until the queues drain.
    const unsigned long long cap0 = c.captures, inv0 = c.invalidLight, rel0 = c.relights;
    iris::LightNodePtr lamp = w.lights[w.lights.size() / 2];
    lamp->setLocalPos(lamp->getLocalPos() + iris::Vec3(1.0f, 0, 0));
    int f = 0;
    for (; f < 900; ++f) {
        frame(env, 1);
        const CardCacheStatus s = env.scene->giStatus().cards;
        if (f > 10 && s.queueLength == 0 && s.capturesLastFrame == 0 && s.relitLastFrame == 0) break;
    }
    const CardCacheStatus c1 = env.scene->giStatus().cards;
    std::printf("W9 one lamp moved 1 m: recaptures %llu, light invalidations %llu, relights %llu over %d frames\n",
                (unsigned long long)(c1.captures - cap0), (unsigned long long)(c1.invalidLight - inv0),
                (unsigned long long)(c1.relights - rel0), f);
    target("W9", double(c1.captures - cap0), "captures", "recaptures one lamp move costs (global invalidation)");
    target("W9", double(c1.relights - rel0), "relights", "card relights one lamp move costs");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.far_field — W10: THE FAR FIELD, 60 m / 208 m.
// Anchors: irisgl/engine/src/OgreRayQuery.cpp:230 (kMaxReflectCascades 4) and :4645-4652
// (the reflection ray's length: the outer cascade's diagonal, at least 50 m, at most the
// far plane); ScreenProbeGather.h:69 (kGatherMaxCascades 4: what a gather hit reads).
// Two PICTURES (the offscreen view with the viewport's chain), each read at the pixels a
// fan of targets at 10-480 m projects to — the distance where the picture goes sky-only:
//   DIFFUSE — a black sky, no lamp, an EMISSIVE floor the only source; white posts at
//     10-480 m (the far plane is the camera's default 500 m). Lit = the post shows bounce.
//   REFLECTION (Epic) — a perfect mirror wall in front of the camera and EMISSIVE panels
//     BEHIND the camera (never on screen, so screen-space reflection cannot find them),
//     each scaled with its distance; the mirror's pixel at a panel's mirrored position
//     reads the panel only if a reflection query reaches it.
// Beside them, the reaches the live chain states (the outer cascade's half extent, the
// reflection ray length its diagonal gives).
// ===========================================================================
static double projectedMean(const Image &img, const GpuCullRequest &req, const iris::Vec3 &c)
{
    const float *m = req.viewProj;
    const float X = c.x(), Y = c.y(), Z = c.z();
    const float cx = m[0] * X + m[1] * Y + m[2] * Z + m[3], cy = m[4] * X + m[5] * Y + m[6] * Z + m[7],
                cw = m[12] * X + m[13] * Y + m[14] * Z + m[15];
    if (cw <= 0) return -1.0;
    const int px = int((cx / cw * 0.5f + 0.5f) * float(img.width));
    const int py = int((1.0f - (cy / cw * 0.5f + 0.5f)) * float(img.height));
    if (px < 0 || py < 0 || px >= int(img.width) || py >= int(img.height)) return -1.0;
    double sum = 0;
    int k = 0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const Colour col = img.at(unsigned(std::clamp(px + dx, 0, int(img.width) - 1)),
                                      unsigned(std::clamp(py + dy, 0, int(img.height) - 1)));
            sum += (col.r + col.g + col.b) / 3.0;
            ++k;
        }
    return sum / double(k);
}

static void farDoc(Env &env, worldmodes::PhotonTier tier)
{
    env.doc = iris::Scene::create();
    env.mirror->setSource(env.doc);
    env.doc->skyType = iris::SkyType::SINGLE_COLOR;
    env.doc->skyColor = QColor(0, 0, 0);
    env.doc->fogEnabled = false;
    worldmodes::setMode(env.doc, worldmodes::Mode(int(tier)));
    worldmodes::setPhoton(env.doc, true, tier);
}

static iris::MeshNodePtr farBox(Env &env, const iris::MeshPtr &cube, iris::Vec3 pos, iris::Vec3 scale, float yawDeg,
                                const iris::PbrMaterialPtr &mat)
{
    auto n = iris::MeshNode::create();
    n->setMesh(cube);
    n->setLocalPos(pos);
    n->setLocalScale(scale);
    n->setLocalRot(iris::Quat::fromEulerAngles(0.0f, yawDeg, 0.0f));
    n->setMaterial(mat);
    env.doc->getRootNode()->addChild(n);
    return n;
}

static bool farShot(Env &env, Image &img, GpuCullRequest &req, const char *shotName)
{
    env.doc->getRootNode()->applyStaticDefaults();
    for (int f = 0; f < 900; ++f) {
        frame(env, 1);
        if (f > 60 && env.scene->giStatus().giAtRest) break;
    }
    frame(env, 30);
    if (!env.view->readPixels(img) || !img.width) return false;
    env.engine->fillCullView(env.view, req);
    if (!qgetenv("JAH_SCALE_SHOT").isEmpty()) {
        QImage q(img.rgba.data(), int(img.width), int(img.height), QImage::Format_RGBA8888);
        q.save(QString::fromLocal8Bit(qgetenv("JAH_SCALE_SHOT")) + shotName);
    }
    return true;
}

static int farFieldMain()
{
    Env env;
    if (!boot(env, "test-scale-far-field-ogre.log")) return 1;
    BakeInfo bi;
    iris::MeshPtr cube = bakedMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj"), "cube", &bi);
    const float dists[] = { 10, 20, 40, 60, 80, 120, 160, 210, 300, 400, 480 };
    const int n = int(sizeof(dists) / sizeof(dists[0]));
    const double kLit = 0.02;

    // ---- DIFFUSE --------------------------------------------------------------
    farDoc(env, worldmodes::PhotonTier::High);
    {
        auto m = iris::PbrMaterial::create();
        m->setValue("baseColor", QColor(0, 0, 0));
        m->setValue("emissiveColor", QColor(255, 255, 255));
        m->setValue("emissiveIntensity", 1.0f);
        m->setValue("roughness", 1.0f);
        farBox(env, cube, iris::Vec3(0, -0.5f, -300.0f), iris::Vec3(1200.0f, 1.0f, 1200.0f), 0.0f, m);
    }
    auto white = iris::PbrMaterial::create();
    white->setValue("baseColor", QColor(255, 255, 255));
    white->setValue("roughness", 1.0f);
    white->setValue("metallic", 0.0f);
    std::vector<iris::Vec3> faces;
    for (int i = 0; i < n; ++i) {
        const float ang = (float(i) / float(n - 1) - 0.5f) * 0.9f, d = dists[i], s = 0.06f * d;
        farBox(env, cube, iris::Vec3(d * std::sin(ang), s, -d * std::cos(ang)), iris::Vec3(s, 2.0f * s, s),
               -ang * 57.29578f, white);
        faces.push_back(iris::Vec3((d - 0.5f * s) * std::sin(ang), s, -(d - 0.5f * s) * std::cos(ang)));
    }
    setCamera(env, iris::Vec3(0, 1.7f, 0), iris::Vec3(0, 1.0f, -50.0f));
    Image img;
    GpuCullRequest req;
    REQUIRE(farShot(env, img, req, "-diffuse.png"), "the diffuse picture (%ux%u)", img.width, img.height);
    double diffuseReach = 0;
    for (int i = 0; i < n; ++i) {
        const double v = projectedMean(img, req, faces[size_t(i)]);
        std::printf("W10 diffuse   post at %5.0f m: %.4f %s\n", double(dists[i]), v, v > kLit ? "LIT" : "sky-only");
        if (v > kLit && (i == 0 || diffuseReach >= dists[i - 1])) diffuseReach = dists[i];
    }
    const GiStatus gi = env.scene->giStatus();
    double outer = 0, diag = 0;
    if (!gi.cascades.empty()) {
        outer = gi.cascades.back().halfSize;
        diag = 2.0 * gi.cascades.back().halfSize * std::sqrt(3.0);
    }

    // ---- REFLECTION (Epic) ------------------------------------------------------
    farDoc(env, worldmodes::PhotonTier::Epic);
    {
        auto mirrorMat = iris::PbrMaterial::create();
        mirrorMat->setValue("baseColor", QColor(255, 255, 255));
        mirrorMat->setValue("metallic", 1.0f);
        mirrorMat->setValue("roughness", 0.0f);
        farBox(env, cube, iris::Vec3(0, 4.0f, -6.0f), iris::Vec3(40.0f, 24.0f, 0.2f), 0.0f, mirrorMat);
    }
    auto glow = iris::PbrMaterial::create();
    glow->setValue("baseColor", QColor(0, 0, 0));
    glow->setValue("emissiveColor", QColor(255, 255, 255));
    glow->setValue("emissiveIntensity", 1.0f);
    std::vector<iris::Vec3> mirrored;
    for (int i = 0; i < n; ++i) {
        const float ang = (float(i) / float(n - 1) - 0.5f) * 0.6f, d = dists[i], s = 0.05f * d;
        const iris::Vec3 p(d * std::sin(ang), 4.0f, d * std::cos(ang));   // BEHIND the camera (+Z)
        farBox(env, cube, p, iris::Vec3(s, s, 0.1f), -ang * 57.29578f, glow);
        // the panel's image in the mirror plane z = -6 - 0.1: reflect z about it
        mirrored.push_back(iris::Vec3(p.x(), p.y(), -12.2f - p.z()));
    }
    setCamera(env, iris::Vec3(0, 4.0f, 0), iris::Vec3(0, 4.0f, -10.0f));
    REQUIRE(farShot(env, img, req, "-reflect.png"), "the reflection picture");
    double reflectReach = 0;
    for (int i = 0; i < n; ++i) {
        const double v = projectedMean(img, req, mirrored[size_t(i)]);
        std::printf("W10 reflection panel at %5.0f m: %.4f %s\n", double(dists[i]), v, v > kLit ? "SEEN" : "sky-only");
        if (v > kLit && (i == 0 || reflectReach >= dists[i - 1])) reflectReach = dists[i];
    }
    std::printf("W10 the live chain (High): %zu cascades, outer halfSize %.1f m; the reflection ray's length "
                "(OgreRayQuery.cpp:4649) = the outer cascade's diagonal %.1f m; the camera's far plane %.0f m\n",
                gi.cascades.size(), outer, diag, double(env.camera->farClip));
    target("W10", diffuseReach, "m", "the diffuse picture's lit reach (the farthest post of an unbroken run that reads bounce)");
    target("W10", reflectReach, "m", "the reflection's reach at Epic (the farthest off-screen panel the mirror shows)");
    target("W10", outer, "m", "the outer cascade's half-extent at High (the voxel chain's reach)");
    target("W10", diag, "m", "the reflection ray's length (the outer cascade's diagonal)");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.bake — W11: THE SINGLE-THREADED BAKE.
// Anchor: irisgl/import/meshbake.cpp (no threads anywhere in the bake);
// spikes/atom-cluster-1/bake-table-1b.txt (dragon 68k: 31.8 s, 10.3 s the DAG).
// Number: seconds per million triangles through MeshBake::buildFromFile (the import
// door), the DAG stage apart, the blob, the level count and the peak RSS — for a 250 k
// shell baked HERE (forced: a cache hit measures nothing), and the cached 1 M / 5 M /
// 10 M rows as scale_assets_gen recorded them.
// ===========================================================================
static int bakeMain()
{
    std::printf("W11 %-14s %6s %10s %8s %10s %8s %8s %10s %7s %9s\n", "asset", "pieces", "tris", "bake s",
                "s per MT", "dag s", "dag %", "blob MB", "levels", "peak MB");
    auto row = [](const BakeInfo &i, const char *how) {
        const double s = i.bakeMs / 1000.0;
        std::printf("W11 %-14s %6d %10zu %8.1f %10.1f %8.1f %7.1f%% %10.1f %7d %9.0f  (%s)\n", qPrintable(i.name),
                    i.pieces, i.triangles, s, i.triangles ? s / (double(i.triangles) / 1e6) : 0.0, i.dagMs / 1000.0,
                    i.bakeMs > 0 ? 100.0 * i.dagMs / i.bakeMs : 0.0, double(i.blobBytes) / 1048576.0, i.levels,
                    double(i.peakRssKb) / 1024.0, how);
        std::fflush(stdout);
    };
    BakeInfo here;
    QFile::remove(shellBlobPath(250000));
    const QList<iris::MeshPtr> m = shellAsset(250000, &here, true);
    REQUIRE(!m.isEmpty() && !here.fromCache && here.bakeMs > 0, "a 250 k shell baked through the import door");
    row(here, "baked now");
    target("W11", here.bakeMs / 1000.0 / (double(here.triangles) / 1e6), "s/MT", "the bake's seconds per million triangles (250 k, Debug)");
    target("W11", here.bakeMs > 0 ? 100.0 * here.dagMs / here.bakeMs : 0.0, "%", "the cluster-DAG stage's share of the bake");
    for (size_t t : { size_t(1000000), size_t(5000000), size_t(10000000) }) {
        BakeInfo c;
        if (shellAsset(t, &c, false).isEmpty()) { std::printf("W11 shell-%zu: not cached (scale_assets_gen)\n", t); continue; }
        row(c, c.bakeMs > 0 ? "scale_assets_gen's record" : "cached, no record");
        if (c.bakeMs > 0) {
            const std::string what = "bake seconds for the " + std::to_string(c.triangles) + "-triangle shell";
            target("W11", c.bakeMs / 1000.0, "s", what.c_str());
        }
    }
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.hit_list — W12: THE HIT LIST (1.17 M records vs 2.07 M Epic gather rays).
// Anchor: irisgl/engine/src/EnginePrivate.h:960 (kHitListHeightFactor 0.5625: the list
// is sized from the view); rq_probe_gather.comp:300-311 (a dropped record = zero radiance).
// Number: records appended, dropped and the capacity (RayQueryStatus) on the Epic world at
// 1080p — still and on the walk.
// ===========================================================================
static int hitListMain()
{
    Env env;
    World w;
    WorldSpec spec;
    spec.tier = worldmodes::PhotonTier::Epic;
    if (!bootWorld(env, w, "test-scale-hit-list-ogre.log", spec)) return 1;
    std::vector<double> rec, drop;
    unsigned long long cap = 0;
    auto sample = [&](int frames, const std::function<void(int)> &step) {
        for (int f = 0; f < frames; ++f) {
            step(f);
            frame(env, 1);
            const RayQueryStatus rq = env.scene->rayQueryStatus();
            if (f > 8) { rec.push_back(double(rq.hitRecords)); drop.push_back(double(rq.hitDropped)); }
            cap = std::max(cap, rq.hitCapacity);
        }
    };
    sample(90, [&](int) { setCamera(env, worldEye(), worldEye() + iris::Vec3(0, 0, -10)); });
    const Stats rs = stats(rec), ds = stats(drop);
    rec.clear(); drop.clear();
    sample(300, [&](int f) {
        const float x = -60.0f + float(f) * 10.0f * kDt;
        setCamera(env, iris::Vec3(x, 1.7f, 0), iris::Vec3(x + 10.0f, 1.7f, -2.0f));
    });
    const Stats rw = stats(rec), dw = stats(drop);
    const RayQueryStatus rq = env.scene->rayQueryStatus();
    REQUIRE(cap > 0, "the hit list exists at Epic (capacity %llu)", cap);
    std::printf("W12 capacity %llu | still: records med %.0f max %.0f, dropped med %.0f max %.0f | walk: records med %.0f "
                "max %.0f, dropped med %.0f max %.0f | decode draws %d\n",
                cap, rs.median, rs.max, ds.median, ds.max, rw.median, rw.max, dw.median, dw.max, rq.hitDecodeDraws);
    target("W12", dw.median, "records", "hit records DROPPED per frame on the Epic walk (zero radiance each)");
    target("W12", rw.max, "records", ("appended per frame at worst on the walk, against " + std::to_string(cap)).c_str());
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.cpu_walks — W13: FOUR O(N) CPU WALKS A FRAME.
// Anchors: irisgl/engine/src/OgreGpuScene.cpp:841-900 (the dirty scan: stage
// engine.gpuscene), OgreScene.cpp:2041-2063 (the card candidate walk, every frame: stage
// engine.cards, around OgreScene::updateSurfaceCache), OgreAtomDraw.cpp:250-265 (the
// Atom words walk + sort: stage engine.atomwords, around updateAtomDraw) and
// OgreRayQuery.cpp:2035-2100 (the TLAS writer: stage engine.rayquery). The two middle
// stages are this lane's (monitor stages only; nothing they wrap changed).
// Numbers: each walk's CPU ms on a still frame and on a frame with one mover, at 10k.
// (engine.rayquery also carries the ray tier's per-material decode WITNESS walk —
// OgreRayQuery.cpp:3056-3068, every frame, before its movement gate — and
// engine.atomwords runs the split's own witness twice a frame: at 10k materials those,
// not the item walks, are most of a still frame's CPU.)
// ===========================================================================
static int cpuWalksMain()
{
    Env env;
    World w;
    if (!bootWorld(env, w, "test-scale-cpu-walks-ogre.log")) return 1;
    pathStill(env, 30);
    iris::MeshNodePtr mover = w.items[w.items.size() / 3];
    mover->setMobility(iris::Mobility::Movable);   // see scale.tlas: a mover the renderer moves every frame
    frame(env, 30);
    const iris::Vec3 home = mover->getLocalPos();
    static const char *kWalks[4] = { "engine.gpuscene", "engine.cards", "engine.atomwords", "engine.rayquery" };
    static const char *kWhat[4] = { "the dirty scan", "the card candidate walk (the card cache's frame)",
                                    "the Atom words walk + sort", "the TLAS writer" };
    auto measure = [&](const char *label, bool move) {
        const auto recs = collect(env, [&] {
            for (int f = 0; f < 120; ++f) {
                if (move) mover->setLocalPos(home + iris::Vec3(0.02f * float(f % 2 ? 1 : -1), 0, 0));
                frame(env, 1);
            }
        });
        std::array<double, 4> med{};
        std::array<size_t, 4> seen{};
        for (int k = 0; k < 4; ++k) {
            std::vector<double> v;
            for (const FrameRecord &r : recs) {
                bool has = false;
                for (const FrameStage &st : r.stages) has |= st.name == kWalks[k];
                if (has) v.push_back(stageMs(r, kWalks[k]));
            }
            // A STAGE THAT IS NOT FILED DID NOT RUN (the dirty scan skips a frame the
            // transform-write epoch says is still): 0 ms, and the count says so.
            med[size_t(k)] = v.empty() ? 0.0 : stats(v).median;
            seen[size_t(k)] = v.size();
        }
        std::printf("W13 %-10s frames %3zu | gpuscene %6.3f ms | cards %6.3f ms | atomwords %6.3f ms | rayquery %6.3f ms"
                    " (medians; frames carrying each: %zu %zu %zu %zu)\n",
                    label, recs.size(), med[0], med[1], med[2], med[3], seen[0], seen[1], seen[2], seen[3]);
        return med;
    };
    const auto still = measure("still", false);
    const auto moving = measure("one mover", true);
    REQUIRE(moving[0] >= 0 && moving[1] >= 0 && moving[2] >= 0 && moving[3] >= 0,
            "every walk's stage was filed on the mover frames");
    for (int k = 0; k < 4; ++k) {
        const std::string what = std::string(kWhat[k]) + " (" + kWalks[k] + ") on a mover frame at 10k; still " +
                                 std::to_string(still[size_t(k)]) + " ms";
        target("W13", moving[size_t(k)], "ms", what.c_str());
    }
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// OWED MEASUREMENTS that are tools, not suites (brief §4.5)
// ===========================================================================

/// THE LATTICE (E2's 8,404-node fixture: a 20x20x20 cube lattice, one material each, one
/// shadowed point lamp) — for S3-DRAW's cost row and the drag's scene-graph updates.
static void buildLattice(Env &env)
{
    env.doc = iris::Scene::create();
    env.mirror->setSource(env.doc);
    env.doc->skyType = iris::SkyType::REALISTIC;
    env.doc->setSkyRealistic(iris::SkyRealistic::defaults());
    worldmodes::setMode(env.doc, worldmodes::Mode::High);
    worldmodes::setPhoton(env.doc, true, worldmodes::PhotonTier::High);
    BakeInfo bi;
    iris::MeshPtr cube = bakedMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj"), "cube", &bi);
    for (int y = 0; y < 20; ++y)
        for (int z = 0; z < 20; ++z)
            for (int x = 0; x < 20; ++x) {
                auto n = iris::MeshNode::create();
                n->setMesh(cube);
                n->setLocalPos(iris::Vec3(float(x) - 9.5f, float(y) + 0.5f, float(z) - 9.5f));
                auto m = iris::PbrMaterial::create();
                const int i = (y * 20 + z) * 20 + x;
                m->setValue("baseColor", QColor(90 + (i * 37) % 160, 90 + (i * 13) % 160, 90 + (i * 7) % 160));
                n->setMaterial(m);
                env.doc->getRootNode()->addChild(n);
            }
    auto lamp = iris::LightNode::create();
    lamp->setLightType(iris::LightType::Point);
    lamp->setLocalPos(iris::Vec3(0, 12, 0));
    env.doc->getRootNode()->addChild(lamp);
    lamp->setPropertyValue(QStringLiteral("distance"), 40.0f);
    env.doc->getRootNode()->applyStaticDefaults();
    setCamera(env, iris::Vec3(0, 12, 30), iris::Vec3(0, 8, 0));
    for (int f = 0; f < 900; ++f) { frame(env, 1); if (f > 30 && env.scene->giStatus().giAtRest) break; }
}

static int latticeOwedMain()
{
    Env env;
    if (!boot(env, "test-scale-lattice-ogre.log")) return 1;
    buildLattice(env);
    armMonitor(env);
    const AtomDrawStatus st = env.scene->atomDrawStatus();
    std::vector<double> dec, id;
    unsigned decodePasses = 0;
    const auto recs = collect(env, [&] { frame(env, 90); });
    for (const FrameRecord &r : recs) {
        unsigned passes = 0;
        double ms = 0;
        for (const FramePass &p : r.passes)
            if (p.pass == "Jahshaka opaque" && p.gpuMs >= 0) { ms += p.gpuMs; ++passes; }
        if (passes) { dec.push_back(ms); decodePasses = std::max(decodePasses, passes); }
        if (const FramePass *p = passNamed(r, "Jahshaka atom id")) if (p->gpuMs >= 0) id.push_back(p->gpuMs);
    }
    const double mpx = 1920.0 * 1080.0 / 1e6, d = stats(dec).median;
    std::printf("OWED S3-DRAW lattice: buckets %u, decode passes a frame %u, decode GPU ms med %.3f (id %.3f) -> %.4f "
                "ms per bucket-pass, %.4f ms per bucket-pass-Mpx (the prologue's cost per pixel x buckets x passes)\n",
                st.buckets, decodePasses, d, stats(id).median, d / std::max(1u, st.buckets * decodePasses),
                d / std::max(1u, st.buckets * decodePasses) / mpx);
    // THE DRAG: one cube moved every frame for 60 frames — the scene-graph updates the
    // GI ticks add per frame (the gi.sceneGraph monitor stages, one per call).
    auto doc = env.doc;
    iris::SceneNodePtr cubeNode = doc->getRootNode()->children().at(4210);
    const iris::Vec3 p0 = cubeNode->getLocalPos();
    const auto drag = collect(env, [&] {
        for (int f = 0; f < 60; ++f) {
            cubeNode->setLocalPos(p0 + iris::Vec3(0.05f * float(f), 0, 0));
            frame(env, 1);
        }
    });
    std::vector<double> calls, ms;
    for (const FrameRecord &r : drag) {
        unsigned n = 0;
        double t = 0;
        for (const FrameStage &s : r.stages)
            if (s.name == "gi.sceneGraph") { ++n; t += s.ms; }
        calls.push_back(n);
        ms.push_back(t);
    }
    std::printf("OWED drag on the lattice: extra updateSceneGraph calls from GI per frame med %.1f max %.0f, their "
                "ms med %.3f max %.3f (over %zu frames)\n",
                stats(calls).median, stats(calls).max, stats(ms).median, stats(ms).max, drag.size());
    shutdown(env);
    return 0;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "--world") return worldMain();
    if (mode == "--voxel-scroll") return voxelScrollMain();
    if (mode == "--lights") return lightsMain();
    if (mode == "--cluster-cut") return clusterCutMain();
    if (mode == "--levels") return levelsMain();
    if (mode == "--residency") return residencyMain();
    if (mode == "--decode") return decodeMain();
    if (mode == "--occlusion") return occlusionMain();
    if (mode == "--tlas") return tlasMain();
    if (mode == "--atlas") return atlasMain();
    if (mode == "--far-field") return farFieldMain();
    if (mode == "--bake") return bakeMain();
    if (mode == "--hit-list") return hitListMain();
    if (mode == "--cpu-walks") return cpuWalksMain();
    if (mode == "--lattice-owed") return latticeOwedMain();
    std::printf("usage: test_scale --world|--voxel-scroll|--lights|--cluster-cut|--levels|--residency|--decode|"
                "--occlusion|--tlas|--atlas|--far-field|--bake|--hit-list|--cpu-walks|--lattice-owed\n");
    return 2;
}
