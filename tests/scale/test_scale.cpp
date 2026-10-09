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
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QImage>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
/// A path longer than the monitor's ring (kRingCapacity, 4,096 frames — the 2 km walk is
/// 6,000) drains into collect()'s buffer as it goes (midDrain), or its first frames are lost.
static std::vector<FrameRecord> *g_midDrain = nullptr;
static void midDrain(Env &env)
{
    if (!g_midDrain) return;
    for (FrameRecord &r : drain(env)) g_midDrain->push_back(std::move(r));
}
static std::vector<FrameRecord> collect(Env &env, const std::function<void()> &body, int tail = 8)
{
    frame(env, 4);
    unsigned long long before = 0;
    for (const FrameRecord &r : drain(env)) before = std::max(before, r.frame);
    std::vector<FrameRecord> mid;
    g_midDrain = &mid;
    body();
    frame(env, tail);
    midDrain(env);
    g_midDrain = nullptr;
    std::vector<FrameRecord> out;
    for (FrameRecord &r : mid)
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
        if (f % 1024 == 1023) midDrain(env);
    }
}
/// A TELEPORT: `cycles` jumps from x = `from` to x = `to` and back, `legFrames` each leg.
static void pathTeleport(Env &env, float from, float to, int cycles, int legFrames)
{
    for (int c = 0; c < cycles; ++c) {
        setCamera(env, iris::Vec3(to, 1.7f, 0.0f), iris::Vec3(to, 1.7f, -10.0f));
        frame(env, legFrames);
        setCamera(env, iris::Vec3(from, 1.7f, 0.0f), iris::Vec3(from, 1.7f, -10.0f));
        frame(env, legFrames);
    }
}
/// THE 2 KM PATHS (V2-P0A): the walk crosses the whole 2 km ground edge to edge, and
/// the teleport set jumps across the voxel chain (60 m: inside the outer cascade), past it
/// (250 m), half the world (1 km) and the whole of it (1.9 km).
static constexpr float kWorldWalkMetres = 2000.0f, kWorldWalkSpeed = 20.0f;
struct Teleport { const char *name; float from, to; };
static const Teleport kTeleports[] = {
    { "teleport 60 m", 0.0f, 60.0f },
    { "teleport 250 m", 0.0f, 250.0f },
    { "teleport 1 km", -500.0f, 500.0f },
    { "teleport 1.9 km", -950.0f, 950.0f },
};
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
    REQUIRE(gpuTimed(env), "the frame monitor has GPU timing (fork 1a81f866a+1bccc3f93 (was 0027)'s query pool)");
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
// Number: ms per cascade rebuild (CPU and GPU, per cascade) on the 2 km walk and the
// teleport set (60 m, 250 m, 1 km, 1.9 km) over the 2 km world, from the monitor's
// `vct.cascadeN` rows (the 0027 timestamp pair).
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
    // THE PATHS: the 2 km walk, then each teleport of the set (4 jumps there and back,
    // 30 frames a leg). Each path's rows are its own — a 1.9 km jump and a 60 m one are
    // different bills.
    std::vector<std::pair<std::string, std::map<std::string, Row>>> sets;
    {
        const auto walkRecs = collect(env, [&] { pathWalk(env, kWorldWalkMetres, kWorldWalkSpeed); });
        printPath("walk2km", walkRecs);
        sets.push_back({ "walk 2 km @20 m/s", {} });
        harvest(walkRecs, sets.back().second);
    }
    for (const Teleport &t : kTeleports) {
        const auto recs = collect(env, [&] { pathTeleport(env, t.from, t.to, 4, 30); });
        printPath(t.name, recs);
        sets.push_back({ std::string(t.name) + " x4", {} });
        harvest(recs, sets.back().second);
    }
    bool any = false;
    for (auto &set : sets) {
        const char *label = set.first.c_str();
        for (auto &kv : set.second) {
            any = true;
            const Stats c = stats(kv.second.cpu), g = stats(kv.second.gpu);
            std::printf("W1 %-22s %-14s rebuilds %3zu | CPU ms med %7.2f max %7.2f | GPU ms med %8.2f max %8.2f\n",
                        label, kv.first.c_str(), c.n, c.median, c.max, g.median, g.max);
            const std::string what = std::string(label) + ": " + kv.first + " GPU ms per whole-cascade rebuild (median)";
            target("W1", g.median, "ms", what.c_str());
        }
    }
    REQUIRE(any, "the paths re-voxelised at least one cascade");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.lights, part 2 — W2: THE ONE LIGHT LIST (P1C-LIGHT-LIST; irisgl engine/src/SceneLightList.h).
// The voxel injection used to take the FIRST 16 lights in memory order (after IMAGE-1's range
// cull) and the cards the first 64 with no range cull. Now every consumer selects from the scene's
// one list: range-culled against its own box, the budget's worth by contribution (the one falloff
// at the box), the rest COUNTED (GiStatus::cascades[].lightsOverBudget, cards.lightsDropped).
// Fixture: a floor and a red wall in front of the camera, a black sky, High.
//   A  FITS:      15 dim fillers in range of cascade 0, then the KEY: 16 in range, all injected,
//                 0 over; the key's toggle moves the cascade-0 voxel digest.
//   B  BEST, NOT FIRST: 16 dim fillers FIRST, then the bright KEY (the 17th in memory order): 17 in
//                 range, 16 injected, 1 over the budget — and the key's toggle STILL moves the
//                 digest (the old rule dropped it: the 17th). The picture arm the brief names.
//   C  EVERY CASCADE: lamps on a line at growing distances; each cascade's lightsInRange equals
//                 the analytic count (range sphere vs the cascade's box from its status), injected
//                 = min(in range, the budget), over = the rest — on every cascade.
//   E  ONE UNIT: 16 dim point fillers + an area light delivering x1.05 one filler enters and
//                 moves the digest, its x0.95 twin goes over; a spot aimed away is culled by its cone.
//   F  THE RANK: a dim NEAR lamp beats a bright FAR one and the reverse, the order computed from
//                 the rank's law P W(d) / d_f^2 in the test.
//   D  THE CARDS (where rays run): 64 dim fillers FIRST, then the KEY: the floor's card radiance
//                 moves with the key (the old first-64 dropped it), cards.lightsDropped = 1.
// ===========================================================================
static void lightListArms(Env &env)
{
    iris::MeshPtr cube;
    auto fixture = [&]() {
        env.doc = iris::Scene::create();
        env.mirror->setSource(env.doc);
        env.doc->skyType = iris::SkyType::SINGLE_COLOR;
        env.doc->skyColor = QColor(0, 0, 0);
        worldmodes::setMode(env.doc, worldmodes::Mode::High);
        worldmodes::setPhoton(env.doc, true, worldmodes::PhotonTier::High);
        BakeInfo bi;
        if (!cube) cube = bakedMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj"), "cube", &bi);
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
    };
    auto lamp = [&](const QString &name, iris::Vec3 pos, float intensity, float range) {
        auto l = iris::LightNode::create();
        l->setLightType(iris::LightType::Point);
        l->setName(name);
        l->setLocalPos(pos);
        env.doc->getRootNode()->addChild(l);
        l->setPropertyValue(QStringLiteral("intensity"), intensity);
        l->setPropertyValue(QStringLiteral("distance"), range);
        l->shadowMap->shadowType = iris::ShadowMapType::None;
        return l;
    };
    auto settle = [&] {
        for (int f = 0; f < 600; ++f) {
            frame(env, 1);
            if (f > 30 && env.scene->giStatus().giAtRest) break;
        }
    };
    // Dim fillers inside cascade 0's box, behind the camera (+z), away from the wall.
    auto fillerPos = [](int i) {
        return iris::Vec3(-3.0f + float(i % 4) * 2.0f, 0.6f + float((i / 4) % 4) * 0.8f, 3.0f + float(i / 16) * 0.3f);
    };
    const iris::Vec3 eye(0.0f, 1.6f, 1.0f), at(0.0f, 1.5f, -3.0f);

    // ---- A and B: the voxels -------------------------------------------------
    struct ArmVox { int inRange = -1, injected = -1, over = -1, capacity = -1; bool keyMoves = false; };
    auto armVox = [&](int fillers) {
        ArmVox r;
        fixture();
        for (int i = 0; i < fillers; ++i) lamp(QStringLiteral("filler%1").arg(i), fillerPos(i), 0.05f, 4.0f);
        auto key = lamp(QStringLiteral("key"), iris::Vec3(0.0f, 2.0f, -1.5f), 6.0f, 10.0f);
        env.doc->getRootNode()->applyStaticDefaults();
        setCamera(env, eye, at);
        settle();
        const GiStatus st = env.scene->giStatus();
        GiVoxelStats on = env.scene->giVoxelStats(0);
        if (!st.cascades.empty()) {
            r.inRange = st.cascades[0].lightsInRange;
            r.injected = st.cascades[0].lightsInjected;
            r.over = st.cascades[0].lightsOverBudget;
            r.capacity = st.cascades[0].lightCapacity;
        }
        key->setPropertyValue(QStringLiteral("intensity"), 0.0f);
        key->markChanged(iris::NodeChange::Params);
        settle();
        GiVoxelStats off = env.scene->giVoxelStats(0);
        r.keyMoves = on.available && off.available && on.lightDigest != off.lightDigest;
        std::printf("   %2d fillers + key: cascade 0 lights in range %d, injected %d, over budget %d (budget %d) | "
                    "digest on %s off %s | meanLit on %.6f off %.6f\n",
                    fillers, r.inRange, r.injected, r.over, r.capacity, on.lightDigest.c_str(),
                    off.lightDigest.c_str(), on.meanLit, off.meanLit);
        return r;
    };
    const ArmVox a = armVox(15);
    const int budget = a.capacity;
    REQUIRE(budget >= 16, "the per-cascade budget holds at least the old 16 (%d)", budget);
    REQUIRE(a.inRange == 16 && a.injected == 16 && a.over == 0,
            "A: 15 fillers + the key all reach cascade 0 and all enter it (%d in range, %d injected, %d over)",
            a.inRange, a.injected, a.over);
    REQUIRE(a.keyMoves, "A: the key's toggle moves the cascade-0 voxels");
    const int bFillers = budget;   // the key is the (budget + 1)th light in memory order
    const ArmVox b = armVox(bFillers);
    REQUIRE(b.inRange == bFillers + 1 && b.injected == budget && b.over == 1,
            "B: %d fillers + the key reach cascade 0; the budget's %d enter, ONE is over it and counted "
            "(%d in range, %d injected, %d over)", bFillers, budget, b.inRange, b.injected, b.over);
    REQUIRE(b.keyMoves,
            "B: the bright key, the LAST light in memory order, still enters the voxels (best, not first): "
            "its toggle moves the digest");
    target("W2", double(b.keyMoves ? 1.0 : 0.0), "(1 = the brightest light enters)",
           "the (budget+1)th light in memory order, the brightest at the cascade, reaches the voxels", "1 (exact)");
    target("W2", double(budget), "lights", "cascade 0's budget (PhotonVoxelLighting::lightBudgetFor: 16 at 128^3, 128 at 64^3)");

    // ---- C: every cascade, the analytic count ----------------------------------
    {
        fixture();
        // Lamps on a line along +x from the eye, 2 m range: each reaches the cascades whose box
        // its sphere touches. 132 more far out (x 40-57 m) reach only the outer cascade, past
        // its budget (128 at 64^3, PhotonVoxelLighting::kLightSlots), so it alone goes over it.
        std::vector<std::pair<iris::Vec3, float>> placed;
        const float xs[] = { 1.0f, 4.0f, 6.5f, 9.0f, 12.0f, 16.0f, 22.0f, 40.0f, 58.0f };
        int n = 0;
        for (float x : xs) {
            const iris::Vec3 p(eye.x() + x, 1.0f, eye.z());
            lamp(QStringLiteral("line%1").arg(n++), p, 1.0f, 2.0f);
            placed.push_back({ p, 2.0f });
        }
        for (int i = 0; i < 132; ++i) {
            const iris::Vec3 p(eye.x() + 40.0f + float(i % 12) * 1.5f, 1.0f, eye.z() - 8.0f + float(i / 12) * 1.5f);
            lamp(QStringLiteral("far%1").arg(i), p, 1.0f, 2.0f);
            placed.push_back({ p, 2.0f });
        }
        env.doc->getRootNode()->applyStaticDefaults();
        setCamera(env, eye, at);
        settle();
        const GiStatus st = env.scene->giStatus();
        REQUIRE(st.cascades.size() >= 2, "C: the chain has its cascades (%zu)", st.cascades.size());
        bool allMatch = !st.cascades.empty();
        int anyOver = 0;
        for (size_t c = 0; c < st.cascades.size(); ++c) {
            const GiStatus::CascadeStatus &cs = st.cascades[c];
            int expect = 0;
            for (const auto &pl : placed) {
                double d2 = 0.0;
                const float p[3] = { pl.first.x(), pl.first.y(), pl.first.z() };
                const float ctr[3] = { cs.centre.x, cs.centre.y, cs.centre.z };
                for (int k = 0; k < 3; ++k) {
                    const double lo = ctr[k] - cs.halfSize, hi = ctr[k] + cs.halfSize;
                    const double q = std::min(std::max(double(p[k]), lo), hi);
                    d2 += (q - p[k]) * (q - p[k]);
                }
                // A lamp within 1 cm of a box face is ambiguous to float: the fixture avoids it.
                if (d2 <= double(pl.second) * pl.second) ++expect;
            }
            const int wantInjected = std::min(expect, cs.lightCapacity);
            const bool ok = cs.lightsInRange == expect && cs.lightsInjected == wantInjected &&
                            cs.lightsOverBudget == expect - wantInjected;
            allMatch = allMatch && ok;
            anyOver += cs.lightsOverBudget;
            std::printf("   C cascade %zu [centre %.2f %.2f %.2f, half %.1f m]: in range %d (analytic %d), "
                        "injected %d, over %d, budget %d %s\n", c, cs.centre.x, cs.centre.y, cs.centre.z,
                        double(cs.halfSize), cs.lightsInRange, expect, cs.lightsInjected, cs.lightsOverBudget,
                        cs.lightCapacity, ok ? "" : "<-- MISMATCH");
        }
        REQUIRE(allMatch, "C: every cascade takes every light within its range up to the budget, the rest "
                          "counted over it");
        REQUIRE(anyOver > 0, "C: the outer cascade's overflow is REPORTED (%d over)", anyOver);
    }

    // ---- E: one unit for every type; the spot's cone ---------------------------
    // 16 dim point fillers inside cascade 0 (its budget, 16), then ONE 2 x 1 m AREA light
    // sized from the rank's own law so that it delivers to the box JUST ABOVE one filler
    // (x 1.05; a filler delivers I / d_f^2, the area light I r^2 / (r^2 + d_f^2), r =
    // sqrt(w h / pi), d_f = half the box's half-extent): it must ENTER (one filler goes over
    // the budget) and its toggle must move the voxels. Its twin JUST BELOW (x 0.95) must go
    // OVER the budget: its toggle leaves the voxels byte for byte. A wrong area unit fails
    // one of the two. In the first arm, two spots 7 m off the box's +x and -x faces, range 8
    // (both spheres reach the box), both aimed +x: the one aimed AWAY lights nothing in it
    // and is CULLED, the one aimed AT it is counted (and, the dimmest at the box, goes over).
    auto armE = [&](float k, bool spots) {
        fixture();
        const float fillerI = 0.05f;
        for (int i = 0; i < 16; ++i) lamp(QStringLiteral("efill%1").arg(i), fillerPos(i), fillerI, 4.0f);
        env.doc->getRootNode()->applyStaticDefaults();
        setCamera(env, eye, at);
        settle();
        const GiStatus g0 = env.scene->giStatus();
        REQUIRE(!g0.cascades.empty(), "E: the chain is built");
        if (g0.cascades.empty()) return;
        const float half = g0.cascades[0].halfSize;
        const float dF = 0.5f * half;
        const float rectW = 2.0f, rectH = 1.0f;
        const float r2 = rectW * rectH / 3.14159265f;
        const float areaI = k * fillerI / (dF * dF) * (r2 + dF * dF) / r2;
        auto area = iris::LightNode::create();
        area->setLightType(iris::LightType::Area);
        area->setName(QStringLiteral("area"));
        area->setLocalPos(iris::Vec3(0.0f, 2.0f, -1.5f));
        env.doc->getRootNode()->addChild(area);
        area->setPropertyValue(QStringLiteral("intensity"), areaI);
        area->setPropertyValue(QStringLiteral("distance"), 10.0f);
        area->setPropertyValue(QStringLiteral("rectWidth"), rectW);
        area->setPropertyValue(QStringLiteral("rectHeight"), rectH);
        area->shadowMap->shadowType = iris::ShadowMapType::None;
        auto spot = [&](const QString &name, float x) {
            auto l = iris::LightNode::create();
            l->setLightType(iris::LightType::Spot);
            l->setName(name);
            l->setLocalPos(iris::Vec3(x, g0.cascades[0].centre.y, g0.cascades[0].centre.z));
            // a light shines along its -Y (the document convention): a +90 degree roll aims it +x
            l->setLocalRot(iris::Quat::fromEulerAngles(0.0f, 0.0f, 90.0f));
            env.doc->getRootNode()->addChild(l);
            l->setPropertyValue(QStringLiteral("intensity"), 0.5f);
            l->setPropertyValue(QStringLiteral("distance"), 8.0f);
            l->setPropertyValue(QStringLiteral("spotCutOff"), 30.0f);
            l->shadowMap->shadowType = iris::ShadowMapType::None;
        };
        if (spots) {
            // 7 m outside the box's +x / -x faces (inside the 8 m range).
            spot(QStringLiteral("spotAway"), g0.cascades[0].centre.x + half + 7.0f);
            spot(QStringLiteral("spotAt"), g0.cascades[0].centre.x - half - 7.0f);
        }
        env.doc->getRootNode()->applyStaticDefaults();
        settle();
        const GiStatus st = env.scene->giStatus();
        const GiVoxelStats on = env.scene->giVoxelStats(0);
        area->setPropertyValue(QStringLiteral("intensity"), 0.0f);
        area->markChanged(iris::NodeChange::Params);
        settle();
        const GiVoxelStats off = env.scene->giVoxelStats(0);
        const GiStatus::CascadeStatus &c = st.cascades[0];
        const int want = spots ? 18 : 17;
        std::printf("   E (x %.2f%s): 16 fillers (%.2f) + a 2 x 1 m area light (%.4f) : cascade 0 in range %d, "
                    "injected %d, over %d (budget %d) | digest area on %s off %s\n",
                    double(k), spots ? ", two spots" : "", double(fillerI), double(areaI), c.lightsInRange,
                    c.lightsInjected, c.lightsOverBudget, c.lightCapacity, on.lightDigest.c_str(),
                    off.lightDigest.c_str());
        if (spots)
            REQUIRE(c.lightsInRange == want,
                    "E: the fillers, the area light and the spot aimed AT the box reach cascade 0; the spot aimed "
                    "AWAY is culled by its cone (%d in range, %d expected)", c.lightsInRange, want);
        else
            REQUIRE(c.lightsInRange == want, "E: the fillers and the area light reach cascade 0 (%d of %d)",
                    c.lightsInRange, want);
        REQUIRE(c.lightsInjected == c.lightCapacity && c.lightsOverBudget == c.lightsInRange - c.lightCapacity,
                "E: the budget fills and the rest are counted (%d injected of %d, %d over)", c.lightsInjected,
                c.lightsInRange, c.lightsOverBudget);
        const bool moves = on.available && off.available && on.lightDigest != off.lightDigest;
        if (k > 1.0f)
            REQUIRE(moves, "E: the area light delivering x %.2f one filler, ranked in the point lights' unit, ENTERS "
                    "the voxels: its toggle moves the digest", double(k));
        else
            REQUIRE(on.available && off.available && !moves,
                    "E: its twin delivering x %.2f one filler goes OVER the budget: its toggle leaves the digest",
                    double(k));
    };
    armE(1.05f, true);
    armE(0.95f, false);

    // ---- F: the rank is the irradiance the box receives, not the power -----------
    // 15 bright fillers inside cascade 0 take 15 of its 16 slots; two lamps OUTSIDE the box
    // on its +x side compete for the last: NEAR (3 m from the face, range 6) and FAR (6 m,
    // range 12; both at d / range = 0.5). The expected winner is computed here from the
    // rank's law, P x W(d) / d_f^2 with d_f = max(d, half the half-extent) and W(d) =
    // (1 - (d/R)^4)^2: (1) a dim NEAR lamp beats a brighter FAR one; (2) the FAR lamp made
    // bright enough beats the NEAR one. The winner's toggle moves the voxels; the loser's
    // (over the budget) leaves them byte for byte. A rank by raw power fails (1).
    auto armF = [&](float nearI, float farI) {
        fixture();
        for (int i = 0; i < 15; ++i) lamp(QStringLiteral("ffill%1").arg(i), fillerPos(i), 1.0f, 4.0f);
        env.doc->getRootNode()->applyStaticDefaults();
        setCamera(env, eye, at);
        settle();
        const GiStatus g0 = env.scene->giStatus();
        REQUIRE(!g0.cascades.empty(), "F: the chain is built");
        if (g0.cascades.empty()) return;
        const GiStatus::CascadeStatus &c0 = g0.cascades[0];
        const float half = c0.halfSize;
        const float floorD = 0.5f * half;
        auto score = [&](float I, float d, float R) {
            const float x = d / R, x2 = x * x;
            const float w = std::pow(std::max(0.0f, 1.0f - x2 * x2), 2.0f);
            const float df = std::max(d, floorD);
            return I * w / (df * df);
        };
        const float dN = 3.0f, rN = 6.0f, dFar = 6.0f, rF = 12.0f;
        const float sN = score(nearI, dN, rN), sF = score(farI, dFar, rF), sFill = score(1.0f, 0.0f, 4.0f);
        const bool nearWins = sN > sF;
        auto nearL = lamp(QStringLiteral("near"), iris::Vec3(c0.centre.x + half + dN, c0.centre.y, c0.centre.z), nearI, rN);
        auto farL = lamp(QStringLiteral("far"), iris::Vec3(c0.centre.x + half + dFar, c0.centre.y, c0.centre.z), farI, rF);
        env.doc->getRootNode()->applyStaticDefaults();
        settle();
        const GiStatus st = env.scene->giStatus();
        const GiVoxelStats base = env.scene->giVoxelStats(0);
        auto toggleMoves = [&](iris::LightNodePtr l, float I) {
            l->setPropertyValue(QStringLiteral("intensity"), 0.0f);
            l->markChanged(iris::NodeChange::Params);
            settle();
            const GiVoxelStats off = env.scene->giVoxelStats(0);
            l->setPropertyValue(QStringLiteral("intensity"), I);
            l->markChanged(iris::NodeChange::Params);
            settle();
            return off.lightDigest != base.lightDigest;
        };
        const bool nearMoves = toggleMoves(nearL, nearI);
        const bool farMoves = toggleMoves(farL, farI);
        std::printf("   F: near %.2f at %.0f m (score %.5f) vs far %.2f at %.0f m (score %.5f), fillers %.5f: "
                    "in range %d, injected %d, over %d | toggles move the voxels: near %d far %d\n",
                    double(nearI), double(dN), double(sN), double(farI), double(dFar), double(sF), double(sFill),
                    st.cascades[0].lightsInRange, st.cascades[0].lightsInjected, st.cascades[0].lightsOverBudget,
                    int(nearMoves), int(farMoves));
        REQUIRE(sFill > sN && sFill > sF && st.cascades[0].lightsInRange == 17 && st.cascades[0].lightsOverBudget == 1,
                "F: the fillers outrank both, all 17 reach cascade 0 and one is over its 16");
        REQUIRE(nearWins ? (nearMoves && !farMoves) : (farMoves && !nearMoves),
                "F: the %s lamp (%.2f at %.0f m) takes the last slot by the irradiance it delivers, the %s one "
                "(%.2f at %.0f m) goes over", nearWins ? "NEAR" : "FAR", double(nearWins ? nearI : farI),
                double(nearWins ? dN : dFar), nearWins ? "FAR" : "NEAR", double(nearWins ? farI : nearI),
                double(nearWins ? dFar : dN));
    };
    armF(0.5f, 1.5f);   // the dim near lamp wins: 0.5 W / 9 > 1.5 W / 36
    armF(0.5f, 2.5f);   // the bright far lamp wins: 2.5 W / 36 > 0.5 W / 9

    // ---- D: the cards (they run where rays run) ---------------------------------
    {
        fixture();
        for (int i = 0; i < 64; ++i) lamp(QStringLiteral("cfill%1").arg(i), fillerPos(i), 0.02f, 3.0f);
        auto key = lamp(QStringLiteral("key"), iris::Vec3(0.0f, 2.0f, -1.5f), 6.0f, 10.0f);
        env.doc->getRootNode()->applyStaticDefaults();
        setCamera(env, eye, at);
        settle();
        for (int f = 0; f < 600 && env.scene->giStatus().cards.queueLength > 0; ++f) frame(env, 1);
        frame(env, 30);
        CardCacheStatus c0 = env.scene->giStatus().cards;
        // A FULL RELIGHT PASS (not the last batch, which depends on which cards it held):
        // a radiance change (the key 6 -> 6.01) stales every resident card's direct half;
        // the pass's reading is the largest batch box's — the one holding the floor card,
        // whose capture volume holds every lamp — over the frames until every card relit.
        unsigned passInRange = 0u, passDropped = 0u;
        if (c0.built) {
            const unsigned long long rel0 = c0.relights;
            const unsigned resident = c0.cardsResident;
            key->setPropertyValue(QStringLiteral("intensity"), 6.01f);
            key->markChanged(iris::NodeChange::Params);
            for (int f = 0; f < 600; ++f) {
                frame(env, 1);
                const CardCacheStatus s = env.scene->giStatus().cards;
                if (s.relitLastFrame > 0 && s.lightsInRange >= passInRange) {
                    passInRange = s.lightsInRange;
                    passDropped = s.lightsDropped;
                }
                if (s.relights - rel0 >= resident && s.queueLength == 0) break;
            }
            frame(env, 30);
            c0 = env.scene->giStatus().cards;
            std::printf("   D: the full relight pass: %llu relights of %u resident cards\n",
                        (unsigned long long)(c0.relights - rel0), resident);
        }
        if (!c0.built) {
            std::printf("   D: no card cache on this machine (cards run where rays run): the arm is skipped\n");
        } else {
            CardSample on;
            const bool okOn = env.scene->readCardAt(Vec3(0.0f, 0.1f, -1.5f), Vec3(0, 1, 0), on) && on.ok;
            key->setPropertyValue(QStringLiteral("intensity"), 0.0f);
            key->markChanged(iris::NodeChange::Params);
            settle();
            for (int f = 0; f < 600 && env.scene->giStatus().cards.queueLength > 0; ++f) frame(env, 1);
            frame(env, 30);
            CardSample off;
            const bool okOff = env.scene->readCardAt(Vec3(0.0f, 0.1f, -1.5f), Vec3(0, 1, 0), off) && off.ok;
            std::printf("   D: 64 dim fillers + the key: cards lights in range %u, dropped %u | the floor's card "
                        "under the key: radiance (red) key on %.5f off %.5f (read %d/%d, lit %d/%d)\n",
                        passInRange, passDropped, double(on.radiance[0]), double(off.radiance[0]),
                        int(okOn), int(okOff), int(on.lit), int(off.lit));
            REQUIRE(passInRange == 65u && passDropped == 1u,
                    "D: over a full relight pass 65 lights reach the relit cards, the relight's 64 hold all but "
                    "one, counted (%u, %u)", passInRange, passDropped);
            REQUIRE(okOn && okOff && on.radiance[0] > 2.0f * off.radiance[0] + 1e-4f,
                    "D: the key, the 65th light in memory order and the brightest, lights the floor's card");
        }
    }
}

