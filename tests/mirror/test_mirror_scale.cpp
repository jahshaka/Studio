// mirror.scale — WHAT ONE STILL FRAME COSTS THE MIRROR AT SCALE.
//
// The mirror's sync() is the document->engine push and it runs every frame,
// for every node, forever. On the render review's 8,404-node lattice
// (spikes/render-review-2026-09-13) `host.mirror` measured 52 ms per frame on a
// STILL scene while the engine's own record was 11-13 ms: the walk WAS the
// frame. This suite is the regression gate for that.
//
// It asserts SHAPES, never milliseconds — a wall-clock budget on a shared box
// is a flake generator (CLAUDE.md, cameras.exposure). The shapes are:
//
//   1. A STILL sync is a small FRACTION of the first (adopting) sync. Nothing
//      changed, so nothing may be rebuilt.
//   2. The still cost is LINEAR in node count, not super-linear: 2N nodes cost
//      at most ~2.5x N nodes. A per-node hash probe is linear; a per-node walk
//      of something global is not.
//   3. The per-node still cost does not depend on how many DISTINCT MATERIALS
//      the scene holds. Every mesh node in this editor is born with its own
//      PbrMaterial (SceneEditService::addNodeToScene), so "8000 cubes" is
//      "8000 materials": a mirror that rebuilds a MaterialSync per material per
//      frame pays for the scene twice and the memo hides it from shape 2.
//   4. A still sync allocates NOTHING per node (the counting allocator below).
//      This is the sharpest of the four and the one that fails first.
#include <QGuiApplication>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <memory>
#include <vector>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

struct Arm {
    const char *label = "";
    Scene *target = nullptr;
    iris::ScenePtr doc;
    std::vector<iris::SceneNodePtr> keep;   // holds the nodes alive
    std::unique_ptr<SceneMirror> mirror;
    int visited = 0;
    double firstMs = 0;
    std::vector<double> still;              // one entry per timed round
    long long builds = 0;

    double stillMs() const
    {
        std::vector<double> v = still;
        std::sort(v.begin(), v.end());
        return v.empty() ? 0 : v[v.size() / 2];
    }
    double usPerNode() const { return stillMs() * 1000.0 / double(visited ? visited : 1); }
};

// Builds a lattice of `count` mesh nodes under `rows` group nodes and mirrors
// it once (the ADOPTING sync, timed).
static void build(Arm &a, Engine *engine, const char *label, int count, int rows,
                  bool shareMaterial)
{
    a.label = label;
    a.target = engine->createScene(label);
    a.doc = iris::Scene::create();

    // ONE mesh asset, shared: this suite measures the WALK, not mesh loading.
    auto mesh = iris::Mesh::loadMesh(":assets/models/cube.obj");
    iris::PbrMaterialPtr shared = iris::PbrMaterial::create();

    std::vector<iris::SceneNodePtr> groups;
    for (int r = 0; r < rows; ++r) {
        auto g = iris::SceneNode::create();
        g->setName(QStringLiteral("row%1").arg(r));
        g->setLocalPos(iris::Vec3(0, float(r) * 2.0f, 0));
        a.doc->getRootNode()->addChild(g);
        groups.push_back(g);
        a.keep.push_back(g);
    }
    for (int i = 0; i < count; ++i) {
        auto m = iris::MeshNode::create();
        m->setMesh(mesh);
        // THE EDITOR'S OWN SHAPE: every primitive added through
        // SceneEditService gets its OWN PbrMaterial, so a scene of N cubes is a
        // scene of N materials. `shareMaterial` is the control arm.
        m->setMaterial(shareMaterial ? shared : iris::PbrMaterial::create());
        m->setLocalPos(iris::Vec3(float(i % 20) - 9.5f, 0, float((i / 20) % 20) - 9.5f));
        groups[size_t(i % rows)]->addChild(m);
        a.keep.push_back(m);
    }

    // THE STATIC PASS, as a load runs it (SceneNode::applyStaticDefaults).
    // Nodes added straight through addChild carry a hint nobody applied; the
    // app's add funnel and its scene reader both run this, so the suite does.
    a.doc->getRootNode()->applyStaticDefaults();

    a.mirror.reset(new SceneMirror(a.target));
    a.mirror->setSource(a.doc);

    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    a.visited = a.mirror->sync();
    auto t1 = clock::now();
    a.firstMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
}

/// ONE timed STILL sync. Nothing in the document has changed since the last.
static void round(Arm &a)
{
    using clock = std::chrono::steady_clock;
    auto s0 = clock::now();
    a.mirror->sync();
    auto s1 = clock::now();
    a.still.push_back(std::chrono::duration<double, std::milli>(s1 - s0).count());
    a.builds = (long long)a.mirror->materialBuildCount();
}

