// mirror.settle_incremental — THE SETTLE IS SPREAD OVER FRAMES AND LANDS THE
// SAME CLASSIFICATION AS THE ONE-SHOT (MIRROR-SETTLE-1, 2026-10-10).
//
// SceneMirror re-derives the scene's SCENE_STATIC classification once the
// document goes quiet after a demotion (scenemirror.cpp, the settle). It used
// to do that as ONE whole-tree applyStaticDefaults on one frame — 392-1115 ms
// of main-thread CPU at 50,000 nodes, a hitch per settle (scenegraph.benchmark
// b.idle_sync caught it). It is now a top-down walk that visits at most
// SceneMirror::kStaticSettleNodesPerSync nodes per sync.
//
// What this suite proves, in COUNTS (never milliseconds — the timings it prints
// are for the reader):
//
//   1. EQUALITY. Two identical 50,000-node documents, each bound to its own
//      mirror, take the same demoting edits. Arm R (the reference) runs the
//      ONE-SHOT root->applyStaticDefaults(); arm I settles through the mirror.
//      Afterwards every node agrees: its graph class (isStaticInGraph) and its
//      recorded hint (staticHint). The scene carries everything the rule
//      distinguishes — user Movable and Static settings, lights (ineligible
//      kinds) with props beneath them, demoted subtrees, demoted leaves, and a
//      STALE classification (a setting replayed without the pass) that the
//      settle has to demote.
//   2. BOUNDED. No sync visits more than kStaticSettleNodesPerSync nodes, and
//      the settle took at least ceil(nodes / budget) syncs — it really spread.
//   3. ONE re-derivation per settle (staticRepromotionCount), as before.
//   4. INTERRUPTED. A transform write part-way through the walk ends it; the
//      next quiet spell re-derives from the root and still lands exactly the
//      one-shot's answer for the same edits.
#include <QGuiApplication>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../support/testmesh.h"
#include "irisgl/irisglfwd.h"
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

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static iris::MeshPtr gCube;
static iris::MaterialPtr gMaterial;

struct Arm {
    const char *label = "";
    Scene *target = nullptr;
    iris::ScenePtr doc;
    std::vector<iris::SceneNodePtr> all;      ///< tree order; identical index = identical node
    std::vector<iris::SceneNodePtr> groups;   ///< the second-level groups
    std::unique_ptr<SceneMirror> mirror;
};

/// THE SCENE. Deterministic, so two arms built by it are node-for-node equal.
///   root
///    └ 40 districts (plain groups)
///        └ 25 blocks each (plain groups) — 1,000 blocks
///            └ 49 props each (cubes)      — 49,000 props
/// ...plus what the classification rule tells apart:
///   * every 7th district is user MOVABLE (its whole subtree resolves movable);
///   * every 11th district is user STATIC;
///   * every 9th block carries a LIGHT (an ineligible kind) with 3 props under it;
///   * every 13th prop is user MOVABLE.
static void build(Arm &a, Engine *engine, const char *label)
{
    a.label = label;
    a.target = engine->createScene(label);
    a.doc = iris::Scene::create();
    auto add = [&](const iris::SceneNodePtr &parent, const iris::SceneNodePtr &n) {
        parent->addChild(n);
        a.all.push_back(n);
    };
    auto prop = [&](const iris::SceneNodePtr &parent, int i) {
        auto m = iris::MeshNode::create();
        m->setMesh(gCube);
        m->setMaterial(gMaterial);
        m->setName(QStringLiteral("p%1").arg(i));
        m->setLocalPos(iris::Vec3(float(i % 7), 0.0f, float(i / 7)));
        add(parent, m);
        return m;
    };
    int serial = 0;
    for (int d = 0; d < 40; ++d) {
        auto district = iris::SceneNode::create();
        district->setName(QStringLiteral("d%1").arg(d));
        district->setLocalPos(iris::Vec3(float(d) * 20.0f, 0, 0));
        if (d % 7 == 3) district->_setMobility(iris::Mobility::Movable);
        if (d % 11 == 5) district->_setMobility(iris::Mobility::Static);
        add(a.doc->getRootNode(), district);
        for (int b = 0; b < 25; ++b) {
            auto block = iris::SceneNode::create();
            block->setName(QStringLiteral("b%1_%2").arg(d).arg(b));
            block->setLocalPos(iris::Vec3(0, 0, float(b) * 10.0f));
            add(district, block);
            a.groups.push_back(block);
            for (int p = 0; p < 49; ++p) {
                auto m = prop(block, serial++);
                if (serial % 13 == 0) m->_setMobility(iris::Mobility::Movable);
            }
            if (b % 9 == 4) {
                auto lamp = iris::LightNode::create();
                lamp->setName(QStringLiteral("lamp%1_%2").arg(d).arg(b));
                add(block, lamp);
                for (int p = 0; p < 3; ++p) prop(lamp, serial++);
            }
        }
    }
    // THE LOAD'S PASS, before the mirror binds — what the reader runs.
    a.doc->getRootNode()->applyStaticDefaults();
    a.mirror.reset(new SceneMirror(a.target));
    a.mirror->setVerifierBudget(0);
    a.mirror->setSource(a.doc);
    a.mirror->sync();
    // ONE warm-up nudge-and-settle per arm, the same in both: the bind migrated
    // every node into the arm's own scene manager (reborn dynamic, hints
    // replayed); the settle puts the tree where the rule says it belongs.
    a.all[size_t(7)]->setLocalPos(a.all[size_t(7)]->getLocalPos() + iris::Vec3(0.01f, 0, 0));
    a.mirror->sync();
    for (quint32 i = 0; i < SceneMirror::kStaticSettleFrames + 2; ++i) a.mirror->sync();
    for (int g = 0; a.mirror->staticSettleInProgress() && g < 100000; ++g) a.mirror->sync();
}