// ===========================================================================
// scale.lights, part 1 — W2: THE VOXEL INJECTION'S BUDGET (V2-P0A's sweep; P1C-LIGHT-LIST's rule).
// Anchor: irisgl/engine/src/photon/voxel/PhotonVoxelLighting.cpp (lightBudgetFor: 16 at
// 128^3, 128 at 64^3) and SceneLightList::select (the range cull, then the budget by
// contribution, the rest counted over it). Part 2 (lightListArms, above) holds the rule's arms.
// THE ROW MEASURES THE CAP ITSELF (V2-P0A): N = 8, 16, 32 and 64 filler lamps placed
// INSIDE cascade 0's reach (a 3 m range around the camera's own spot, so the range cull
// keeps every one of them), then the KEY lamp, created last, facing a red wall. The
// number is the engine's own reading of the cascade's light slots (GiStatus::cascades[0]
// lightsInRange / lightsInjected / lightCapacity): the count that ENTERS the voxels.
// THE FALSE-PASS GUARD: an arm whose lamps the range cull drops does not push the cap
// (D1's fillers stood 40 m away with a 4 m range; once IMAGE-1 culled them the row read
// "light 17 bounces" over a scene that held two lights) — every arm must read all N + 1
// lamps IN RANGE or the row FAILS. D1's old placement is still read and printed, as
// information (the proof that it passed falsely), never as a bar.
// The behavioural cross-check (the voxel store's digest, GiVoxelStats::lightDigest):
// at N = 8 the key's toggle moves the store (the CONTROL: the fixture sees a light);
// at N = 16 the key is light 17 — whether its bounce reaches is printed (today: not).
// ===========================================================================
static int lightsMain()
{
    Env env;
    if (!boot(env, "test-scale-lights-ogre.log")) return 1;
    const iris::Vec3 eye(0.0f, 2.0f, 1.0f);
    BakeInfo bi;
    iris::MeshPtr cube = bakedMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj"), "cube", &bi);
    if (cube.isNull()) { REQUIRE(false, "the cube mesh"); return 1; }
    struct Reading {
        bool ok = false;
        int inRange = -1, injected = -1, capacity = -1;
        std::string digestOn, digestOff;
        double litOn = 0, litOff = 0;
    };
    // One build per arm: `fillers` lamps first (`near`: inside cascade 0's reach; else D1's
    // old placement 40 m away), then the key. `toggle`: read the store with the key on and off.
    auto arm = [&](int fillers, bool nearFillers, bool toggle) {
        Reading out;
        env.doc = iris::Scene::create();
        env.mirror->setSource(env.doc);
        env.doc->skyType = iris::SkyType::SINGLE_COLOR;
        env.doc->skyColor = QColor(0, 0, 0);
        worldmodes::setMode(env.doc, worldmodes::Mode::High);
        worldmodes::setPhoton(env.doc, true, worldmodes::PhotonTier::High);
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
            // NEAR: an 8 x 8 lattice at a 0.5 m pitch, 1.5 m above the eye's own spot —
            // every range sphere (3 m) holds the eye, so it reaches the box cascade 0 is
            // centred on whatever the box's quantisation. FAR: D1's 40 m placement.
            l->setLocalPos(nearFillers ? iris::Vec3(eye.x() - 1.75f + 0.5f * float(i % 8), eye.y() + 1.5f,
                                                    eye.z() - 1.75f + 0.5f * float(i / 8))
                                       : iris::Vec3(40.0f + float(i % 4) * 3.0f, 3.0f, float(i / 4) * 3.0f - 4.5f));
            env.doc->getRootNode()->addChild(l);
            l->setPropertyValue(QStringLiteral("intensity"), 0.25f);
            l->setPropertyValue(QStringLiteral("distance"), nearFillers ? 3.0f : 4.0f);
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
        setCamera(env, eye, iris::Vec3(0, 1, -2));
        auto settle = [&] {
            for (int f = 0; f < 600; ++f) {
                frame(env, 1);
                if (f > 30 && env.scene->giStatus().giAtRest) break;
            }
        };
        settle();
        const GiStatus gi = env.scene->giStatus();
        if (gi.cascades.empty()) return out;
        const GiStatus::CascadeStatus &c0 = gi.cascades[0];
        out.inRange = c0.lightsInRange;
        out.injected = c0.lightsInjected;
        out.capacity = c0.lightCapacity;
        out.ok = true;
        if (toggle) {
            const GiVoxelStats on = env.scene->giVoxelStats(0);
            key->setPropertyValue(QStringLiteral("intensity"), 0.0f);
            key->markChanged(iris::NodeChange::Params);
            settle();
            const GiVoxelStats off = env.scene->giVoxelStats(0);
            out.ok = on.available && off.available;
            out.digestOn = on.lightDigest;
            out.digestOff = off.lightDigest;
            out.litOn = on.meanLit;
            out.litOff = off.meanLit;
        }
        std::printf("   %2d fillers (%s) + key: cascade 0 [centre %.2f %.2f %.2f, half %.1f m] lights in range %d, "
                    "injected %d of %d slots%s%s\n",
                    fillers, nearFillers ? "inside its reach" : "D1's, 40 m away", double(c0.centre.x),
                    double(c0.centre.y), double(c0.centre.z), double(c0.halfSize), out.inRange, out.injected,
                    out.capacity, toggle ? (out.digestOn != out.digestOff ? " | the key's toggle MOVES the store"
                                                                          : " | the key's toggle leaves the store")
                                         : "",
                    toggle ? qPrintable(QStringLiteral(" (mean lit %1 vs %2)").arg(out.litOn, 0, 'f', 6)
                                            .arg(out.litOff, 0, 'f', 6)) : "");
        std::fflush(stdout);
        return out;
    };
    // THE GUARD PROVES ITSELF on D1's placement: its 16 fillers are culled, so the arm holds
    // the key alone and would "pass" without pushing the cap — the guard must see that.
    // INFORMATIONAL, never a bar: a wider cascade 0 could bring them into range one day, and
    // that is no defect — the per-arm "all N + 1 in range" check below is the guard.
    const Reading d1 = arm(16, false, false);
    std::printf("W2 THE GUARD'S PROOF: D1's 40 m fillers read %d of 17 lamps in range%s\n", d1.inRange,
                d1.ok && d1.inRange < 17 ? " — culled: that fixture passed falsely, and the per-arm check reds it" : "");
    target("W2", double(d1.inRange), "lights", "D1's old placement (16 fillers 40 m away + the key): lamps in range "
           "of cascade 0 (informational)");
    int cap = -1;
    bool keyReachesAt16 = false;
    for (int n : { 8, 16, 32, 64 }) {
        const Reading r = arm(n, true, n == 8 || n == 16);
        REQUIRE(r.ok, "N = %d: cascade 0 was read (and the store where toggled)", n);
        // THE FALSE-PASS GUARD: every lamp of the arm reaches cascade 0, or the arm does not
        // push the cap and its reading means nothing.
        REQUIRE(r.inRange >= n + 1, "N = %d: all %d lamps are IN RANGE of cascade 0 (read %d) — the arm pushes the cap",
                n, n + 1, r.inRange);
        REQUIRE(r.injected == std::min(r.inRange, r.capacity), "N = %d: the budget fills, by contribution, up to the "
                "capacity (%d injected of %d in range, %d the budget)", n, r.injected, r.inRange, r.capacity);
        const std::string what = "lights that ENTER cascade 0's voxels with " + std::to_string(n + 1) +
                                 " in its reach (" + std::to_string(r.inRange) + " in range, " +
                                 std::to_string(r.inRange - r.injected) + " dropped)";
        target("W2", double(r.injected), "lights", what.c_str(), "every light in range");
        cap = std::max(cap, r.injected);
        if (n == 8)
            REQUIRE(r.digestOn != r.digestOff, "CONTROL: with 8 lamps before it the key's toggle moves the voxel store");
        if (n == 16) keyReachesAt16 = r.digestOn != r.digestOff;
    }
    std::printf("W2: the cap the injection holds: %d lights per cascade; with 16 lights before it the key lamp %s "
                "the voxel store\n", cap, keyReachesAt16 ? "REACHES" : "does NOT reach");
    target("W2", double(cap), "lights", "THE CAP: the most lights one cascade's injection took (64 + 1 in range)",
           "every light in range");
    target("W2", keyReachesAt16 ? 1.0 : 0.0, "(1 = light 17 bounces)",
           "the 17th light's bounce reaches the voxels: exact bar = the digest moves", "1 (exact)");
    lightListArms(env);
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// --lights-cost (a MEASURING mode, not a row; P1C-LIGHT-LIST): THE GI LIGHT TICK'S GPU ms on the
// P0 world (500 lamps), `JAH_LIGHTS_COST_ROUNDS` (16) at-rest ticks forced by refreshGiLighting —
// the `vct.light` rows' GPU ms (the whole tick: every cascade's injection, bounces, mips). With
// `JAH_LIGHTS_COST_CLUSTER=N`, N more lamps around the eye (30 m range: every one reaches every
// cascade) — the per-light cost the budget is chosen by. Run under scripts/gpu-exclusive.sh.
// ===========================================================================
static int lightsCostMain()
{
    Env env;
    World w;
    WorldSpec spec;
    const char *cl = std::getenv("JAH_LIGHTS_COST_CLUSTER");
    const int cluster = cl ? std::atoi(cl) : 0;
    const char *rr = std::getenv("JAH_LIGHTS_COST_ROUNDS");
    const int rounds = rr ? std::max(1, std::atoi(rr)) : 16;
    if (!bootWorld(env, w, "test-scale-lights-cost-ogre.log", spec)) return 1;
    REQUIRE(gpuTimed(env), "the frame monitor has GPU timing");
    if (cluster > 0) {
        const iris::Vec3 eye = env.camera->getLocalPos();
        for (int i = 0; i < cluster; ++i) {
            const float a = 6.2831853f * float(i) / float(cluster);
            auto l = iris::LightNode::create();
            l->setLightType(iris::LightType::Point);
            l->setName(QStringLiteral("cluster%1").arg(i));
            l->setLocalPos(iris::Vec3(eye.x() + 3.0f * std::cos(a), 3.0f, eye.z() + 3.0f * std::sin(a)));
            env.doc->getRootNode()->addChild(l);
            l->setPropertyValue(QStringLiteral("intensity"), 0.5f);
            l->setPropertyValue(QStringLiteral("distance"), 30.0f);
            l->shadowMap->shadowType = iris::ShadowMapType::None;
        }
        for (int f = 0; f < 600; ++f) {
            frame(env, 1);
            if (f > 30 && env.scene->giStatus().giAtRest) break;
        }
    }
    {
        const GiStatus st = env.scene->giStatus();
        for (size_t c = 0; c < st.cascades.size(); ++c)
            std::printf("   cascade %zu (half %.1f m): lights in range %d, injected %d, over budget %d\n", c,
                        double(st.cascades[c].halfSize), st.cascades[c].lightsInRange,
                        st.cascades[c].lightsInjected, st.cascades[c].lightsOverBudget);
    }
    std::vector<double> ms;
    const auto recs = collect(env, [&] {
        for (int r = 0; r < rounds; ++r) {
            env.scene->refreshGiLighting(false);
            frame(env, 8);
        }
    });
    for (const FrameRecord &r : recs)
        for (const CacheWork &cw : r.cacheWork)
            if (cw.detail == "vct.light" && cw.gpuMs > 0.0f) ms.push_back(cw.gpuMs);
    const Stats s = stats(ms);
    std::printf("LIGHTS-COST cluster %d: the light tick's GPU ms median %.3f mean %.3f p95 %.3f max %.3f (n %zu)\n",
                cluster, s.median, s.mean, s.p95, s.max, s.n);
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// THE LARGE ASSET for W3 / W4: THE 10 M SHELL from the shared scale cache (requireShell —
// a missing shell is the row's FAIL, never a smaller asset measured in its place; the
// fixture row scale.assets bakes it once per bake producer for the whole box).
// ===========================================================================
static constexpr size_t kLargeAsset = 10000000;

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
/// THE DECODE'S MS is the OPAQUE SIDE: the screen decode pass in front of the opaque
/// pass (ATOM-DECODE-CLASS-1, "Jahshaka atom decode": the classifier when no prepass
/// ran, and the bucket draws) PLUS the opaque pass — the one number that reads the
/// same before the classification (the bucket draws were the opaque pass's first)
/// and after it. `decodePassMs` is the decode pass alone.
struct IdRead {
    double tris = -1, idMs = -1, decodeMs = -1, decodePassMs = -1;
    /// The occlusion's LATE list ("Jahshaka atom id late": what the first cull rejected
    /// against the previous frame's pyramid and this frame's found visible) — the id
    /// pass's second row; `tris + lateTris` is the two-pass total. 0 with occlusion off.
    double lateTris = 0;
    /// The id pass's four cull jobs, each its monitor row per frame (summed over the
    /// frame's early and late culls): test, compact, cut, emit. -1 = unsampled.
    double cullMs[4] = { -1, -1, -1, -1 };
    unsigned survivors = 0;
};
/// The opaque side of one frame (see IdRead): negative when the opaque pass carries
/// no GPU sample.
static double opaqueSideMs(const FrameRecord &r, double *decodePassMs = nullptr)
{
    const FramePass *o = passNamed(r, "Jahshaka opaque");
    if (!o || o->gpuMs < 0) return -1;
    const FramePass *d = passNamed(r, "Jahshaka atom decode");
    if (decodePassMs) *decodePassMs = d && d->gpuMs >= 0 ? d->gpuMs : -1;
    return o->gpuMs + (d && d->gpuMs >= 0 ? d->gpuMs : 0.0);
}
/// A GPU reading for a report line: its ms, or "unsampled" (never a -1 as a number).
static std::string msText(double ms)
{
    if (ms < 0) return "unsampled";
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f ms", ms);
    return b;
}

static IdRead readIdPass(Env &env, int frames)
{
    // FRAME-COUNTED RE-READS, never a CPU pause. A record now waits for its GPU
    // samples however heavy its frame (MONITOR-RETIRE-1 — before it, 0-4 of 38 records
    // inside the 10 M shell's bounds carried an id time); the loop stays as the bound on
    // a reading that never lands (a lost device): batches of `frames` are read until
    // each pass holds kWant samples or kMaxFrames have been drawn, and a reading that
    // never landed is UNSAMPLED (negative) and printed as such.
    static const size_t kWant = 8;
    static const int kMaxFrames = 600;
    IdRead out;
    std::vector<double> tris, late, id, dec, decPass, cull[4];
    static const char *kCullRows[4] = { "id.cull.test", "id.cull.compact", "id.cull.cut", "id.cull.emit" };
    int drawn = 0;
    unsigned dropped = 0;
    size_t records = 0;
    while (drawn < kMaxFrames) {
        const auto recs = collect(env, [&] { frame(env, frames); }, 8);
        drawn += frames + 12;
        records += recs.size();
        for (const FrameRecord &r : recs) {
            dropped += r.gpuMarksDropped;
            if (const FramePass *p = passNamed(r, "Jahshaka atom id")) {
                tris.push_back(double(p->triangles));
                if (p->gpuMs >= 0) id.push_back(p->gpuMs);
            }
            if (const FramePass *p = passNamed(r, "Jahshaka atom id late")) late.push_back(double(p->triangles));
            double dp = -1;
            const double ms = opaqueSideMs(r, &dp);
            if (ms >= 0) dec.push_back(ms);
            if (dp >= 0) decPass.push_back(dp);
            for (int k = 0; k < 4; ++k) {
                double sum = -1;
                for (const CacheWork &w : r.cacheWork)
                    if (w.detail == kCullRows[k] && w.gpuMs >= 0) sum = (sum < 0 ? 0 : sum) + w.gpuMs;
                if (sum >= 0) cull[k].push_back(sum);
            }
        }
        if (id.size() >= kWant && dec.size() >= kWant) break;
    }
    out.tris = stats(tris).median;
    out.lateTris = late.empty() ? 0.0 : stats(late).median;
    out.idMs = stats(id).median;
    out.decodeMs = stats(dec).median;
    out.decodePassMs = decPass.empty() ? -1.0 : stats(decPass).median;
    for (int k = 0; k < 4; ++k) out.cullMs[k] = cull[k].empty() ? -1.0 : stats(cull[k]).median;
    if (id.size() < kWant || dec.size() < kWant)
        std::printf("   (GPU samples short after %d frames: %zu records, id %zu, decode %zu, %u GPU marks dropped)\n",
                    drawn, records, id.size(), dec.size(), dropped);
    return out;
}

/// The LEVEL RULE's answer for one piece at the view's eye: the cluster cut the DAG
/// gives at the id pass's own tolerance (kLodBudgetPixels x the scene's LOD bias) —
/// Types.h clusterCut, the GLSL twin's arithmetic.
/// Types.h clusterCut over the model's pieces at the view's eye. `root`: an unbounded
/// tolerance and no frustum — the DAG's ROOT, the coarsest cut the DAG can draw.
static size_t cutTriangles(Env &env, const QList<iris::MeshPtr> &pieces, const iris::Vec3 &pos, float k,
                           bool root = false)
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
        v.tolerance = root ? 1e30f : kLodBudgetPixels * env.scene->lodBias();
        v.projScaleY = req.projScaleY;
        v.viewportHeight = req.viewportHeight;
        // ...and the view's frustum, cluster by cluster, as the cut job tests it
        // (SPEED-VR-MEM, Types.h clusterInFrustum).
        v.cullPlanes = !root;
        for (int i = 0; i < 6; ++i)
            for (int c = 0; c < 4; ++c) v.planes[i][c] = req.planes[i * 4 + c];
        std::vector<unsigned> drawn;
        total += clusterCut(d.clusterGroups, d.clusters, v, drawn);
    }
    return total;
}