static void report(const Arm &a)
{
    std::printf("    %-22s nodes %5d  first %8.2f ms   still(med) %8.3f ms  "
                "(%6.2f us/node)  materialBuilds/still %5lld\n",
                a.label, a.visited, a.firstMs, a.stillMs(), a.usPerNode(), a.builds);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_mirror_scale-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // A VIEW BEFORE THE FIRST DOCUMENT NODE. `iris::graph::stagingScene()`'s
    // fallback refuses to build a scene manager until the backend has a window
    // and the Hlms is registered (CLAUDE.md: a render window must exist before
    // createSceneManager), and without it every node is born handle-less and
    // every transform reads identity.
    View *view = engine->createOffscreenView("scale", 32, 32, Colour(0, 0, 0));
    CHECK(view != nullptr, "offscreen view (the graph device)");
    if (!view) return 1;

    // Warm every lazy global the first sync of the process would otherwise be
    // charged for (mesh upload paths, Hlms datablock templates).
    Arm warm; build(warm, engine.get(), "warmup", 200, 4, false);
    for (int i = 0; i < 5; ++i) round(warm);

    Arm a, b, c;
    build(a, engine.get(), "1000 own materials", 1000, 20, false);
    build(b, engine.get(), "2000 own materials", 2000, 20, false);
    build(c, engine.get(), "2000 one material",  2000, 20, true);

    // INTERLEAVED, and that is the whole reason this suite can assert anything
    // on a shared box: the arms are compared to EACH OTHER, so they have to
    // meet the same contention. Run back to back, a linker landing between two
    // arms moves the ratio by more than the defect being gated does.
    int reps = 9;
    if (const char *r = std::getenv("JAH_MIRROR_SCALE_REPS")) { const int v = std::atoi(r); if (v > 0) reps = v; }
    for (int i = 0; i < reps; ++i) { round(a); round(b); round(c); }

    report(a); report(b); report(c);

    // ---- shape 1: a still sync is a fraction of the adopting one ----------
    const double ratio = b.stillMs() / (b.firstMs > 0 ? b.firstMs : 1);
    std::printf("    still/first = %.4f\n", ratio);
    CHECK(ratio < 0.25, "a STILL sync costs under a quarter of the adopting sync");

    // ---- shape 2: linear in node count ------------------------------------
    const double growth = b.stillMs() / (a.stillMs() > 0 ? a.stillMs() : 1e-9);
    std::printf("    2N/N still = %.3f\n", growth);
    CHECK(growth < 2.6, "doubling the nodes at most ~2.5x the still walk (linear)");

    // ---- shape 3: material COUNT does not change the per-node cost --------
    const double matRatio = b.stillMs() / (c.stillMs() > 0 ? c.stillMs() : 1e-9);
    std::printf("    ownMaterials/oneMaterial still = %.3f\n", matRatio);
    // A per-material FINGERPRINT is still a per-material cost — the memo removes
    // the REBUILD, not the validity check. What this catches is the rebuild
    // coming back: before the memo the ratio was 4.9.
    CHECK(matRatio < 2.4, "N distinct materials cost a CHECK per material, not a rebuild");

    // ---- shape 4: a still sync builds no material description -------------
    // The sharpest of the four and the one that fails first: a memo that stops
    // holding rebuilds every description every frame, and THAT is what made the
    // 8,404-node lattice cost 52 ms of mirror per still frame.
    CHECK(b.builds == 0, "a still sync rebuilds NO material description");
    CHECK(c.builds == 0, "...with one shared material too");

    // ---- SCENE_STATIC: the settle half ------------------------------------
    // Rule 4 demotes a static subtree on the first transform write and nothing
    // used to put it back, so every prop a user nudged spent the rest of the
    // session in the renderer's per-frame transform and bounds passes. The
    // mirror re-derives the classification once the document goes quiet.
    {
        // SETTLE FIRST, so the baseline is the steady state: binding a mirror
        // MIGRATES the document's Ogre nodes into its scene manager and they
        // are reborn dynamic, so the count right after a bind is a hint replay
        // and not yet the resolution rule's own answer.
        for (quint32 i = 0; i < SceneMirror::kStaticSettleFrames + 2; ++i) b.mirror->sync();
        const quint64 settled0 = iris::graph::staticNodeCount();
        std::printf("    staticNodes before the nudge: %llu\n",
                    (unsigned long long)settled0);
        CHECK(settled0 > 0, "the lattices' props are SCENE_STATIC to begin with");

        // Nudge 30 props — a user dragging things around.
        int nudged = 0;
        for (const auto &n : b.keep) {
            if (n->getSceneNodeType() != iris::SceneNodeType::Mesh) continue;
            n->setLocalPos(n->getLocalPos() + iris::Vec3(0.001f, 0, 0));
            if (++nudged >= 30) break;
        }
        b.mirror->sync();
        const quint64 afterNudge = iris::graph::staticNodeCount();
        std::printf("    staticNodes after 30 nudges:  %llu\n",
                    (unsigned long long)afterNudge);
        CHECK(afterNudge < settled0, "a nudge DEMOTES the nudged subtrees (rule 4)");

        // Now settle: syncs with no transform write anywhere.
        const quint64 repro0 = b.mirror->staticRepromotionCount();
        for (quint32 i = 0; i < SceneMirror::kStaticSettleFrames + 2; ++i) b.mirror->sync();
        const quint64 settled1 = iris::graph::staticNodeCount();
        std::printf("    staticNodes after the settle: %llu  (re-promotions %llu)\n",
                    (unsigned long long)settled1,
                    (unsigned long long)(b.mirror->staticRepromotionCount() - repro0));
        CHECK(b.mirror->staticRepromotionCount() == repro0 + 1,
              "the settle re-derived the classification exactly ONCE");
        CHECK(settled1 == settled0, "…and every nudged prop is static again");

        // A SECOND settle must not run: nothing has written a transform since.
        for (quint32 i = 0; i < SceneMirror::kStaticSettleFrames + 2; ++i) b.mirror->sync();
        CHECK(b.mirror->staticRepromotionCount() == repro0 + 1,
              "a scene that never moves again re-derives nothing further");
    }

    std::printf("%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