/// THE DEMOTING EDITS, identical in both arms: every 3rd block moved (rule 4
/// demotes its whole subtree), every 17th prop moved (a leaf), and a STALE
/// classification — a block of a static district re-set Movable through the
/// replay setter, which records the setting without running the pass, so the
/// block is still in the static half and only a re-derivation demotes it.
static void edit(Arm &a, int round)
{
    const float dx = round % 2 ? 0.25f : -0.25f;
    for (size_t i = 0; i < a.groups.size(); i += 3)
        a.groups[i]->setLocalPos(a.groups[i]->getLocalPos() + iris::Vec3(dx, 0, 0));
    for (size_t i = 5; i < a.all.size(); i += 17)
        if (a.all[i]->getSceneNodeType() == iris::SceneNodeType::Mesh)
            a.all[i]->setLocalPos(a.all[i]->getLocalPos() + iris::Vec3(0, dx, 0));
    a.groups[size_t(round == 0 ? 26 : 52)]->_setMobility(iris::Mobility::Movable);
}

struct Snapshot {
    std::vector<char> inGraph, hint;
    size_t statics = 0;
};
static Snapshot snap(const Arm &a)
{
    Snapshot s;
    s.inGraph.reserve(a.all.size());
    s.hint.reserve(a.all.size());
    for (const auto &n : a.all) {
        const bool st = n->isStaticInGraph();
        s.inGraph.push_back(st ? 1 : 0);
        s.hint.push_back(n->staticHint() ? 1 : 0);
        if (st) ++s.statics;
    }
    return s;
}
static size_t differences(const Snapshot &x, const Snapshot &y, const Arm &a)
{
    size_t diff = 0;
    for (size_t i = 0; i < x.inGraph.size() && i < y.inGraph.size(); ++i) {
        if (x.inGraph[i] == y.inGraph[i] && x.hint[i] == y.hint[i]) continue;
        if (diff < 8)
            std::printf("    DIFF node %zu '%s': static %d/%d hint %d/%d (one-shot/incremental)\n", i,
                        qPrintable(a.all[i]->getName()), x.inGraph[i], y.inGraph[i], x.hint[i], y.hint[i]);
        ++diff;
    }
    return diff + (x.inGraph.size() != y.inGraph.size() ? 1 : 0);
}