// ===========================================================================
// scale.cluster_cut — W3: THE CUT PER CLUSTER GROUP (ATOM-CLUSTER-CUT closed the wall:
// until then the id pass drew one LEVEL per instance, and inside the bounds d = 0 forced
// level 0). Anchor: irisgl/engine/media/Hlms/Jahshaka/JahCullCut_cs.glsl (the cull's cut
// job). Number: triangles the id pass draws for the large asset at three camera distances
// (inside its bounds, 5 radii, 30 radii) against Types.h clusterCut at the same tolerance
// — the acceptance is within 1.2x (the GPU's count is the stats ring's, a few frames
// late, so the reading waits for the still pose). Also the owed row: the id pass's and
// the decode's GPU ms on the asset.
// ===========================================================================
static int clusterCutMain()
{
    Env env;
    if (!boot(env, "test-scale-cluster-cut-ogre.log")) return 1;
    BakeInfo info;
    const size_t asked = kLargeAsset;
    const unsigned long long rss0 = rssKb();
    const QList<iris::MeshPtr> shell = requireShell(asked, &info);
    REQUIRE(!shell.isEmpty(), "the large asset (%zu triangles asked, %d pieces, %zu level-0 triangles)", asked,
            info.pieces, info.triangles);
    if (shell.isEmpty()) return 1;
    geometryDoc(env);
    const float radius = 10.0f;   // a building-sized asset: the shell's mean radius 1 -> 10 m
    const iris::Vec3 at(0, radius, 0);
    auto placed = placeModel(env, shell, at, radius);
    env.doc->getRootNode()->applyStaticDefaults();
    armMonitor(env);
    const struct { const char *name; float d; } poses[] = {
        { "inside bounds (1.3 R)", 1.3f * radius }, { "5 R", 5.0f * radius }, { "30 R", 30.0f * radius } };
    // TWO ARMS PER POSE, ONE PROCESS (V2-P0A fix round): the reference (Types.h clusterCut)
    // applies the frustum and NO occlusion, so it is compared with the id pass's occlusion
    // OFF (frustum-only, one list); with it ON (the shipped two-pass form) the early and
    // late lists' total must stay within the cut (occlusion only ever removes what is
    // hidden: a screen-filling shell hides whole pieces behind itself). Both arms must fit
    // the cut's stream: no instance on the coarse reserve, none missing.
    for (const auto &p : poses) {
        setCamera(env, at + iris::Vec3(0, 0.2f * radius, p.d), at);
        frame(env, 10);
        const IdRead on = readIdPass(env, 30);
        const AtomDrawStatus sOn = env.scene->atomDrawStatus();
        env.scene->setAtomOcclusionEnabled(false);
        frame(env, 10);
        const IdRead r = readIdPass(env, 30);
        const AtomDrawStatus sOff = env.scene->atomDrawStatus();
        env.scene->setAtomOcclusionEnabled(true);
        frame(env, 10);
        const size_t cut = cutTriangles(env, shell, at, radius);
        const double twoPass = on.tris + on.lateTris;
        std::printf("W3 %-24s occlusion OFF: id pass draws %10.0f tris | the cluster cut: %10zu tris | ratio %.2fx "
                    "(overflow %u, missing %u, budget %u indices) || occlusion ON: early %.0f + late %.0f = %.0f tris "
                    "(%.2fx the cut; overflow %u, missing %u, occluded %u) | id %s, decode %s GPU\n",
                    p.name, r.tris, cut, cut ? r.tris / double(cut) : 0.0, sOff.cutOverflow, sOff.cutMissing,
                    sOff.cutIndexBudget, on.tris, on.lateTris, twoPass, cut ? twoPass / double(cut) : 0.0,
                    sOn.cutOverflow, sOn.cutMissing, sOn.occluded, msText(on.idMs).c_str(), msText(on.decodeMs).c_str());
        const std::string what = std::string("triangles drawn at ") + p.name + ", occlusion off (the cut would draw " +
                                 std::to_string(cut) + ")";
        target("W3", r.tris, "tris", what.c_str());
        const std::string whatOn = std::string("triangles drawn at ") + p.name + ", occlusion on (early + late lists)";
        target("W3", twoPass, "tris", whatOn.c_str());
        // THE BAR THIS LANE CLOSES W3 WITH (ATOM-CLUSTER-CUT; the D1 convention: the bar
        // comes with the part that closes the wall): the id pass draws the cut, within
        // 1.2x of Types.h clusterCut at the same tolerance, either way — frustum against
        // frustum (occlusion off).
        REQUIRE(cut > 0 && r.tris <= 1.2 * double(cut) && r.tris * 1.2 >= double(cut),
                "W3 %s: with occlusion off the id pass draws %.0f tris, within 1.2x of the cut's %zu", p.name, r.tris, cut);
        REQUIRE(twoPass > 0 && twoPass <= 1.2 * double(cut),
                "W3 %s: with occlusion on the two lists draw %.0f tris, no more than 1.2x the cut's %zu", p.name, twoPass,
                cut);
        REQUIRE(sOff.cutValid && sOff.cutOverflow == 0 && sOff.cutMissing == 0 && sOn.cutOverflow == 0 &&
                    sOn.cutMissing == 0,
                "W3 %s: the cut fits its stream (occlusion off: %u overflowed, %u missing; on: %u, %u)", p.name,
                sOff.cutOverflow, sOff.cutMissing, sOn.cutOverflow, sOn.cutMissing);
        const std::string owed = std::string("id pass GPU ms on the ") + std::to_string(info.triangles) +
                                 "-triangle asset at " + p.name + " (decode " +
                                 (on.decodeMs >= 0 ? std::to_string(on.decodeMs) + " ms" : std::string("unsampled")) + ")";
        target("W3", on.idMs, "ms", owed.c_str());
    }
    std::printf("   memory: RSS %.0f MB before the asset, %.0f MB now, peak %.0f MB\n", double(rss0) / 1024.0,
                double(rssKb()) / 1024.0, double(peakRssKb()) / 1024.0);
    // THE OWED ROW: the id pass (and the decode) on EACH cached asset, inside its bounds
    // (the whole-mesh level 0 — the worst case the id pass draws today).
    for (auto &n : placed) n->removeFromParent();
    frame(env, 10);
    for (size_t t : { size_t(1000000), size_t(5000000), size_t(10000000) }) {
        BakeInfo bi;
        const QList<iris::MeshPtr> m = requireShell(t, &bi);
        if (m.isEmpty()) { ++failures; continue; }
        auto nodes = placeModel(env, m, at, radius);
        env.doc->getRootNode()->applyStaticDefaults();
        setCamera(env, at + iris::Vec3(0, 0.2f * radius, 1.3f * radius), at);
        frame(env, 10);
        const IdRead r = readIdPass(env, 30);
        std::printf("OWED id pass on %s (%zu tris, %d pieces) inside its bounds: draws %.0f tris, id %s, decode %s GPU\n",
                    qPrintable(bi.name), bi.triangles, bi.pieces, r.tris, msText(r.idMs).c_str(),
                    msText(r.decodeMs).c_str());
        for (auto &n : nodes) n->removeFromParent();
        frame(env, 10);
    }
    std::printf("   VS invocations: not available (the monitor has no pipeline-statistics query)\n");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.cut_cost — D1's decision number (ATOM-CLUSTER-CUT, SPECS/v2/CLUSTER_CUT_DESIGN.md
// D1): THE FLAT CUT'S COST AT 10k INSTANCES. The world fixture, the id pass's own request
// (visible | Atom, one sample x the LOD bias, mode 3) at five poses across it. THE GPU
// NUMBER IS THE ID PASS'S OWN ROW (the monitor's timestamp pair around the pass: the four
// cull jobs AND the draw), which bounds the cut from above — and each cull job is its own
// row inside it (lane TEST-1: "id.cull.cut" ...; the wall-clock "slopes" that stood here
// measured the CPU's recording and submission, not the GPU, and are deleted).
// The design owes the hierarchical traversal (E) IF the flat evaluation reads above 0.5 ms.
// ===========================================================================
static int cutCostMain()
{
    Env env;
    World w;
    if (!bootWorld(env, w, "test-scale-cut-cost-ogre.log")) return 1;
    frame(env, 10);
    std::vector<double> cutMs, emitMs, testMs, compactMs, idMs, evaluated, clusters, tris, indices;
    unsigned overflow = 0, sampled = 0;
    for (int s = 0; s < 5; ++s) {
        const float x = -120.0f + 60.0f * float(s);
        setCamera(env, iris::Vec3(x, 12.0f, 60.0f), iris::Vec3(x + 20.0f, 0.0f, -60.0f));
        frame(env, 6);
        const IdRead ir = readIdPass(env, 20);
        GpuCullRequest req;
        if (!env.engine->fillCullView(env.view, req)) continue;
        req.flagsRequired = 1u | 512u;   // visible | ATOM (GpuSceneEntry::flags, Types.h)
        req.pixelTolerance = kLodBudgetPixels * env.scene->lodBias();
        req.mode = 3u;
        GpuCullResult r;
        if (!env.engine->gpuCull(env.scene, env.view, req, false, r)) continue;
        ++sampled;
        overflow += r.cutOverflow;
        if (ir.cullMs[0] >= 0) testMs.push_back(ir.cullMs[0]);
        if (ir.cullMs[1] >= 0) compactMs.push_back(ir.cullMs[1]);
        if (ir.cullMs[2] >= 0) cutMs.push_back(ir.cullMs[2]);
        if (ir.cullMs[3] >= 0) emitMs.push_back(ir.cullMs[3]);
        if (ir.idMs >= 0) idMs.push_back(ir.idMs);
        evaluated.push_back(r.cutEvaluated);
        clusters.push_back(r.cutClusters);
        tris.push_back(r.cutTriangles);
        indices.push_back(r.cutIndices);
        std::printf("CUT pose %d: %u of %u instances survive, %u (instance, cluster) pairs evaluated, %u clusters / %u "
                    "tris / %u indices drawn (budget %u, overflow %u) | test %s compact %s CUT %s emit %s GPU "
                    "(the id pass's rows) | the id pass %s GPU\n",
                    s, r.survivors, r.instances, r.cutEvaluated, r.cutClusters, r.cutTriangles, r.cutIndices,
                    r.cutIndexBudget, r.cutOverflow, msText(ir.cullMs[0]).c_str(), msText(ir.cullMs[1]).c_str(),
                    msText(ir.cullMs[2]).c_str(), msText(ir.cullMs[3]).c_str(), msText(ir.idMs).c_str());
    }
    REQUIRE(sampled >= 3, "the cut was measured at %u poses", sampled);
    REQUIRE(overflow == 0, "no pose overflowed the cut's stream (%u)", overflow);
    std::printf("D1 the id pass's other cull jobs, median GPU ms: test %s compact %s\n",
                msText(testMs.empty() ? -1.0 : stats(testMs).median).c_str(),
                msText(compactMs.empty() ? -1.0 : stats(compactMs).median).c_str());
    // THE CUT'S OWN GPU MS (lane TEST-1: the job's monitor row inside the id pass; it was a
    // CPU recording slope, so the trend steps here once — a re-definition, not a regression).
    target("D1", cutMs.empty() ? -1.0 : stats(cutMs).median, "ms",
           "the cut job's GPU ms per frame (its monitor row inside the id pass)");
    target("D1", emitMs.empty() ? -1.0 : stats(emitMs).median, "ms", "the emit job's, likewise");
    target("D1", stats(evaluated).median, "pairs", "(instance, cluster) pairs the rule evaluated");
    target("D1", stats(tris).median, "tris", "triangles the cut draws");
    target("D1", stats(indices).median, "indices", "the compacted stream per frame");
    if (!idMs.empty())
        target("D1", stats(idMs).median, "ms", "THE GPU BOUND: the id pass (the four cull jobs + the draw), the "
               "monitor's row, locked clocks (the flat cut's ms at 10k instances is below it)");
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.levels — W4: THE COARSEST END IS REACHABLE. Until ATOM-CLUSTER-CUT only the
// finest 8 LEVELS reached the GPU path (GpuScene::kLevelsPerMesh); the id pass now draws
// the cut, whose terminal groups' clusters are drawn when nothing finer is affordable.
// Number: the triangles the id pass draws for the asset at 1 km against the chain's
// coarsest level (the level table keeps 8 levels for the voxeliser and the casters, D6).
// ===========================================================================
static int levelsMain()
{
    Env env;
    if (!boot(env, "test-scale-levels-ogre.log")) return 1;
    BakeInfo info;
    const QList<iris::MeshPtr> shell = requireShell(kLargeAsset, &info);
    if (shell.isEmpty()) { REQUIRE(false, "the large asset (the 10 M shell)"); return 1; }
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
    const AtomDrawStatus st = env.scene->atomDrawStatus();
    const size_t cut = cutTriangles(env, shell, at, radius);
    const size_t root = cutTriangles(env, shell, at, radius, true);
    const double drawn = r.tris + r.lateTris;
    std::printf("W4 at 1 km: the id pass draws %.0f tris (early %.0f + late %.0f; overflow %u) | the CPU cut at the "
                "same pose %zu | THE DAG ROOT (the coarsest the DAG can draw) %zu = %.0f a piece | the LOD chain's "
                "coarsest level %zu (a different structure: the voxeliser's and the casters')\n",
                drawn, r.tris, r.lateTris, st.cutOverflow, cut, root, info.pieces ? double(root) / info.pieces : 0.0,
                info.coarsestTriangles);
    target("W4", double(info.levels), "levels", "the asset's chain length (longest piece, level 0 included)");
    target("W4", drawn, "tris", "triangles the id pass's CUT draws for the asset at 1 km");
    // THE DAG'S FLOOR (a BAKE property, V2-P0A's verdict): the split's pieces keep their open
    // borders LOCKED through the simplification (meshbake.cpp, the locked-border pass), so the
    // groups along the seams stop converging — the 10 M bake's log reads 48-210 UNCONVERGED
    // groups a piece (735-1,552 retries) and only 0-3 made terminal — and every piece's root
    // keeps ~2.36 k triangles, 11x the chain's coarsest level (whose levels are not locked).
    // A finding for the audit spec, not a cut defect: the cut reaches this root.
    target("W4", double(root), "tris", "THE DAG FLOOR: the 10 M shell's DAG root over its pieces (the locked seams)");
    // THE BARS (ATOM-CLUSTER-CUT closes W4), LIKE FOR LIKE: the id pass draws the CPU cut at
    // 1 km (within 1.2x), and that cut has reached the DAG's root (within 1.2x of it) —
    // the coarsest end the DAG holds is reachable.
    REQUIRE(cut > 0 && drawn <= 1.2 * double(cut) && drawn * 1.2 >= double(cut),
            "W4: at 1 km the id pass draws %.0f tris, within 1.2x of the CPU cut's %zu", drawn, cut);
    REQUIRE(root > 0 && double(cut) <= 1.2 * double(root),
            "W4: at 1 km the cut (%zu) reaches the DAG's root (%zu) within 1.2x", cut, root);
    REQUIRE(st.cutOverflow == 0 && st.cutMissing == 0, "W4: the cut fits its stream (%u overflowed, %u missing)",
            st.cutOverflow, st.cutMissing);
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
// Number: the VRAM each shell takes when attached, measured through the ENGINE'S OWN STATS
// (MemoryStats: the VaoManager's pools, capacity - free) — there is no vmaStats door on this
// boundary — beside the 48 B/vertex formula.
// THE FIRST MESH PAYS THE ENGINE'S ONE-TIME ALLOCATIONS (V2-P0A: the "24.6 -> 87.4 MB at
// 250 k rise" was this): the first asset a process attaches allocates the cut's and the
// casters' buffers every later asset shares, so D1's 250 k row (measured FOURTH, after the
// 10 M) read the formula exactly while every run since (the big shells uncached, the 250 k
// measured FIRST) read the formula + the one-time term (63 MB, ~14 MB of it arrived with
// ATOM-SHADOWS-1's caster buffers). So the row attaches the 250 k shell once COLD (its delta
// less its warm delta is the one-time term), releases it, and then measures every shell WARM.
// THE BARS: each shell's warm VRAM within 1.15x of the formula (measured +0 / +12 / +9 / +7 %
// at 250 k / 1 M / 5 M / 10 M), and the one-time term at most 72 MB (62.9 measured, +15 %).
// ===========================================================================
static int residencyMain()
{
    Env env;
    if (!boot(env, "test-scale-residency-ogre.log")) return 1;
    geometryDoc(env);
    armMonitor(env);
    setCamera(env, iris::Vec3(0, 12, 40), iris::Vec3(0, 10, 0));
    frame(env, 20);
    struct Delta { double used = 0, capacity = 0, formula = 0; };
    // Attach `t`'s shell, read the pool's growth and the formula, release it again.
    auto measure = [&](size_t t, const char *how, Delta &d) -> bool {
        BakeInfo info;
        QList<iris::MeshPtr> shell = requireShell(t, &info);
        if (shell.isEmpty()) return false;
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
            MeshData md;
            if (!SceneMirror::toMeshData(m.data(), md)) continue;
            verts += md.positions.size() / 3;
            levelIdx += md.indices.size();
            for (const auto &l : md.lodIndices) levelIdx += l.size();
            clusterIdx += md.clusterIndices.size();
        }
        const double mainMB = (double(verts) * 48.0 + double(levelIdx) * 4.0) / 1048576.0;
        const double shadowMB = (double(verts) * 12.0 + double(levelIdx) * 4.0) / 1048576.0;
        const double clusterMB = double(clusterIdx) * 4.0 / 1048576.0;
        d.used = (used1 - used0) / 1048576.0;
        d.capacity = double(m1.gpuPoolCapacityBytes - m0.gpuPoolCapacityBytes) / 1048576.0;
        d.formula = mainMB + shadowMB + clusterMB;
        std::printf("W5 %-14s %-5s %d pieces %10zu tris %9zu verts | pool used +%8.1f MB (capacity +%8.1f MB) | "
                    "formula: levels %.1f + shadow chain %.1f + cluster stream %.1f = %.1f MB | over it %+.1f MB | "
                    "RSS %.0f MB\n",
                    qPrintable(info.name), how, info.pieces, info.triangles, verts, d.used, d.capacity, mainMB,
                    shadowMB, clusterMB, d.formula, d.used - d.formula, double(rssKb()) / 1024.0);
        std::fflush(stdout);
        for (auto &n : nodes) n->removeFromParent();
        nodes.clear();
        shell.clear();
        frame(env, 20);
        return true;
    };
    Delta cold;
    if (!measure(250000, "COLD", cold)) { ++failures; shutdown(env); return 1; }
    int measured = 0;
    for (size_t t : { size_t(250000), size_t(1000000), size_t(5000000), size_t(10000000) }) {
        Delta d;
        if (!measure(t, "warm", d)) { ++failures; continue; }
        ++measured;
        const std::string what = "VRAM (pool used) per " + std::to_string(t) + "-triangle shell, warm";
        target("W5", d.used, "MB", what.c_str(), "<= 1.15x the formula");
        REQUIRE(d.used <= 1.15 * d.formula, "W5 %zu: warm VRAM %.1f MB within 1.15x of the formula's %.1f MB", t, d.used,
                d.formula);
        if (t == 250000) {
            std::printf("W5 the first mesh's one-time allocations: %.1f MB (the 250 k shell cold %.1f, warm %.1f)\n",
                        cold.used - d.used, cold.used, d.used);
            target("W5", cold.used - d.used, "MB", "the engine's one-time allocations the FIRST mesh a process "
                   "attaches pays (the cut's and casters' buffers: the 250 k shell cold less warm)", "<= 72 MB");
            REQUIRE(cold.used - d.used <= 72.0, "W5: the first mesh's one-time allocations %.1f MB <= 72 MB",
                    cold.used - d.used);
        }
    }
    REQUIRE(measured == 4, "the four shells were measured warm (%d)", measured);
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
        std::printf("W6 materials %4d textures %4d -> buckets %4u screen draws %4u decode draws %4u | decode GPU %s"
                    " (the decode pass alone %s; id %s)\n",
                    a.materials, a.textures, st.buckets, st.screenDraws, st.decodeDraws, msText(r.decodeMs).c_str(),
                    msText(r.decodePassMs).c_str(), msText(r.idMs).c_str());
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
        // THE CLASSIFICATION'S BAR (ATOM-DECODE-CLASS-1): the decode's cost independent
        // of the bucket count — the most buckets within 1.5x of the fewest.
        if (rows.front().second > 0)
            target("W6", rows.back().second / rows.front().second, "x",
                   "the decode at the most buckets over the fewest (1080p)", "<= 1.5");
    }
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// atom.decode_exact — THE CLASSIFICATION CHANGES WHO SHADES A PIXEL, NEVER WHAT IT
// SHADES (ATOM-DECODE-CLASS-1). D1's world (10,000 instances) wearing ~200 buckets,
// one still pose, both chain shapes — no prepass (the opaque decode pass classifies)
// and the SSR prepass (the prepass's decode pass classifies, the opaque one loads).
// THE PROOF THAT THE PICTURE IS THE LATE-DISCARD LOOP'S was this suite's first form,
// run before that loop was deleted (in one process, both shapes, byte for byte under a
// magenta clear: 0 px — spikes/atom-decode-class-1/decode-exact-pre-deletion.log).
// What stays is what can be asked of the classified decode alone:
//   STILL   each shape's picture repeats byte for byte (a class that flickers between
//           buckets, or a missed EQUAL test, would not);
//   HOLES   (no prepass) a pixel the id pass covered that follows the clear colour is
//           one no draw wrote — a bucket draw's EQUAL test that missed its surface, or
//           a surface handed to the wrong bucket (the decode's guard discards it). The
//           Atom view (Objects), which paints every covered pixel, says which pixels
//           follow the clear anyway (this world's night sky is the clear). Bloom, SMAA
//           and SSAO off for it (each carries a neighbour into a pixel). The base's own
//           seams are the bar (below); the NEGATIVE CONTROL (the arm "atom.decode" = 0:
//           no decode at all) must see the Atom items.
// A DETERMINISTIC PICTURE: every term that moves between frames of a still pose is
// off and the same in every read — the output dither (JAHSHAKA_NO_DITHER), the ray
// tier (JAHSHAKA_NO_RAY_QUERY: stochastic reflections), Photon (the field's and the
// voxels' updates), the auto exposure (Manual). A picture is taken when it stops
// moving: read every 16 frames until two consecutive reads are the same bytes (at
// most 12 reads).
// ===========================================================================
static int decodeExactMain()
{
    setenv("JAHSHAKA_NO_DITHER", "1", 1);
    setenv("JAHSHAKA_NO_RAY_QUERY", "1", 1);
    Env env;
    World w;
    WorldSpec spec = denseWorld();   // D1's dense world: a pixel bar on a dense scene (V2-P0A)
    spec.materials = 1;
    if (!bootWorld(env, w, "test-atom-decode-exact-ogre.log", spec)) return 1;
    applyMaterials(w, 200, 200);
    env.doc->exposureMode = iris::ExposureMode::Manual;
    worldmodes::setMode(env.doc, worldmodes::Mode::High);
    worldmodes::setPhoton(env.doc, false, worldmodes::PhotonTier::High);

    auto meanOf = [](const Image &img) {
        double mean = 0;
        for (size_t i = 0; i < img.rgba.size(); i += 4) mean += img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2];
        return mean / double(std::max<size_t>(1, img.rgba.size() / 4 * 3));
    };
    auto differing = [](const Image &x, const Image &y) {
        size_t n = 0;
        if (x.rgba.size() != y.rgba.size()) return size_t(~0ull);
        for (size_t i = 0; i < x.rgba.size(); i += 4) n += std::memcmp(&x.rgba[i], &y.rgba[i], 4) != 0;
        return n;
    };
    auto still = [&](const char *what, Image &img) {
        Image prev;
        // A switch compiles the other arm's permutations and a clear-colour change
        // rebuilds the chain: 64 frames before the first read.
        frame(env, 64);
        if (!env.view->readPixels(prev) || !prev.width) return false;
        for (int t = 1; t <= 12; ++t) {
            frame(env, 16);
            if (!env.view->readPixels(img) || !img.width) return false;
            const size_t moved = differing(img, prev);
            if (moved == 0u) {
                std::printf("decode_exact:   %s: still after %d reads (mean %.2f)\n", what, t + 1, meanOf(img));
                return true;
            }
            std::printf("decode_exact:   %s: read %d moved %zu px (mean %.2f)\n", what, t, moved, meanOf(img));
            prev.rgba.swap(img.rgba);
        }
        return false;
    };
    auto save = [&](const Image &img, const std::string &name) {
        if (qgetenv("JAH_SCALE_SHOT").isEmpty()) return;
        QImage q(img.rgba.data(), int(img.width), int(img.height), QImage::Format_RGBA8888);
        q.save(QString::fromLocal8Bit(qgetenv("JAH_SCALE_SHOT")) + QString::fromStdString(name));
    };
    const Colour bgA(0.0f, 0.0f, 0.0f, 1.0f), bgB(1.0f, 0.0f, 1.0f, 1.0f);
    for (int shape = 0; shape < 2; ++shape) {
        const char *shapeName = shape ? "the SSR prepass" : "no prepass";
        worldmodes::setRowValue(env.doc, QStringLiteral("ssr"), shape ? 1 : 0);
        env.view->setBackground(bgA);
        // SETTLED: the 200 textures landed (a datablock still baking is PENDING and
        // drawn by PBS; every landing re-routes items) and the frames after it.
        int settle = 0;
        for (; settle < 3000; ++settle) {
            frame(env, 1);
            if (settle > 300 && env.scene->atomDrawStatus().pending == 0u) break;
        }
        pathStill(env, 120);
        const AtomDrawStatus st = env.scene->atomDrawStatus();
        bool prepassRan = false, decodeRan = false;
        {
            const auto recs = collect(env, [&] { frame(env, 2); });
            for (const FrameRecord &r : recs) {
                prepassRan = prepassRan || passNamed(r, "Jahshaka atom decode prepass");
                decodeRan = decodeRan || passNamed(r, "Jahshaka atom decode");
            }
        }
        std::printf("decode_exact: [%s] settled %d frames | buckets %u, screen draws %u, atom items %u | decode passes: "
                    "prepass %d, opaque %d\n", shapeName, settle, st.buckets, st.screenDraws, st.atomItems,
                    int(prepassRan), int(decodeRan));
        REQUIRE(st.buckets >= 150u && st.screenDraws == st.buckets, "[%s] the world wears ~200 buckets, one draw each",
                shapeName);
        REQUIRE(decodeRan && prepassRan == (shape == 1), "[%s] the chain carries its decode passes", shapeName);

        // STILL: the classified picture repeats.
        Image a;
        const bool sa = still("classified", a);
        REQUIRE(sa, "[%s] the classified picture stopped moving", shapeName);
        save(a, std::string("decode-exact-") + (shape ? "prepass" : "fwd") + "-classified.png");
    }
    // HOLES (no prepass): bloom, SMAA and SSAO off (each carries a neighbour into a
    // pixel), then the pixels that follow the clear with the Atom view on (never
    // covered by the id pass) and off. A pixel counts as COVERED only when every pixel
    // within two of it is too: the view's quad places its mask off the scene's along a
    // silhouette (measured: rings of one to two pixels along silhouettes against the
    // sky, nothing inside a surface) — a bucket's EQUAL test that missed would leave
    // its surfaces, not their outlines.
    const char *shapeName = "no prepass";
    worldmodes::setRowValue(env.doc, QStringLiteral("ssr"), 0);
    worldmodes::setRowValue(env.doc, QStringLiteral("bloom"), 0);
    worldmodes::setRowValue(env.doc, QStringLiteral("smaa"), 0);
    worldmodes::setRowValue(env.doc, QStringLiteral("ssao"), 0);
    pathStill(env, 120);
    auto follow = [&](const char *what, std::vector<bool> &mask) {
        Image pb, pm;
        env.view->setBackground(bgA);
        bool ok = still(what, pb);
        env.view->setBackground(bgB);
        ok = still(what, pm) && ok;
        env.view->setBackground(bgA);
        mask.assign(pb.rgba.size() / 4, false);
        for (size_t i = 0; i < mask.size() && i * 4 + 3 < pm.rgba.size(); ++i)
            mask[i] = std::memcmp(&pb.rgba[i * 4], &pm.rgba[i * 4], 4) != 0;
        return ok;
    };
    std::vector<bool> uncovered, classifiedFollow, noDecodeFollow;
    env.scene->setAtomView(AtomView::Objects);
    const bool su = follow("the Atom view", uncovered);
    env.scene->setAtomView(AtomView::Off);
    const bool sf = follow("classified", classifiedFollow);
    env.engine->setArm("atom.decode", 0.0);
    const bool sn = follow("no decode", noDecodeFollow);
    env.engine->setArm("atom.decode", 1.0);
    if (!qgetenv("JAH_SCALE_SHOT").isEmpty()) {
        Image m;
        m.width = 1920; m.height = 1080;
        m.rgba.assign(size_t(m.width) * m.height * 4, 0);
        for (size_t i = 0; i < uncovered.size() && i < m.rgba.size() / 4; ++i) {
            m.rgba[i * 4 + 0] = (!uncovered[i] && classifiedFollow[i]) ? 255 : 0;   // before the erosion
            m.rgba[i * 4 + 1] = uncovered[i] ? 60 : 0;
            m.rgba[i * 4 + 3] = 255;
        }
        save(m, "decode-exact-holes.png");
    }
    size_t sky = 0, holes = 0, controlHoles = 0;
    const int W = 1920, H = 1080;
    auto coveredCore = [&](int x, int y) {
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const int nx = std::clamp(x + dx, 0, W - 1), ny = std::clamp(y + dy, 0, H - 1);
                if (uncovered[size_t(ny) * W + nx]) return false;
            }
        return true;
    };
    if (uncovered.size() != size_t(W) * H) uncovered.assign(size_t(W) * H, true);   // an unread picture covers nothing
    for (size_t i = 0; i < uncovered.size(); ++i) {
        sky += uncovered[i];
        if (!coveredCore(int(i % W), int(i / W))) continue;
        holes += i < classifiedFollow.size() && classifiedFollow[i];
        controlHoles += i < noDecodeFollow.size() && noDecodeFollow[i];
    }
    std::printf("decode_exact: [%s] never covered (the Atom view follows the clear) %zu px | holes %zu | the negative "
                "control (no decode) %zu\n", shapeName, sky, holes, controlHoles);
    REQUIRE(su && sf && sn, "[%s] the hole pictures stopped moving", shapeName);
    // THE BAR IS NOT ZERO: 99 px of one- to two-pixel seams INSIDE surfaces the id
    // pass covered, which the decode's own covering test refuses, measured identical
    // on the late-discard loop (the first form's EXACT arms, under the magenta clear)
    // — a pre-existing finding of ATOM-DECODE-CLASS-1's. A bucket whose EQUAL test
    // missed leaves its surfaces.
    REQUIRE(holes <= 1000u, "[%s] no surface the id pass covered is left unwritten (%zu px)", shapeName, holes);
    REQUIRE(controlHoles > 100000u, "[%s] the hole test sees the Atom items when nothing decodes them", shapeName);
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// scale.occlusion — W7, CLOSED BY ATOM-OCCLUSION-1: THE ID PASS DRAWS WHAT THE CAMERA CAN SEE.
// The id pass culls the Atom queue against the depth pyramid in the two-pass form
// (OgreAtomIdPass.cpp: the previous frame's pyramid, then the rejected set again against
// the one built from the first pass's depth). Three arms over the SAME 8 poses of the walk,
// one process (the measuring door world.setAtomOcclusion = Scene::setAtomOcclusionEnabled):
//   OFF  the door shut: the frustum-only id pass (before this lane), with the view's own
//        pyramid of the COMPLETE depth switched on so that THE REFERENCE can be taken —
//        the id pass's own request (the cut, mode 3) replayed through Engine::gpuCull
//        against that pyramid: what a single cull against this frame's whole depth keeps;
//   ON   the product: the door open, no pyramid of the view's own (the id pass's alone);
//   ON+  the door open WITH the view's complete-depth pyramid rebuilt after the opaque pass
//        (the design question: does the first cull gain from the complete depth).
// THE BAR (brief §1): the ON arm draws within 1.25x of the reference (median over the
// walk), and its SETTLED id image equals the OFF arm's word for word at every pose. The
// frame-by-frame proof — the first frame after a camera cut included — is
// atom.occlusion_exact's (two worlds in lockstep): this world's first frames after a jump
// are not a picture of the pose even frustum-only (settledIds says why).
// ===========================================================================
static bool readIdsAt(Env &env, std::vector<uint32_t> &ids)
{
    unsigned w = 0, h = 0;
    return env.engine->readAtomIds(env.view, ids, w, h) && !ids.empty();
}
static size_t idDiff(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, size_t &missing)
{
    missing = 0;
    if (a.size() != b.size()) return ~size_t(0);
    size_t d = 0;
    for (size_t p = 0; p + 1 < a.size(); p += 2)
        if (a[p] != b[p] || a[p + 1] != b[p + 1]) {
            ++d;
            if (a[p] == 0xFFFFFFFFu) ++missing;
        }
    return d;
}
/// THE SETTLED ID IMAGE at a still pose: frames until two CONSECUTIVE images are equal
/// (at most 40). NOT a formality — the finding this lane reports: on the base (cee4f48fd)
/// the 10k world's id image is intermittently GARBAGE for a few frames after a camera jump
/// (frames 1, 4, 7 after it; up to the whole image; both readback routes), with the
/// occlusion door shut as well — so one read after N frames is not a picture of the pose.
/// `unstable` counts the reads that disagreed with the frame after them.
static bool settledIds(Env &env, std::vector<uint32_t> &ids, unsigned &unstable)
{
    std::vector<uint32_t> prev;
    frame(env, 1);
    if (!readIdsAt(env, prev)) return false;
    for (int f = 0; f < 40; ++f) {
        frame(env, 1);
        if (!readIdsAt(env, ids)) return false;
        size_t m = 0;
        if (idDiff(ids, prev, m) == 0) return true;
        ++unstable;
        prev.swap(ids);
    }
    return false;
}

static int occlusionMain()
{
    Env env;
    World w;
    // D1's DENSE WORLD: the bar is a ratio on a dense scene, not a scale stick (V2-P0A).
    if (!bootWorld(env, w, "test-scale-occlusion-ogre.log", denseWorld())) return 1;
    const int kPoses = 8, kSettle = 10;
    auto pose = [&](int s) {
        const float x = -90.0f + 25.0f * float(s);
        setCamera(env, iris::Vec3(x, 1.7f, 0.0f), iris::Vec3(x + 10.0f, 1.7f, -2.0f));
    };
    std::vector<std::vector<uint32_t>> offIds(kPoses);
    std::vector<double> offTris(kPoses, 0.0), refTris(kPoses, 0.0), onTris(kPoses, 0.0), onPlusTris(kPoses, 0.0);
    std::vector<unsigned> occluded(kPoses, 0u), disoccluded(kPoses, 0u), refOccluded(kPoses, 0u);
    unsigned bad = 0, missingPx = 0, comparedPoses = 0, unstableOff = 0, unstableOn = 0;

    // ---- OFF + the reference ------------------------------------------------------
    env.scene->setAtomOcclusionEnabled(false);
    env.fxOverride = [](PostFxDesc &fx) { fx.hzb = true; fx.hzbFarthest = true; };
    frame(env, 10);
    for (int s = 0; s < kPoses; ++s) {
        pose(s);
        frame(env, kSettle);
        const AtomDrawStatus st = env.scene->atomDrawStatus();
        offTris[s] = double(st.cutTriangles);
        if (!settledIds(env, offIds[s], unstableOff)) offIds[s].clear();
        HzbStatus hz;
        GpuCullRequest req;
        if (env.engine->hzbStatus(env.view, hz) && hz.primed && env.engine->fillCullView(env.view, req)) {
            req.flagsRequired = 1u | 512u;   // visible | ATOM (GpuSceneEntry::flags, Types.h): the id pass's
            req.pixelTolerance = kLodBudgetPixels * env.scene->lodBias();
            req.mode = 3u;
            req.hzbLevels = hz.levels;
            GpuCullResult ref;
            if (env.engine->gpuCull(env.scene, env.view, req, false, ref)) {
                refTris[s] = double(ref.cutTriangles);
                refOccluded[s] = ref.occluded;
            }
        }
    }
    // ---- ON (the product) -----------------------------------------------------------
    env.scene->setAtomOcclusionEnabled(true);
    env.fxOverride = nullptr;
    frame(env, 10);
    for (int s = 0; s < kPoses; ++s) {
        pose(s);
        frame(env, kSettle);
        const AtomDrawStatus st = env.scene->atomDrawStatus();
        onTris[s] = double(st.cutTriangles);
        occluded[s] = st.occluded;
        disoccluded[s] = st.disoccluded;
        std::vector<uint32_t> settled;
        const bool ok = settledIds(env, settled, unstableOn);
        size_t m = 0;
        const size_t d = ok && !offIds[s].empty() ? idDiff(settled, offIds[s], m) : ~size_t(0);
        if (d != ~size_t(0)) ++comparedPoses;
        if (d) ++bad;
        missingPx += unsigned(m);
        std::printf("W7 x %6.1f: frustum-only %9.0f tris | reference (one cull, this frame's whole depth) %9.0f "
                    "(%u rejected) | two-pass %9.0f (occluded %u, disoccluded %u) = %.2fx the reference, %.2fx fewer "
                    "than frustum-only | id image vs frustum-only: %zu px differ (%zu empty with the occlusion)\n",
                    -90.0 + 25.0 * s, offTris[s], refTris[s], refOccluded[s], onTris[s], occluded[s], disoccluded[s],
                    refTris[s] > 0 ? onTris[s] / refTris[s] : 0.0, onTris[s] > 0 ? offTris[s] / onTris[s] : 0.0,
                    d == ~size_t(0) ? size_t(0) : d, m);
    }
    // ---- ON+ (the complete-depth pyramid rebuilt after the opaque pass too) ---------
    env.fxOverride = [](PostFxDesc &fx) { fx.hzb = true; fx.hzbFarthest = true; };
    frame(env, 10);
    for (int s = 0; s < kPoses; ++s) {
        pose(s);
        frame(env, kSettle);
        onPlusTris[s] = double(env.scene->atomDrawStatus().cutTriangles);
    }
    env.fxOverride = nullptr;

    std::vector<double> ratio, gain, ratioPlus;
    for (int s = 0; s < kPoses; ++s) {
        if (refTris[s] > 0 && onTris[s] > 0) {
            ratio.push_back(onTris[s] / refTris[s]);
            gain.push_back(offTris[s] / onTris[s]);
        }
        if (refTris[s] > 0 && onPlusTris[s] > 0) ratioPlus.push_back(onPlusTris[s] / refTris[s]);
        std::printf("W7 x %6.1f: the ON+ arm (the complete-depth pyramid first) %9.0f tris = %.2fx the reference\n",
                    -90.0 + 25.0 * s, onPlusTris[s], refTris[s] > 0 ? onPlusTris[s] / refTris[s] : 0.0);
    }
    std::printf("W7 settle: reads that disagreed with the next frame's before two agreed — frustum-only %u, two-pass %u "
                "(the base's id-image transient after a jump; see settledIds)\n", unstableOff, unstableOn);
    const double medRatio = stats(ratio).median;
    REQUIRE(comparedPoses == unsigned(kPoses), "every pose's settled id images were read in both arms (%u of %d)",
            comparedPoses, kPoses);
    REQUIRE(bad == 0u,
            "the settled id image with the occlusion equals the frustum-only one at every pose (%u differ; %u pixels "
            "empty with the occlusion)", bad, missingPx);
    REQUIRE(!ratio.empty() && medRatio <= 1.25,
            "the two-pass id pass draws within 1.25x of the reference cull (median %.3fx over %zu poses)", medRatio,
            ratio.size());
    target("W7", stats(offTris).median, "tris", "drawn per frame on the walk, frustum only (the door shut)");
    target("W7", stats(refTris).median, "tris", "the reference: one cull against this frame's whole depth");
    target("W7", stats(onTris).median, "tris", "drawn per frame on the walk, the two-pass occlusion (the product)");
    target("W7", stats(gain).median, "x", "frustum-only / two-pass, median over the walk");
    target("W7", medRatio, "x", "two-pass / the reference, median over the walk", "<= 1.25");
    target("W7", stats(ratioPlus).median, "x", "the ON+ arm (the complete-depth pyramid first) / the reference");
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
                "wanted %zu cards on %zu instances | lights in range of the relit cards %u, over the 64 %u\n",
                c.pages, c.pageSize, c.pagesUsed, double(c.residencyRadius), c.cardsResident, c.instancesResident,
                wanted, instances, c.lightsInRange, c.lightsDropped);
    // THE ONE LIGHT LIST on the P0 world (P1C-LIGHT-LIST): each cascade's lights, culled by range
    // against its box and over its budget by contribution — the overflow a count, never silent.
    {
        const GiStatus st = env.scene->giStatus();
        for (size_t k = 0; k < st.cascades.size(); ++k)
            std::printf("W9/W2 cascade %zu (half %.1f m): lights in range %d, injected %d, over budget %d\n", k,
                        double(st.cascades[k].halfSize), st.cascades[k].lightsInRange,
                        st.cascades[k].lightsInjected, st.cascades[k].lightsOverBudget);
    }
    target("W9", double(c.cardsResident), "cards", ("held, of " + std::to_string(wanted) + " wanted inside the radius").c_str());
    target("W9", double(c.pagesUsed), "pages", ("used of " + std::to_string(c.pages) + " (the fixed 2k atlas)").c_str());
    target("W9", double(c.lightsDropped), "lights",
           "in range of the relit cards but over the relight's 64 (the dimmest there; 500 lamps in the world)");
    // ONE LAMP MOVE: the captures and relights it costs until the queues drain.
    const unsigned long long cap0 = c.captures, inv0 = c.invalidSun, rel0 = c.relights;
    iris::LightNodePtr lamp = w.lights[w.lights.size() / 2];
    lamp->setLocalPos(lamp->getLocalPos() + iris::Vec3(1.0f, 0, 0));
    int f = 0;
    for (; f < 900; ++f) {
        frame(env, 1);
        const CardCacheStatus s = env.scene->giStatus().cards;
        if (f > 10 && s.queueLength == 0 && s.capturesLastFrame == 0 && s.relitLastFrame == 0) break;
    }
    const CardCacheStatus c1 = env.scene->giStatus().cards;
    std::printf("W9 one lamp moved 1 m: recaptures %llu, sun invalidations %llu, relights %llu over %d frames\n",
                (unsigned long long)(c1.captures - cap0), (unsigned long long)(c1.invalidSun - inv0),
                (unsigned long long)(c1.relights - rel0), f);
    target("W9", double(c1.captures - cap0), "captures", "recaptures one lamp move costs (0: a capture holds no light quantity)");
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
    // THE 250 k BAKE IS FORCED IN A PRIVATE DIRECTORY (a cache hit measures nothing, and the
    // shared blob other trees read is never deleted or raced).
    BakeInfo here;
    QList<iris::MeshPtr> m;
    {
        QTemporaryDir priv(QDir::tempPath() + "/scale-bake-XXXXXX");
        ScopedCacheDir scope(priv.path());
        m = shellAsset(250000, &here, true);
    }
    REQUIRE(!m.isEmpty() && !here.fromCache && here.bakeMs > 0, "a 250 k shell baked through the import door");
    row(here, "baked now");
    target("W11", here.bakeMs / 1000.0 / (double(here.triangles) / 1e6), "s/MT", "the bake's seconds per million triangles (250 k, Debug)");
    target("W11", here.bakeMs > 0 ? 100.0 * here.dagMs / here.bakeMs : 0.0, "%", "the cluster-DAG stage's share of the bake");
    for (size_t t : { size_t(1000000), size_t(5000000), size_t(10000000) }) {
        BakeInfo c;
        if (requireShell(t, &c).isEmpty()) { ++failures; continue; }
        row(c, c.bakeMs > 0 ? "scale_assets_gen's record" : "cached, no record");
        REQUIRE(c.bakeMs > 0, "the %zu-triangle shell carries its bake's record (time, peak)", t);
        if (c.bakeMs > 0) {
            const std::string what = "bake seconds for the " + std::to_string(c.triangles) + "-triangle shell";
            target("W11", c.bakeMs / 1000.0, "s", what.c_str());
            const std::string peak = "the bake's peak RSS for the " + std::to_string(c.triangles) + "-triangle shell";
            target("W11", double(c.peakRssKb) / 1024.0, "MB", peak.c_str());
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
// scale.cpu_walks — W13: FOUR O(N) CPU WALKS A FRAME, and ATOM-CPU-WALKS-1's bars.
// The stages: engine.gpuscene (the dirty scan, OgreGpuScene.cpp ensureGpuScene),
// engine.cards (the card candidates — since ATOM-CPU-WALKS-1 the change feed's
// columns, OgreScene::CardFeed; the LIGHT walk it used to hide is its own nested
// stage, engine.cards.lights, printed beside it), engine.atomwords (the split's word
// set — the change feed and the PBS change log, OgreAtomDraw.cpp) and
// engine.rayquery (the ray tier's frame — the instances written on the device by
// the instance job rq_tlas_write.comp, OgreRayQuery.cpp).
// THE GATE (ATOM-CPU-WALKS-1, fix round F2): the change-driven walks visit 0 slots on
// a still frame and at most the moved slot on a mover frame (the change feed's
// notifications, the split's words, the ray tier's feed). Their CPU ms — the brief's
// 0.5 ms still / 1.5 ms mover — are printed `target:` lines: an ms bar under sibling
// lanes' builds is a load flake, the visit count is the property. The DIRTY SCAN stays
// a printed target: its bound needs a record of WHICH
// nodes the document wrote (ENGINE V2's journal, V2-1) and none exists — the only
// signal is a process-wide write counter (nodegraph.cpp markMoved), so a mover
// frame still compares every item (the lane's STOP, stated in its report).
// ===========================================================================
static int cpuWalksMain()
{
    Env env;
    World w;
    // D1's DENSE WORLD: the bars were calibrated on it (V2-P0A).
    if (!bootWorld(env, w, "test-scale-cpu-walks-ogre.log", denseWorld())) return 1;
    pathStill(env, 30);
    iris::MeshNodePtr mover = w.items[w.items.size() / 3];
    mover->setMobility(iris::Mobility::Movable);   // see scale.tlas: a mover the renderer moves every frame
    frame(env, 30);
    const iris::Vec3 home = mover->getLocalPos();
    static const char *kWalks[5] = { "engine.gpuscene", "engine.cards", "engine.atomwords", "engine.rayquery",
                                     "engine.cards.lights" };
    static const char *kWhat[5] = { "the dirty scan", "the card candidates (the card cache's frame)",
                                    "the Atom words (the change feed)", "the ray tier's frame (the TLAS by compute)",
                                    "the card cache's light walk" };
    // THE GATE IS THE WORK, NOT THE CLOCK (ATOM-CPU-WALKS-1 fix round F2): the three
    // change-driven walks are gated on the SLOTS they visit — the change feed's
    // notifications (every consumer is told exactly these), the split's word set and
    // the ray tier's feed — 0 on a still frame and at most one per moved slot per
    // frame on a mover frame. Their milliseconds are printed as `target:` lines: a CPU
    // ms bar reads a sibling lane's -j3 build as much as this code (0.506 against 0.5
    // once, under load).
    struct Visits { unsigned long long feed = 0, words = 0, rays = 0; };
    auto visits = [&] {
        Visits v;
        v.feed = env.scene->gpuSceneStatus().feedNotifies;
        v.words = env.scene->atomDrawStatus().wordSlotVisits;
        v.rays = env.scene->rayQueryStatus().feedSlotVisits;
        return v;
    };
    size_t frames = 0;
    auto measure = [&](const char *label, bool move) {
        const auto recs = collect(env, [&] {
            for (int f = 0; f < 120; ++f) {
                if (move) mover->setLocalPos(home + iris::Vec3(0.02f * float(f % 2 ? 1 : -1), 0, 0));
                frame(env, 1);
            }
        });
        std::array<double, 5> med{};
        std::array<size_t, 5> seen{};
        for (int k = 0; k < 5; ++k) {
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
        frames = recs.size();
        std::printf("W13 %-10s frames %3zu | gpuscene %6.3f ms | cards %6.3f ms | atomwords %6.3f ms | rayquery %6.3f ms"
                    " | cards.lights %6.3f ms (medians; frames carrying each: %zu %zu %zu %zu %zu)\n",
                    label, recs.size(), med[0], med[1], med[2], med[3], med[4], seen[0], seen[1], seen[2], seen[3],
                    seen[4]);
        return med;
    };
    const Visits v0 = visits();
    const auto still = measure("still", false);
    const Visits v1 = visits();
    const auto moving = measure("one mover", true);
    const Visits v2 = visits();
    const unsigned long long moverFrames = frames;   // frames of the mover arm (one moved slot each)
    std::printf("W13 visits  still: feed %llu, words %llu, ray tier %llu | mover (%llu frames, 1 mover): feed %llu, "
                "words %llu, ray tier %llu\n",
                v1.feed - v0.feed, v1.words - v0.words, v1.rays - v0.rays, moverFrames, v2.feed - v1.feed,
                v2.words - v1.words, v2.rays - v1.rays);
    REQUIRE(moving[0] >= 0 && moving[1] >= 0 && moving[2] >= 0 && moving[3] >= 0,
            "every walk's stage was filed on the mover frames");
    REQUIRE(v1.feed == v0.feed && v1.words == v0.words && v1.rays == v0.rays,
            "W13 a STILL frame at 10k visits 0 slots (the change feed %llu, the split's words %llu, the ray tier %llu)",
            v1.feed - v0.feed, v1.words - v0.words, v1.rays - v0.rays);
    REQUIRE(v2.feed > v1.feed && v2.feed - v1.feed <= moverFrames && v2.words - v1.words <= moverFrames &&
                v2.rays - v1.rays <= moverFrames,
            "W13 a MOVER frame visits at most the moved slot (feed %llu, words %llu, ray tier %llu over %llu frames)",
            v2.feed - v1.feed, v2.words - v1.words, v2.rays - v1.rays, moverFrames);
    for (int k = 0; k < 5; ++k) {
        const std::string what = std::string(kWhat[k]) + " (" + kWalks[k] + ") on a mover frame at 10k; still " +
                                 std::to_string(still[size_t(k)]) + " ms" +
                                 ((k >= 1 && k <= 3) ? " (bars 0.5 still / 1.5 mover)" : "");
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

/// THE PAIRED FRAME ARMS (ATOM-CPU-WALKS-1, a TOOL, not a suite): a still frame and
/// a frame with one thing moving, on the 10k world (`--frame-arms-world`: its Movable
/// mover jittered, scale.cpu_walks' shape) or on the lattice (`--frame-arms-lattice`:
/// one still cube dragged — the editor's drag). Each arm prints its PATH line and the
/// four walks' stages; a base binary built from the same source is the other arm
/// (one process per arm, alternated, under scripts/gpu-exclusive.sh).
static int frameArmsMain(bool lattice)
{
    Env env;
    World w;
    iris::SceneNodePtr moving;
    if (lattice) {
        if (!boot(env, "test-scale-frame-arms-lattice-ogre.log")) return 1;
        buildLattice(env);
        armMonitor(env);
        moving = env.doc->getRootNode()->children().at(4210);
    } else {
        if (!bootWorld(env, w, "test-scale-frame-arms-world-ogre.log")) return 1;
        pathStill(env, 30);
        iris::MeshNodePtr m = w.items[w.items.size() / 3];
        m->setMobility(iris::Mobility::Movable);
        moving = m;
    }
    frame(env, 30);
    const iris::Vec3 home = moving->getLocalPos();
    static const char *kWalks[5] = { "engine.gpuscene", "engine.cards", "engine.cards.lights", "engine.atomwords",
                                     "engine.rayquery" };
    auto arm = [&](const char *label, bool move) {
        const auto recs = collect(env, [&] {
            for (int f = 0; f < 120; ++f) {
                if (move) moving->setLocalPos(home + iris::Vec3(lattice ? 0.05f * float(f % 40) : 0.02f * float(f % 2 ? 1 : -1), 0, 0));
                frame(env, 1);
            }
        });
        printPath(label, recs);
        std::printf("WALKS %-9s", label);
        for (const char *wk : kWalks) {
            std::vector<double> v;
            for (const FrameRecord &r : recs) v.push_back(stageMs(r, wk));
            std::printf(" | %s %.3f", wk, stats(v).median);
        }
        std::printf("\n");
        std::fflush(stdout);
    };
    arm(lattice ? "lat-still" : "wld-still", false);
    arm(lattice ? "lat-mover" : "wld-mover", true);
    moving->setLocalPos(home);
    shutdown(env);
    return 0;
}

// ===========================================================================
// scale.gpu_coverage — EVERY BUSY GPU MILLISECOND HAS A ROW (lane TEST-1, the perf audit's F5).
// The frame monitor's records carry the frame's OWN GPU span (FrameRecord::frameGpuMs), and the
// monitor times every stretch in which none of its rows is open: a stretch that crossed a CPU
// submission is the GPU WAITING (gpuIdleMs — a frame-schedule reading, printed, never a coverage
// hole: the irradiance field's raster submits every 8 probes and the CPU records on), one inside a
// submission is GPU work no row names (unattributedGpuMs). THE BAR is ATTRIBUTION over the BUSY
// span: the median frame's unattributed share of (frameGpuMs - gpuIdleMs) stays under kCoverageBar,
// so sibling CPU load (longer idle) cannot red it. A pass or dispatch outside every row — the card
// relight and the sky bakes were, before this lane — shows here. THE FIXTURE: the lattice (8,000
// cubes, a shadowed point lamp) at EPIC, 1080p, still and with the camera moving; 120 frames warm,
// 240 measured per arm.
// ===========================================================================
static int gpuCoverageMain()
{
    static constexpr double kCoverageBar = 0.05;
    Env env;
    if (!boot(env, "test-scale-gpu-coverage-ogre.log")) return 1;
    buildLattice(env);
    worldmodes::setMode(env.doc, worldmodes::Mode::Epic);
    worldmodes::setPhoton(env.doc, true, worldmodes::PhotonTier::Epic);
    for (int f = 0; f < 900; ++f) { frame(env, 1); if (f > 30 && env.scene->giStatus().giAtRest) break; }
    armMonitor(env);
    frame(env, 120);
    for (int arm = 0; arm < 2; ++arm) {
        const bool pan = arm == 1;
        const char *what = pan ? "pan" : "still";
        const auto recs = collect(env, [&] {
            for (int f = 0; f < 240; ++f) {
                if (pan) {
                    const float a = 0.004f * float(f);
                    setCamera(env, iris::Vec3(30.0f * std::sin(a), 12.0f, 30.0f * std::cos(a)), iris::Vec3(0, 8, 0));
                }
                frame(env, 1);
            }
        });
        std::vector<double> span, busy, idle, rest, share, mismatch;
        unsigned unmeasured = 0;
        const FrameRecord *worst = nullptr, *worstIdle = nullptr;
        for (const FrameRecord &r : recs) {
            if (r.frameGpuMs <= 0.0f || r.unattributedGpuMs < 0.0f || r.gpuIdleMs < 0.0f) { ++unmeasured; continue; }
            const double b = double(r.frameGpuMs) - double(r.gpuIdleMs);
            span.push_back(r.frameGpuMs);
            idle.push_back(r.gpuIdleMs);
            busy.push_back(b);
            rest.push_back(r.unattributedGpuMs);
            share.push_back(b > 0.0 ? double(r.unattributedGpuMs) / b : 1.0);
            if (!worst || r.unattributedGpuMs > worst->unattributedGpuMs) worst = &r;
            if (!worstIdle || r.gpuIdleMs > worstIdle->gpuIdleMs) worstIdle = &r;
        }
        const Stats sp = stats(span), bu = stats(busy), id = stats(idle), un = stats(rest), sh = stats(share);
        std::printf("COVERAGE %-5s: %zu frames measured (%u not): span median %.3f ms | busy %.3f | IDLE (GPU waiting "
                    "for a submission) median %.3f ms p95 %.3f | unattributed median %.3f ms p95 %.3f | share of "
                    "busy median %.2f %% p95 %.2f %% (bar %.0f %%)\n",
                    what, span.size(), unmeasured, sp.median, bu.median, id.median, id.p95, un.median, un.p95,
                    100.0 * sh.median, 100.0 * sh.p95, 100.0 * kCoverageBar);
        const auto dump = [&](const char *label, const FrameRecord *r) {
            if (!r) return;
            std::printf("COVERAGE %-5s %s frame %llu: span %.3f idle %.3f unattributed %.3f ms, CPU %.3f ms; rows",
                        what, label, r->frame, r->frameGpuMs, r->gpuIdleMs, r->unattributedGpuMs, r->totalMs);
            for (const CacheWork &w : r->cacheWork)
                if (w.gpuMs > 0.1f) std::printf(" | %s %.2f", w.detail.c_str(), w.gpuMs);
            std::printf("; stages");
            for (const FrameStage &st : r->stages)
                if (st.ms > 0.5f) std::printf(" | %s %.2f", st.name.c_str(), st.ms);
            std::printf("\n");
        };
        dump("worst-unattributed", worst);
        dump("worst-idle", worstIdle);
        target("F5", id.median, "ms", (std::string(what) + ": the GPU's median wait for submissions inside a frame "
                                       "(gpuIdleMs; a schedule reading for the speed work, never a coverage hole)").c_str());
        REQUIRE(span.size() >= 200u, "%s: the frame's own pair and its gaps answered on %zu of %zu frames", what,
                span.size(), recs.size());
        REQUIRE(!share.empty() && sh.median < kCoverageBar,
                "%s: the median frame's unattributed share of its BUSY GPU time %.2f %% < %.0f %%", what,
                100.0 * sh.median, 100.0 * kCoverageBar);
    }
    shutdown(env);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// scale.shadow_cut — THE CASTER PASS FROM THE CUT (ATOM-SHADOWS-1): the shadow node's CPU
// and GPU time with the Atom casters drawn by the light's cluster cut (one indirect draw a
// map) against the per-instance stock PBS caster path (the split's door shut: every item
// back on PBS, its caster drawn per instance). PAIRED ARMS in one process, alternated
// (cut, PBS, cut, PBS), each 120 still frames after a 60-frame settle, under the GPU lock.
// THE BARS (the lead's, 2026-09-28):
//   * CPU, D1's world: <= 1/3 of the per-instance path's — the lane's point (the per-instance
//     caster draws gone). The small world (200 objects): <= 2x — four dispatches a map beat
//     200 draws on the GPU's side of the ledger, not the CPU's, so a small scene pays a
//     little more CPU by construction (measured 1.33x).
//   * GPU: ABSOLUTE, per map re-rendered — never a ratio against the old route. The old route
//     drew each caster at the VIEW's level (the pin's shadow passes never compute LOD), i.e.
//     the wrong geometry for the light, so its GPU time is no reference for a correct map
//     (shadow.atom_parity is the physics: within a texel of the dense caster, where the old
//     route was 10.7 texels off). Measured over three runs on D1's world: 0.065 / 0.089 / 0.102
//     ms a map (3 PSSM maps), 0.044 on the small one; the bar is 1.5x the WORST seen, rounded:
//     0.15 ms a map, on both worlds (1.27x the worst was a flake candidate under contention).
// The caster pass = every pass of the view's shadow node (PassBucket::ShadowView), its scene
// passes and the caster cut's passes together.
static int shadowCutMain()
{
    struct Arm { std::vector<double> cpu, gpu; unsigned passes = 0; };
    auto measure = [&](Env &env, Arm &arm) {
        const auto recs = collect(env, [&] { frame(env, 120); });
        for (const FrameRecord &r : recs) {
            double cpu = 0, gpu = 0;
            bool gpuOk = true;
            unsigned n = 0;
            for (const FramePass &p : r.passes) {
                if (p.bucket != PassBucket::ShadowView || p.orphaned) continue;
                ++n;
                // A scene pass's cpuMs INCLUDES its nested shadow-node update only for the
                // VIEW's passes; the node's own passes carry their own time.
                cpu += p.cpuMs;
                if (p.gpuMs < 0) gpuOk = false; else gpu += p.gpuMs;
            }
            if (!n) continue;
            arm.passes = std::max(arm.passes, n);
            arm.cpu.push_back(cpu);
            if (gpuOk) arm.gpu.push_back(gpu);
        }
    };
    constexpr double kGpuMsPerMapBar = 0.15;
    auto runWorld = [&](const char *label, WorldSpec spec, double cpuBar, double gpuBarPerMap) {
        Env env;
        World w;
        const std::string log = std::string("test-scale-shadow-cut-") + label + "-ogre.log";
        if (!bootWorld(env, w, log.c_str(), spec)) { ++failures; return; }
        pathStill(env, 60);
        Arm cut, pbs;
        for (int round = 0; round < 2; ++round) {
            env.scene->setAtomDrawEnabled(true);
            frame(env, 60);
            measure(env, cut);
            env.scene->setAtomDrawEnabled(false);
            frame(env, 60);
            measure(env, pbs);
        }
        env.scene->setAtomDrawEnabled(true);
        frame(env, 30);
        const AtomDrawStatus st = env.scene->atomDrawStatus();
        const Stats cc = stats(cut.cpu), pc = stats(pbs.cpu), cg = stats(cut.gpu), pg = stats(pbs.gpu);
        std::printf("SHADOW %-6s atom items %u | caster maps a frame %u, caster triangles %llu, instances %u | "
                    "shadow-node passes cut %u / pbs %u | CPU ms med cut %.3f pbs %.3f (ratio %.3f) | "
                    "GPU ms med cut %.3f pbs %.3f (ratio %.3f) | frames %zu / %zu\n",
                    label, st.atomItems, st.casterMaps, st.casterTriangles, st.casterInstances, cut.passes,
                    pbs.passes, cc.median, pc.median, pc.median > 0 ? cc.median / pc.median : -1.0, cg.median,
                    pg.median, pg.median > 0 ? cg.median / pg.median : -1.0, cc.n, pc.n);
        const double perMap = st.casterMaps ? cg.median / double(st.casterMaps) : -1.0;
        std::printf("target: scale.shadow_cut %s caster CPU ratio %.3f (bar %.2f), GPU %.3f ms a map (bar %.2f; the "
                    "old route's %.3f ms is the view's level, no reference)\n",
                    label, pc.median > 0 ? cc.median / pc.median : -1.0, cpuBar, perMap, gpuBarPerMap,
                    pg.median / double(std::max(1u, st.casterMaps)));
        REQUIRE(st.on && st.atomItems > 0 && st.casterValid && st.casterMissing == 0u && st.casterMaps > 0u,
                "%s: the caster cut drew (%u maps, %u missing)", label, st.casterMaps, st.casterMissing);
        REQUIRE(cc.n >= 100 && pc.n >= 100, "%s: both arms measured (%zu, %zu frames)", label, cc.n, pc.n);
        REQUIRE(pc.median > 0 && cc.median <= cpuBar * pc.median,
                "%s: the caster pass's CPU from the cut <= %.2fx the per-instance path's (%.3f vs %.3f ms)", label,
                cpuBar, cc.median, pc.median);
        REQUIRE(cg.n > 0 && perMap >= 0.0 && perMap <= gpuBarPerMap,
                "%s: the caster pass's GPU <= %.2f ms a map re-rendered (%.3f ms: %.3f over %u maps)", label,
                gpuBarPerMap, perMap, cg.median, st.casterMaps);
        shutdown(env);
    };
    // BOTH WORLDS IN ONE PROCESS, the big one first: the second boot after a
    // shutdown is itself the regression test of MONITOR-RETIRE-1 F2 (the frame
    // monitor used to outlive its engine and crash the next boot's first frame
    // in FrameMonitor::beginFrame).
    runWorld("world", denseWorld(), 1.0 / 3.0, kGpuMsPerMapBar);   // calibrated on D1's dense world (V2-P0A)
    WorldSpec small;
    small.instances = 200;
    small.lights = 4;
    small.spacing = 3.0f;
    small.name = "the small world";
    small.farClip = 500.0f;
    runWorld("small", small, 2.0, kGpuMsPerMapBar);
    return failures ? 1 : 0;
}

static int latticeOwedMain()
{
    Env env;
    if (!boot(env, "test-scale-lattice-ogre.log")) return 1;
    buildLattice(env);
    armMonitor(env);
    // S3-DRAW'S COST ROW, at High and at Epic (the tier whose chain carries the SSR
    // prepass): the decode's passes a frame, their GPU ms, and the classification's
    // arithmetic — ms / (buckets x passes), and per Mpx.
    for (int tierArm = 0; tierArm < 2; ++tierArm) {
        const worldmodes::PhotonTier t = tierArm ? worldmodes::PhotonTier::Epic : worldmodes::PhotonTier::High;
        worldmodes::setMode(env.doc, worldmodes::Mode(int(t)));
        worldmodes::setPhoton(env.doc, true, t);
        for (int f = 0; f < 900; ++f) { frame(env, 1); if (f > 30 && env.scene->giStatus().giAtRest) break; }
        const AtomDrawStatus st = env.scene->atomDrawStatus();
        std::vector<double> dec, id, pre;
        unsigned decodePasses = 0;
        const auto recs = collect(env, [&] { frame(env, 90); });
        for (const FrameRecord &r : recs) {
            // The PREPASS SIDE too, when the tier carries one: its decode pass + the prepass.
            if (const FramePass *p = passNamed(r, "Jahshaka SSR prepass")) {
                const FramePass *dp = passNamed(r, "Jahshaka atom decode prepass");
                if (p->gpuMs >= 0) pre.push_back(p->gpuMs + (dp && dp->gpuMs >= 0 ? dp->gpuMs : 0.0));
            }
            unsigned passes = 0;
            double ms = 0;
            // The opaque side (IdRead): the screen decode pass + the opaque pass.
            for (const FramePass &p : r.passes)
                if (p.pass == "Jahshaka opaque" && p.gpuMs >= 0) { ms += p.gpuMs; ++passes; }
            if (const FramePass *p = passNamed(r, "Jahshaka atom decode"))
                if (passes && p->gpuMs >= 0) ms += p->gpuMs;
            if (passes) { dec.push_back(ms); decodePasses = std::max(decodePasses, passes); }
            if (const FramePass *p = passNamed(r, "Jahshaka atom id")) if (p->gpuMs >= 0) id.push_back(p->gpuMs);
        }
        const double mpx = 1920.0 * 1080.0 / 1e6, d = stats(dec).median;
        std::printf("OWED S3-DRAW lattice at %s: buckets %u, decode passes a frame %u, decode GPU ms med %.3f (id %.3f) -> "
                    "%.4f ms per bucket-pass, %.4f ms per bucket-pass-Mpx (the prologue's cost per pixel x buckets x "
                    "passes)\n",
                    tierArm ? "Epic" : "High", st.buckets, decodePasses, d, stats(id).median,
                    d / std::max(1u, st.buckets * decodePasses), d / std::max(1u, st.buckets * decodePasses) / mpx);
        if (!pre.empty())
            std::printf("   ... the prepass side (its decode pass + the prepass) GPU ms med %.3f\n", stats(pre).median);
    }
    worldmodes::setMode(env.doc, worldmodes::Mode::High);
    worldmodes::setPhoton(env.doc, true, worldmodes::PhotonTier::High);
    // THE DRAG: one cube moved every frame for 60 frames — a still node moved on
    // consecutive ticks IS the engine's drag (OgreGi.cpp:4053, MOVER-1's promotion) —
    // and the scene-graph updates the GI side adds per frame (the gi.sceneGraph monitor
    // stages, one per call). Two arms: the mover channel ON (the default, per project)
    // and OFF (giDragMoverChannel 0, the older behaviour a project may still choose).
    auto doc = env.doc;
    iris::SceneNodePtr cubeNode = doc->getRootNode()->children().at(4210);
    for (int arm = 0; arm < 2; ++arm) {
        doc->giDragMoverChannel = arm == 0 ? 1 : 0;
        for (int f = 0; f < 900; ++f) { frame(env, 1); if (f > 30 && env.scene->giStatus().giAtRest) break; }
        const iris::Vec3 p0 = cubeNode->getLocalPos();
        int dragMovers = 0;
        const auto drag = collect(env, [&] {
            for (int f = 0; f < 60; ++f) {
                cubeNode->setLocalPos(p0 + iris::Vec3(0.05f * float(f), 0, 0));
                frame(env, 1);
                dragMovers = std::max(dragMovers, env.scene->giStatus().dragMovers);
            }
        });
        std::vector<double> calls, ms;
        for (const FrameRecord &r : drag) {
            unsigned n = 0;
            double t = 0;
            for (const FrameStage &st2 : r.stages)
                if (st2.name == "gi.sceneGraph") { ++n; t += st2.ms; }
            calls.push_back(n);
            ms.push_back(t);
        }
        std::printf("OWED drag on the lattice, mover channel %s (dragMovers seen %d): extra updateSceneGraph calls "
                    "from GI per frame med %.1f max %.0f, their ms med %.3f max %.3f (over %zu frames)\n",
                    arm == 0 ? "ON (default)" : "OFF", dragMovers, stats(calls).median, stats(calls).max,
                    stats(ms).median, stats(ms).max, drag.size());
        cubeNode->setLocalPos(p0);
    }
    shutdown(env);
    return 0;
}


// ===========================================================================
// scale.occlusion_cost — ATOM-OCCLUSION-1's COST TABLE: the door open / shut in ONE
// process, alternating, twice (paired arms), on the WORLD (its still pose and the 8 walk
// poses) and the LATTICE; per arm the frame's GPU ms (the monitor's passes summed) and the
// occlusion's own passes — the first id pass, the pyramid's build (every "Jahshaka atom HZB"
// pass), the late id pass — and the triangles each id pass drew. `W H` (optional) boots
// the view at another size: the pyramid's cost at the Quest Pro's eye (1800 x 1920).
// ===========================================================================
struct OcclArm {
    std::vector<double> frame, id, late, hzb, idTris, lateTris;
};
static void occlCollect(Env &env, OcclArm &arm, const std::function<void()> &body)
{
    for (const FrameRecord &r : collect(env, body)) {
        if (r.gpuMs >= 0) arm.frame.push_back(r.gpuMs);
        double hz = 0.0;
        bool anyHz = false;
        for (const FramePass &p : r.passes) {
            if (p.gpuMs < 0) continue;
            if (p.pass == "Jahshaka atom id") { arm.id.push_back(p.gpuMs); arm.idTris.push_back(double(p.triangles)); }
            else if (p.pass == "Jahshaka atom id late") { arm.late.push_back(p.gpuMs); arm.lateTris.push_back(double(p.triangles)); }
            else if (p.pass.rfind("Jahshaka atom HZB", 0) == 0) { hz += p.gpuMs; anyHz = true; }
        }
        if (anyHz) arm.hzb.push_back(hz);
    }
}
static void occlPrint(const char *label, const char *arm, int rep, const OcclArm &a)
{
    std::printf("COST %-8s %-3s #%d: frame GPU med %.3f ms (p95 %.3f, %zu frames) | id %.4f ms (%.0f tris) | pyramid %.4f ms "
                "| late %.4f ms (%.0f tris)\n",
                label, arm, rep, stats(a.frame).median, stats(a.frame).p95, a.frame.size(), stats(a.id).median,
                stats(a.idTris).median, a.hzb.empty() ? 0.0 : stats(a.hzb).median, a.late.empty() ? 0.0 : stats(a.late).median,
                a.lateTris.empty() ? 0.0 : stats(a.lateTris).median);
    std::fflush(stdout);
}

static int occlusionCostMain(int w, int h)
{
    Env env;
    World world;
    if (!bootWorld(env, world, "test-scale-occlcost-ogre.log", WorldSpec())) return 1;
    (void)w; (void)h;
    REQUIRE(gpuTimed(env), "the frame monitor has GPU timing");
    for (int rep = 0; rep < 2; ++rep)
        for (int arm = 0; arm < 2; ++arm) {
            env.scene->setAtomOcclusionEnabled(arm == 0);
            frame(env, 30);
            OcclArm still, walk;
            occlCollect(env, still, [&] { pathStill(env, 120); });
            occlCollect(env, walk, [&] {
                for (int s = 0; s < 8; ++s) {
                    const float x = -90.0f + 25.0f * float(s);
                    setCamera(env, iris::Vec3(x, 1.7f, 0.0f), iris::Vec3(x + 10.0f, 1.7f, -2.0f));
                    frame(env, 20);
                }
            });
            occlPrint("world", arm == 0 ? "on" : "off", rep, still);
            occlPrint("walk", arm == 0 ? "on" : "off", rep, walk);
        }
    env.scene->setAtomOcclusionEnabled(true);
    // THE LATTICE (8,000 cubes, the S3-DRAW cost fixture) in the same process.
    buildLattice(env);
    armMonitor(env);
    for (int rep = 0; rep < 2; ++rep)
        for (int arm = 0; arm < 2; ++arm) {
            env.scene->setAtomOcclusionEnabled(arm == 0);
            frame(env, 30);
            OcclArm lat;
            occlCollect(env, lat, [&] { frame(env, 120); });
            occlPrint("lattice", arm == 0 ? "on" : "off", rep, lat);
        }
    shutdown(env);
    return failures ? 1 : 0;
}

/// THE PYRAMID AT ANOTHER SIZE (the Quest Pro's eye, 1800 x 1920): the lattice (cheap to
/// build) at `w` x `h`, the door open, the "Jahshaka atom HZB" passes' GPU ms.
static int occlusionPyramidMain(int w, int h)
{
    Env env;
    if (!boot(env, "test-scale-occlpyr-ogre.log", w, h)) return 1;
    buildLattice(env);
    armMonitor(env);
    REQUIRE(gpuTimed(env), "the frame monitor has GPU timing");
    frame(env, 30);
    OcclArm lat;
    occlCollect(env, lat, [&] { frame(env, 120); });
    char label[32];
    std::snprintf(label, sizeof(label), "%dx%d", w, h);
    occlPrint(label, "on", 0, lat);
    shutdown(env);
    return failures ? 1 : 0;
}

// ===========================================================================
// atom.coverage_trace — ATOM-BLACK-FRAMES-1: THE 10k WORLD NEVER RENDERS A FRAME WITHOUT ITS
// ATOM SURFACE. D1's world at ~200 buckets (atom.decode_exact's deterministic config), the
// decode's DISCRIMINATOR on (JAHSHAKA_ATOM_DISCRIMINATE, HlmsAtom::preparePassHash): every
// pixel a bucket draw reaches is painted — code 0 where every validity term holds, else the
// code of the FIRST term that failed (800.Atom_piece_ps.any, AtomDeclDecode) — and every
// frame read back: the id image says which pixels the id pass COVERED, the picture what the
// decode made of them. The codes' colours are LEARNT first from the door's chart (value 2:
// the code of each pixel's column band) and the clear's colour from the uncovered pixels, so
// the post chain's curve is never guessed. A covered pixel is then OK (code 0), FAILED (a
// code, or the clear showing through: no bucket draw reached it), or OTHER (something drawn
// in front of the Atom surface: a lamp's icon, an item PBS draws while its textures land).
// The engine's event trace (JAHSHAKA_ATOM_TRACE, the Ogre log) stamps each table/buffer
// event with the frame AtomDrawStatus::frame names — what a failing row is read against.
// THE PHASES (brief §3.3): the 200 materials landing (the base's F1 window); then a 2,000-
// frame walk (6 m/s) with a 25 m teleport every 150 frames (held 20), a DAG mesh RELEASED
// mid-walk (its 625 items leave: the cluster tables rebuild and every later dag rebases)
// and re-added, a NEW mesh added (a new entry, a new DAG) and released, and the cut's
// budget forced SMALL twice (the main region overflows: the root cut from the reserve, the
// stats ring's 4-frame-late report, the budget's re-create).
// THE BAR: no frame FAILS more than 0.5 % of the pixels the id pass covered (the base's
// black frames failed 90-99 %; its own seams are ~100 px), and no walk step loses half the
// previous frame's coverage (a teleport frame and the first frames of a phase are exempt:
// their picture is another place).
// ===========================================================================
struct CodeChart {
    std::vector<std::array<unsigned char, 3>> colour;   // per code 0..16
    std::array<unsigned char, 3> clear{ { 0, 0, 0 } };
    size_t covered = 0;   ///< the chart frame's covered pixels (the coverage floor's reference)
};
static constexpr int kCodes = 17;
static bool learnChart(Env &env, CodeChart &chart)
{
    setenv("JAHSHAKA_ATOM_DISCRIMINATE", "2", 1);
    frame(env, 64);
    Image img;
    std::vector<uint32_t> ids;
    const bool read = env.view->readPixels(img) && img.width && readIdsAt(env, ids);
    setenv("JAHSHAKA_ATOM_DISCRIMINATE", "1", 1);
    if (!read) return false;
    const unsigned W = img.width, H = img.height;
    auto key = [](const unsigned char *px) { return uint32_t(px[0]) | uint32_t(px[1]) << 8 | uint32_t(px[2]) << 16; };
    auto rgb = [](uint32_t k) {
        return std::array<unsigned char, 3>{ { (unsigned char)(k & 0xFF), (unsigned char)((k >> 8) & 0xFF),
                                                (unsigned char)((k >> 16) & 0xFF) } };
    };
    bool ok = true;
    chart.colour.assign(kCodes, { { 0, 0, 0 } });
    std::vector<std::map<uint32_t, size_t>> seen(kCodes);
    std::map<uint32_t, size_t> clear;
    chart.covered = 0;
    for (size_t p = 0; p < size_t(W) * H; ++p) chart.covered += ids[p * 2] != 0xFFFFFFFFu;
    for (unsigned y = 0; y < H; y += 2)
        for (unsigned x = 0; x < W; ++x) {
            const size_t p = size_t(y) * W + x;
            if (ids[p * 2] == 0xFFFFFFFFu) ++clear[key(&img.rgba[p * 4])];
            else if (y >= H / 2) ++seen[int(x * 17u / W)][key(&img.rgba[p * 4])];
        }
    for (int c = 0; c < kCodes; ++c) {
        uint32_t best = 0;
        size_t bestN = 0, total = 0;
        for (auto &kv : seen[c]) {
            total += kv.second;
            if (kv.second > bestN) { bestN = kv.second; best = kv.first; }
        }
        chart.colour[c] = rgb(best);
        std::printf("coverage_trace: chart code %2d = (%3u,%3u,%3u)  %zu of %zu px\n", c, best & 0xFF, (best >> 8) & 0xFF,
                    (best >> 16) & 0xFF, bestN, total);
        if (!bestN || bestN * 2 < total) ok = false;
        for (int d = 0; d < c; ++d)
            if (chart.colour[d] == chart.colour[c]) ok = false;
    }
    uint32_t best = 0;
    size_t bestN = 0;
    for (auto &kv : clear)
        if (kv.second > bestN) { bestN = kv.second; best = kv.first; }
    chart.clear = rgb(best);
    std::printf("coverage_trace: the clear = (%3u,%3u,%3u) over %zu uncovered px\n", best & 0xFF, (best >> 8) & 0xFF,
                (best >> 16) & 0xFF, bestN);
    for (int c = 0; c < kCodes; ++c)
        if (chart.colour[c] == chart.clear) ok = false;
    return ok;
}

struct FrameCodes {
    size_t covered = 0, ok = 0, clear = 0, other = 0, failed = 0;
    size_t code[kCodes] = {};
};
static void classify(const CodeChart &chart, const Image &img, const std::vector<uint32_t> &ids, FrameCodes &fc)
{
    fc = FrameCodes();
    const size_t n = std::min(ids.size() / 2, img.rgba.size() / 4);
    const auto &okc = chart.colour[0];
    for (size_t p = 0; p < n; ++p) {
        if (ids[p * 2] == 0xFFFFFFFFu) continue;
        ++fc.covered;
        const unsigned char *px = &img.rgba[p * 4];
        if (px[0] == okc[0] && px[1] == okc[1] && px[2] == okc[2]) { ++fc.ok; continue; }
        if (px[0] == chart.clear[0] && px[1] == chart.clear[1] && px[2] == chart.clear[2]) { ++fc.clear; continue; }
        int best = -1, bestD = 1 << 30;
        for (int c = 0; c < kCodes; ++c) {
            const int dr = int(px[0]) - chart.colour[c][0], dg = int(px[1]) - chart.colour[c][1],
                      db = int(px[2]) - chart.colour[c][2];
            const int d = dr * dr + dg * dg + db * db;
            if (d < bestD) { bestD = d; best = c; }
        }
        if (best >= 0 && bestD <= 3 * 12 * 12) {
            if (best == 0) ++fc.ok;
            else ++fc.code[best];
        } else {
            ++fc.other;
        }
    }
    fc.failed = fc.clear;
    for (int c = 1; c < kCodes; ++c) fc.failed += fc.code[c];
}

static int coverageTraceMain()
{
    setenv("JAHSHAKA_NO_DITHER", "1", 1);
    setenv("JAHSHAKA_NO_RAY_QUERY", "1", 1);
    setenv("JAHSHAKA_ATOM_TRACE", "1", 1);
    setenv("JAHSHAKA_ATOM_DISCRIMINATE", "1", 1);
    auto knob = [](const char *name, int dflt) { return std::getenv(name) ? std::atoi(std::getenv(name)) : dflt; };
    const int walkFrames = knob("JAH_TRACE_FRAMES", 2000);
    const int landingFrames = knob("JAH_TRACE_LANDING", 600);
    const int budgetAt = knob("JAH_TRACE_BUDGET_AT", 1100);
    // SMALL, NOT TINY (measured): at walk 1100 the world asks ~582,000 indices, the main
    // region holds 437,500, and the 62,500-index reserve holds the ~30 overflowing
    // survivors' root cuts (232 indices a world instance on average, 2.33 M over all
    // 10,127) — they draw COARSE until the stats ring's report, four frames late, grows
    // the budget to 1 M. A budget under the reserve's floor (JAH_TRACE_BUDGET=60000) is
    // the "fits neither" arm: up to 1,845 objects MISSING, counted apart, for the five
    // frames the report takes — the design's stated limit, reported by the lane.
    const unsigned smallBudget = unsigned(knob("JAH_TRACE_BUDGET", 500000));
    Env env;
    World w;
    // THIS SUITE KEEPS D1's DENSE 600 m WORLD: its budget arms above are measured against
    // that world's index demand (a sparse 2 km world asks too little for the forced small
    // budget to overflow, V2-P0A's gate), and what it guards is the cut's coverage under
    // budget pressure, not the 2 km reach.
    WorldSpec spec = denseWorld();
    spec.materials = 1;
    if (!bootWorld(env, w, "test-atom-coverage-trace-ogre.log", spec)) return 1;
    env.doc->exposureMode = iris::ExposureMode::Manual;
    worldmodes::setMode(env.doc, worldmodes::Mode::High);
    worldmodes::setPhoton(env.doc, false, worldmodes::PhotonTier::High);
    for (const char *row : { "ssr", "bloom", "smaa", "ssao" }) worldmodes::setRowValue(env.doc, QString::fromLatin1(row), 0);
    env.view->setBackground(Colour(1.0f, 0.0f, 1.0f, 1.0f));   // the clear: magenta, never a code
    pathStill(env, 30);
    CodeChart chart;
    const bool charted = learnChart(env, chart);
    REQUIRE(charted, "the discriminator's code chart and the clear were learnt (18 distinct colours)");
    if (!charted) { shutdown(env); return 1; }

    size_t frames = 0, badFrames = 0, dropFrames = 0, worstFailedPx = 0, worstOther = 0;
    double worstFailedShare = 0.0;
    size_t prevCovered = 0;
    int exempt = 1;   // steps whose picture is another place (a phase's first frame, a teleport)
    unsigned maxCoarse = 0, maxMissing = 0;
    auto sample = [&](const char *phase, int step) {
        frame(env, 1);
        Image img;
        std::vector<uint32_t> ids;
        if (!env.view->readPixels(img) || !img.width || !readIdsAt(env, ids)) {
            std::printf("coverage_trace: %s %d: READ FAILED\n", phase, step);
            ++badFrames;
            return;
        }
        FrameCodes fc;
        classify(chart, img, ids, fc);
        const AtomDrawStatus st = env.scene->atomDrawStatus();
        const double failedShare = fc.covered ? double(fc.failed) / double(fc.covered) : 0.0;
        const bool isBad = failedShare > 0.005;
        // THE FLOOR (the Fable read's W1): a frame whose id image went EMPTY fails nothing
        // and, exempt, drops nothing — so a non-exempt frame must cover at least HALF the
        // chart frame's pixels. Measured: the lowest non-exempt frame of the fixed traces
        // covers 1,029,120 px against the chart's ~1.1 M (the ground alone is ~0.8 M), i.e.
        // 0.93; half leaves the walk's own change a 2x margin.
        const bool isDrop = exempt <= 0 && ((prevCovered > 20000u && fc.covered * 2u < prevCovered) ||
                                            fc.covered * 2u < chart.covered);
        ++frames;
        badFrames += isBad;
        dropFrames += isDrop;
        worstFailedShare = std::max(worstFailedShare, failedShare);
        worstFailedPx = std::max(worstFailedPx, fc.failed);
        worstOther = std::max(worstOther, fc.other);
        maxCoarse = std::max(maxCoarse, st.cutOverflow);
        maxMissing = std::max(maxMissing, st.cutMissing);
        std::string codes;
        for (int c = 1; c < kCodes; ++c)
            if (fc.code[c]) codes += " c" + std::to_string(c) + "=" + std::to_string(fc.code[c]);
        std::printf("trace %s %4d frame %llu covered %7zu ok %7zu failed %zu (clear %zu%s) other %zu | cut budget %u "
                    "coarse %u missing %u | atom %u pbs %u pending %u draws %u%s%s\n",
                    phase, step, st.frame, fc.covered, fc.ok, fc.failed, fc.clear, codes.c_str(), fc.other,
                    st.cutIndexBudget, st.cutOverflow, st.cutMissing, st.atomItems, st.pbsItems, st.pending,
                    st.screenDraws, isBad ? " BAD" : "", isDrop ? " DROP" : "");
        std::fflush(stdout);
        // EVIDENCE (JAH_SCALE_SHOT): the first bad frames, the picture and the id image.
        static int shots = 0;
        if (isBad && !qgetenv("JAH_SCALE_SHOT").isEmpty() && shots++ < 3) {
            const QString dir = QString::fromLocal8Bit(qgetenv("JAH_SCALE_SHOT"));
            const QString tag = QStringLiteral("coverage-%1-%2").arg(QString::fromLatin1(phase)).arg(step);
            QImage(img.rgba.data(), int(img.width), int(img.height), QImage::Format_RGBA8888)
                .save(dir + tag + "-picture.png");
            QImage v(int(img.width), int(img.height), QImage::Format_RGB888);
            for (unsigned y = 0; y < img.height; ++y)
                for (unsigned x = 0; x < img.width; ++x) {
                    const size_t p = size_t(y) * img.width + x;
                    uint32_t h = ids[p * 2] == 0xFFFFFFFFu
                                     ? 0u
                                     : ((ids[p * 2] & 0xFFFFFFu) * 2654435761u ^ (ids[p * 2 + 1] >> 8) * 40503u);
                    h ^= h >> 13;
                    v.setPixel(int(x), int(y), h ? ((h & 0xFFFFFFu) | 0x202020u) : 0u);
                }
            v.save(dir + tag + "-ids.png");
        }
        prevCovered = fc.covered;
        --exempt;
    };

    // ---- 1. THE MATERIALS LANDING (the base's F1 window: ~300-460 frames after) --------
    applyMaterials(w, 200, 200);
    exempt = 2;
    for (int f = 0; f < landingFrames; ++f) sample("landing", f);

    // ---- 1b. A NODE TAKEN OUT OF THE DOCUMENT (the Fable read's W3): removeChild alone,
    // no setMesh(null) — does the GPU scene's mesh entry go? Every item of one DAG mesh
    // leaves the document for 20 frames; the engine's trace names the release (or not).
    bool removeChildReleased = false;
    {
        const iris::MeshPtr probeMesh = w.items.size() > 4 ? w.items[4]->getMesh() : iris::MeshPtr();
        std::vector<iris::MeshNodePtr> gone;
        auto releasesInLog = [] {
            QFile f(QStringLiteral("test-atom-coverage-trace-ogre.log"));
            if (!f.open(QIODevice::ReadOnly)) return -1;
            return int(f.readAll().count("mesh released:"));
        };
        const int before = releasesInLog();
        for (auto &it : w.items)
            if (it->getMesh() == probeMesh) { gone.push_back(it); env.doc->getRootNode()->removeChild(it); }
        for (int f = 0; f < 20; ++f) sample("removechild", f);
        const int after = releasesInLog();
        removeChildReleased = before >= 0 && after > before;
        std::printf("coverage_trace: removeChild of %zu items of one mesh: the GPU scene's entry %s (\"mesh released\" "
                    "lines %d -> %d)\n", gone.size(), removeChildReleased ? "RELEASED" : "KEPT", before, after);
        for (auto &it : gone) env.doc->getRootNode()->addChild(it);
        exempt = 2;
        for (int f = 0; f < 10; ++f) sample("readd", f);
    }

    // ---- 2. THE WALK, with teleports, mesh releases/adds and a forced small budget ------
    iris::MeshPtr extra = bakedMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/models/head.obj"), "head");
    std::vector<iris::MeshNodePtr> extraItems, released;
    const iris::MeshPtr releaseMesh = w.items.size() > 3 ? w.items[3]->getMesh() : iris::MeshPtr();
    const float x0 = -120.0f, speed = 0.1f;   // metres a frame (6 m/s: 200 m over the walk)
    unsigned atomBefore = 0, atomReleased = 0, atomExtra = 0;
    exempt = 2;
    for (int f = 0; f < walkFrames; ++f) {
        float x = x0 + float(f) * speed;
        // A 25 m TELEPORT every 150 frames, held for 20 frames (then the walk resumes).
        const int inCycle = f % 150;
        const bool teleported = inCycle >= 100 && inCycle < 120;
        if (teleported) x += 25.0f;
        if (inCycle == 100 || inCycle == 120) exempt = 2;
        setCamera(env, iris::Vec3(x, 1.7f, 0.0f), iris::Vec3(x + 10.0f, 1.7f, -2.0f));
        if (f == 300 && !releaseMesh.isNull()) {
            atomBefore = env.scene->atomDrawStatus().atomItems;
            // THE MESH LEAVES: every item of it drops the mesh, the last reference
            // releases the GPU scene's entry and its DAG.
            for (auto &it : w.items)
                if (it->getMesh() == releaseMesh) { released.push_back(it); it->setMesh(iris::MeshPtr()); }
            std::printf("coverage_trace: walk %d: RELEASED %zu items of one mesh\n", f, released.size());
        }
        if (f == 310) atomReleased = env.scene->atomDrawStatus().atomItems;
        if (f == 390) {
            for (auto &it : released) it->setMesh(releaseMesh);
            std::printf("coverage_trace: walk %d: RE-ADDED %zu items\n", f, released.size());
            released.clear();
        }
        if (f == 700 && !extra.isNull()) {
            for (int k = 0; k < 40; ++k) {
                auto n = iris::MeshNode::create();
                n->setName(QStringLiteral("extra%1").arg(k));
                n->setMesh(extra);
                n->setMaterial(w.items[size_t(k)]->getMaterial());
                n->setLocalPos(iris::Vec3(x + 6.0f + float(k % 8) * 1.5f, 1.0f, -3.0f - float(k / 8) * 1.5f));
                env.doc->getRootNode()->addChild(n);
                extraItems.push_back(n);
            }
            std::printf("coverage_trace: walk %d: ADDED %zu items of a new mesh\n", f, extraItems.size());
        }
        if (f == 720) atomExtra = env.scene->atomDrawStatus().atomItems;
        if (f == 900) {
            for (auto &it : extraItems) {
                it->setMesh(iris::MeshPtr());
                env.doc->getRootNode()->removeChild(it);
            }
            std::printf("coverage_trace: walk %d: RELEASED the new mesh's %zu items\n", f, extraItems.size());
            extraItems.clear();
        }
        if (f == budgetAt || f == budgetAt + 500) {
            env.scene->setAtomCutBudgetForTest(smallBudget);
            std::printf("coverage_trace: walk %d: the cut budget forced to %u indices\n", f, smallBudget);
        }
        sample(teleported ? "jump" : "walk", f);
    }
    std::printf("coverage_trace: %zu frames | %zu BAD (failed > 0.5 %% of the covered px) | %zu DROP | worst failed "
                "share %.4f (%zu px) | most drawn in front (other) %zu px | coarse max %u, missing max %u | atom items "
                "%u -> %u released -> %u with the new mesh\n",
                frames, badFrames, dropFrames, worstFailedShare, worstFailedPx, worstOther, maxCoarse, maxMissing,
                atomBefore, atomReleased, atomExtra);
    REQUIRE(badFrames == 0u, "no frame fails more than 0.5 %% of the pixels the id pass covered (%zu of %zu frames)",
            badFrames, frames);
    REQUIRE(dropFrames == 0u, "no walk step loses half the previous frame's coverage (%zu)", dropFrames);
    if (walkFrames > 900) {
        REQUIRE(atomReleased + 600u <= atomBefore && atomExtra >= atomBefore + 30u,
                "the mesh release and the new mesh reached the split (atom items %u -> %u, then %u)", atomBefore,
                atomReleased, atomExtra);
        REQUIRE(maxCoarse > 0u, "the forced small budget overflowed the main region (%u drawn coarse)", maxCoarse);
    }
    // REMOVECHILD-LEAK-1 (measured by ATOM-BLACK-FRAMES-1's W3 arm, fixed in
    // OgreScene::releaseNode): removeChild takes the items out of the split AND
    // gives back their mesh-table references — the last one releases the GPU
    // scene's entry and its DAG, as the walk's setMesh(null) arm always did.
    REQUIRE(removeChildReleased, "removeChild alone releases the GPU scene's mesh entry (\"mesh released\" "
            "in the trace after every item of the mesh left the document)");
    unsetenv("JAHSHAKA_ATOM_DISCRIMINATE");
    unsetenv("JAHSHAKA_ATOM_TRACE");
    shutdown(env);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "--world") return worldMain();
    if (mode == "--voxel-scroll") return voxelScrollMain();
    if (mode == "--lights") return lightsMain();
    if (mode == "--lights-cost") return lightsCostMain();
    if (mode == "--cluster-cut") return clusterCutMain();
    if (mode == "--levels") return levelsMain();
    if (mode == "--cut-cost") return cutCostMain();
    if (mode == "--residency") return residencyMain();
    if (mode == "--decode") return decodeMain();
    if (mode == "--occlusion") return occlusionMain();
    if (mode == "--occlusion-cost") return occlusionCostMain(0, 0);
    if (mode == "--occlusion-pyramid")
        return occlusionPyramidMain(argc > 3 ? std::atoi(argv[2]) : 1800, argc > 3 ? std::atoi(argv[3]) : 1920);
    if (mode == "--tlas") return tlasMain();
    if (mode == "--atlas") return atlasMain();
    if (mode == "--far-field") return farFieldMain();
    if (mode == "--bake") return bakeMain();
    if (mode == "--hit-list") return hitListMain();
    if (mode == "--cpu-walks") return cpuWalksMain();
    if (mode == "--shadow-cut") return shadowCutMain();
    if (mode == "--lattice-owed") return latticeOwedMain();
    if (mode == "--frame-arms-world") return frameArmsMain(false);
    if (mode == "--frame-arms-lattice") return frameArmsMain(true);
    if (mode == "--decode-exact") return decodeExactMain();
    if (mode == "--coverage-trace") return coverageTraceMain();
    if (mode == "--gpu-coverage") return gpuCoverageMain();
    std::printf("usage: test_scale --world|--voxel-scroll|--lights|--cluster-cut|--levels|--cut-cost|--residency|--decode|"
                "--lights-cost|--occlusion|--tlas|--atlas|--far-field|--bake|--hit-list|--cpu-walks|--lattice-owed|--decode-exact|"
                "--coverage-trace|--gpu-coverage\n");
    return 2;
}