/// Drives the incremental settle to completion. Returns the number of syncs
/// that ran a slice; reports the largest slice and the slowest sync.
struct SettleRun { int slices = 0; quint32 maxVisits = 0; double maxMs = 0; quint64 repro = 0; };
static SettleRun settle(Arm &a, int interruptAfterSlices = -1, int interruptRound = -1)
{
    SettleRun r;
    const quint64 repro0 = a.mirror->staticRepromotionCount();
    bool interrupted = false;
    for (int guard = 0; guard < 200000; ++guard) {
        const auto t0 = Clock::now();
        a.mirror->sync();
        const double ms = msSince(t0);
        const quint32 v = a.mirror->staticSettleVisits();
        if (v > 0) {
            ++r.slices;
            r.maxVisits = std::max(r.maxVisits, v);
            r.maxMs = std::max(r.maxMs, ms);
        }
        if (!interrupted && interruptAfterSlices >= 0 && r.slices == interruptAfterSlices) {
            interrupted = true;
            edit(a, interruptRound);    // a write part-way through the walk
            continue;
        }
        if (r.slices > 0 && !a.mirror->staticSettleInProgress()
            && a.mirror->staticRepromotionCount() != repro0)
            break;
    }
    r.repro = a.mirror->staticRepromotionCount() - repro0;
    return r;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_mirror_settle-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    // A view before the first document node (the staging scene needs a
    // window and the Hlms — see test_mirror_scale).
    View *view = engine->createOffscreenView("settle", 32, 32, Colour(0, 0, 0));
    CHECK(view != nullptr, "offscreen view (the graph device)");
    if (!view) return 1;

    gCube = testmesh::load(":assets/models/cube.obj");
    gMaterial = iris::PbrMaterial::create();

    Arm R, I;
    build(R, engine.get(), "one-shot");
    build(I, engine.get(), "incremental");
    const size_t nodes = I.all.size() + 1;   // + the document root
    std::printf("    %zu document nodes per arm, budget %u nodes/sync\n", nodes,
                SceneMirror::kStaticSettleNodesPerSync);
    CHECK(nodes >= 50000, "the scene is 50,000 nodes");
    CHECK(differences(snap(R), snap(I), I) == 0, "the two arms start identical");

    const quint32 B = SceneMirror::kStaticSettleNodesPerSync;
    const int minSlices = int((nodes + B - 1) / B);

    // ---- 1-3: a settle after demoting edits --------------------------------
    {
        const Snapshot before = snap(I);
        edit(R, 0); R.mirror->sync();
        edit(I, 0); I.mirror->sync();
        const Snapshot demoted = snap(I);
        std::printf("    static nodes: %zu settled, %zu after the edits\n", before.statics, demoted.statics);
        CHECK(demoted.statics < before.statics, "the edits demoted part of the static half");

        const auto t0 = Clock::now();
        R.doc->getRootNode()->applyStaticDefaults();
        const double oneShotMs = msSince(t0);
        const Snapshot ref = snap(R);

        const SettleRun run = settle(I);
        const Snapshot inc = snap(I);
        std::printf("    one-shot %.1f ms on one call | incremental: %d slices, max %u visits, "
                    "slowest sync %.2f ms | static %zu vs %zu\n",
                    oneShotMs, run.slices, run.maxVisits, run.maxMs, ref.statics, inc.statics);
        CHECK(run.repro == 1, "the settle re-derived the classification exactly ONCE");
        CHECK(run.maxVisits <= B, "no sync visited more than kStaticSettleNodesPerSync nodes");
        CHECK(run.slices >= minSlices, "the settle spread over at least ceil(nodes / budget) syncs");
        CHECK(ref.statics > demoted.statics, "the one-shot re-promoted the demoted props");
        CHECK(differences(ref, inc, I) == 0,
              "EQUALITY: every node's graph class and hint match the one-shot");

        // A further quiet spell re-derives nothing.
        const quint64 r0 = I.mirror->staticRepromotionCount();
        for (quint32 i = 0; i < SceneMirror::kStaticSettleFrames * 2; ++i) I.mirror->sync();
        CHECK(I.mirror->staticRepromotionCount() == r0 && !I.mirror->staticSettleInProgress(),
              "a scene that stays still settles nothing further");
    }

    // ---- 4: a write part-way through the walk ------------------------------
    {
        edit(R, 1); R.mirror->sync();
        edit(I, 1); I.mirror->sync();
        // The reference sees the same two edit rounds back to back, then ONE
        // one-shot — what the scene's classification must be after both.
        edit(R, 2); R.mirror->sync();
        R.doc->getRootNode()->applyStaticDefaults();
        const Snapshot ref = snap(R);

        // The incremental arm takes the second round 5 slices into its walk.
        const SettleRun run = settle(I, 5, 2);
        const Snapshot inc = snap(I);
        std::printf("    interrupted settle: %d slices in all, static %zu vs %zu\n", run.slices,
                    ref.statics, inc.statics);
        CHECK(run.repro == 1, "the interrupted settle completed ONCE, after the next quiet spell");
        CHECK(run.slices >= 5 + minSlices, "...by re-walking the whole tree from the root");
        CHECK(differences(ref, inc, I) == 0,
              "EQUALITY after an interruption: the same classification as the one-shot");
    }

    R.mirror->setSource(iris::ScenePtr());
    I.mirror->setSource(iris::ScenePtr());
    std::printf("%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
