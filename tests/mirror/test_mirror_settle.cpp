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
//   4. INTERRUPTED. A transform write part-way through the walk does NOT
//      restart it: the written nodes wait out their own quiet window, the
//      rotation goes on, and the next rotation puts them back — still exactly
//      the one-shot's answer for the same edits.
//   5. MUTATED part-way through the walk: a subtree DESTROYED (the walk holds
//      it weakly), a block REPARENTED, a block REMOVED from the document — the
//      settle completes and lands the one-shot's answer for the mutated tree.
//   6. A SCENE SWITCH part-way through the walk (setSource away and back):
//      nothing of the outgoing walk survives, the rebind owes its own rotation.
//   7. THE DEMOTION'S WORST CASE (lead read 1, item 3): one write to the root
//      of a 50,000-node static subtree demotes all of it on the write path —
//      reported in ms beside the frame that carries the move.
//   8. A PERPETUAL WRITER does not starve the settle: one node of the document
//      written on EVERY sync, and a demoted prop elsewhere still comes back
//      within SceneMirror::kStaticSettleMaxWaitSyncs + a rotation (counted in
//      syncs), while the written node stays dynamic.
//   9. JAH_MIRROR_VERIFY=full (every sync a full walk): a write still stamps
//      its quiet clock and the prop re-promotes within kStaticSettleFrames +
//      ceil(n / kStaticSettleNodesPerSync) + 8 syncs (read 2, F1).
#include <QGuiApplication>

#include <algorithm>
#include <chrono>
#include <functional>
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
struct SettleRun {
    int slices = 0; quint32 maxVisits = 0; double maxMs = 0; quint64 repro = 0;
    std::vector<double> sliceMs;   ///< every slice's sync time — the budget's derivation reads these
    double pct(double q) const {
        if (sliceMs.empty()) return 0.0;
        std::vector<double> v = sliceMs;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, size_t(q * double(v.size() - 1) + 0.5))];
    }
    int over(double ms) const { return int(std::count_if(sliceMs.begin(), sliceMs.end(),
                                                          [ms](double x) { return x > ms; })); }
};
static SettleRun settle(Arm &a, int interruptAfterSlices = -1,
                        const std::function<void(Arm &)> &interrupt = {})
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
            r.sliceMs.push_back(ms);
        }
        if (!interrupted && interruptAfterSlices >= 0 && r.slices == interruptAfterSlices) {
            interrupted = true;
            interrupt(a);               // part-way through the walk
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
        std::printf("    slice sync ms: median %.3f p95 %.3f p99 %.3f max %.3f | %d of %d over 1 ms\n",
                    run.pct(0.5), run.pct(0.95), run.pct(0.99), run.maxMs, run.over(1.0), run.slices);
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
        const SettleRun run = settle(I, 5, [](Arm &a) { edit(a, 2); });
        const Snapshot inc = snap(I);
        std::printf("    interrupted settle: %d slices in all, static %zu vs %zu\n", run.slices,
                    ref.statics, inc.statics);
        CHECK(run.repro == 1, "the interrupted settle completed ONCE, after the written nodes' quiet window");
        CHECK(run.slices >= 5 + minSlices, "...the rotation went on, and the next one walked the whole tree");
        CHECK(differences(ref, inc, I) == 0,
              "EQUALITY after an interruption: the same classification as the one-shot");
    }

    // ---- 5: destruction, reparent and removal part-way through the walk ----
    {
        // A group of props outside `all` (so a snapshot never holds it), in
        // both arms, settled in with everything else before the edits.
        auto addExtra = [](Arm &a) {
            auto g = iris::SceneNode::create();
            g->setName(QStringLiteral("extra"));
            a.doc->getRootNode()->addChild(g);
            for (int i = 0; i < 20; ++i) {
                auto m = iris::MeshNode::create();
                m->setMesh(gCube);
                m->setMaterial(gMaterial);
                g->addChild(m);
            }
            return g;
        };
        iris::SceneNodePtr xR = addExtra(R), xI = addExtra(I);
        R.doc->getRootNode()->applyStaticDefaults();
        settle(I);
        // The mutations, identical in both arms: the extra group removed and
        // DESTROYED (its last reference dropped), block 100 reparented under
        // block 200, block 150 removed from the document (kept alive by `all`).
        auto mutate = [](Arm &a, iris::SceneNodePtr &extra) {
            extra->removeFromParent();
            extra.reset();
            a.groups[200]->addChild(a.groups[100]);
            a.groups[150]->removeFromParent();
        };
        edit(R, 0); R.mirror->sync();
        mutate(R, xR);
        R.mirror->sync();
        R.doc->getRootNode()->applyStaticDefaults();
        const Snapshot ref = snap(R);

        edit(I, 0); I.mirror->sync();
        QWeakPointer<iris::SceneNode> extraWeak = xI.toWeakRef();
        const SettleRun run = settle(I, 5, [&xI, &mutate](Arm &a) { mutate(a, xI); });
        const Snapshot inc = snap(I);
        std::printf("    mutated mid-walk: %d slices in all, static %zu vs %zu\n", run.slices,
                    ref.statics, inc.statics);
        CHECK(extraWeak.isNull(), "the extra group was destroyed part-way through the walk");
        CHECK(run.repro == 1, "the mutated settle completed ONCE");
        CHECK(differences(ref, inc, I) == 0,
              "EQUALITY after destruction + reparent + removal: the one-shot's classification");
    }

    // ---- 6: a scene switch part-way through the walk ------------------------
    {
        edit(R, 1); R.mirror->sync();
        R.doc->getRootNode()->applyStaticDefaults();
        const Snapshot ref = snap(R);

        edit(I, 1); I.mirror->sync();
        iris::ScenePtr other = iris::Scene::create();
        const SettleRun run = settle(I, 5, [&other](Arm &a) {
            a.mirror->setSource(other);           // away: the walk goes with the tree
            CHECK(!a.mirror->staticSettleInProgress(),
                  "a scene switch drops the outgoing document's settle");
            a.mirror->sync();
            a.mirror->setSource(a.doc);           // back: the rebind owes its own rotation
        });
        const Snapshot inc = snap(I);
        std::printf("    scene switch mid-walk: %d slices in all, static %zu vs %zu\n", run.slices,
                    ref.statics, inc.statics);
        CHECK(run.repro == 1, "the rebound document settled ONCE");
        CHECK(differences(ref, inc, I) == 0,
              "EQUALITY after a scene switch: the one-shot's classification");
    }

    // ---- 7: the demotion's worst case: one write to a 50,000-node static root -
    {
        Arm W;
        W.target = engine->createScene("demote");
        W.doc = iris::Scene::create();
        auto g = iris::SceneNode::create();
        g->setName(QStringLiteral("group"));
        W.doc->getRootNode()->addChild(g);
        for (int i = 0; i < 50000; ++i) {
            auto m = iris::MeshNode::create();
            m->setMesh(gCube);
            m->setMaterial(gMaterial);
            m->setLocalPos(iris::Vec3(float(i % 250), 0.0f, float(i / 250)));
            g->addChild(m);
        }
        W.doc->getRootNode()->applyStaticDefaults();
        W.mirror.reset(new SceneMirror(W.target));
        W.mirror->setVerifierBudget(0);
        W.mirror->setSource(W.doc);
        W.mirror->sync();
        for (int k = 0; W.mirror->staticSettleInProgress() && k < 100000; ++k) W.mirror->sync();
        const bool before = g->isStaticInGraph();
        const auto t0 = Clock::now();
        g->setLocalPos(g->getLocalPos() + iris::Vec3(0.5f, 0, 0));   // rule 4: the whole subtree
        const double writeMs = msSince(t0);
        const auto t1 = Clock::now();
        W.mirror->sync();
        const double syncMs = msSince(t1);
        size_t dynamicNow = 0;
        for (int i = 0; i < g->childCount(); ++i)
            if (iris::SceneNode *c = g->childAt(i)) if (!c->isStaticInGraph()) ++dynamicNow;
        std::printf("    demotion worst case: one write to a static group of 50,000 props: "
                    "write %.2f ms (the subtree switch), next sync %.2f ms\n", writeMs, syncMs);
        CHECK(before && !g->isStaticInGraph() && dynamicNow == 50000,
              "rule 4: one write demoted the whole 50,000-node subtree");
        W.mirror->setSource(iris::ScenePtr());
    }

    // ---- 8: a node written on every sync does not starve the others ------------
    {
        Arm P;
        P.target = engine->createScene("perpetual");
        P.doc = iris::Scene::create();
        auto g = iris::SceneNode::create();
        P.doc->getRootNode()->addChild(g);
        const int count = 2000;
        std::vector<iris::SceneNodePtr> props;
        for (int i = 0; i < count; ++i) {
            auto m = iris::MeshNode::create();
            m->setMesh(gCube);
            m->setMaterial(gMaterial);
            m->setLocalPos(iris::Vec3(float(i % 50), 0.0f, float(i / 50)));
            g->addChild(m);
            props.push_back(m);
        }
        auto spinner = iris::MeshNode::create();          // Auto, eligible: resolves static
        spinner->setMesh(gCube);
        spinner->setMaterial(gMaterial);
        P.doc->getRootNode()->addChild(spinner);
        P.doc->getRootNode()->applyStaticDefaults();
        P.mirror.reset(new SceneMirror(P.target));
        P.mirror->setVerifierBudget(0);
        P.mirror->setSource(P.doc);
        P.mirror->sync();
        for (int k = 0; P.mirror->staticSettleInProgress() && k < 100000; ++k) P.mirror->sync();
        const iris::SceneNodePtr victim = props[size_t(count / 2)];
        const bool settledBefore = victim->isStaticInGraph() && spinner->isStaticInGraph();
        victim->setLocalPos(victim->getLocalPos() + iris::Vec3(0.0f, 0.5f, 0.0f));   // rule 4
        const quint32 B = SceneMirror::kStaticSettleNodesPerSync;
        const int bound = int(SceneMirror::kStaticSettleMaxWaitSyncs)
                        + 2 * int((quint32(count) + 2 + B - 1) / B) + 2;
        int syncs = 0;
        bool demoted = false;
        for (; syncs < 20 * bound; ++syncs) {
            spinner->setLocalPos(spinner->getLocalPos() + iris::Vec3(0.001f, 0, 0));
            P.mirror->sync();
            if (!victim->isStaticInGraph()) demoted = true;
            else if (demoted) break;
        }
        std::printf("    perpetual writer: the demoted prop came back after %d syncs (bound %d); "
                    "the written node static: %d\n", syncs, bound, int(spinner->isStaticInGraph()));
        CHECK(settledBefore && demoted, "the write demoted the prop (and the scene had settled before)");
        CHECK(victim->isStaticInGraph() && syncs <= bound,
              "a node written on every sync does not starve the settle (kStaticSettleMaxWaitSyncs)");
        CHECK(!spinner->isStaticInGraph(), "...and the node written on every sync stays dynamic");
        P.mirror->setSource(iris::ScenePtr());
    }

    // ---- 9: the verification mode stamps writes too ------------------------------
    {
        qputenv("JAH_MIRROR_VERIFY", "full");     // read by the mirror's constructor
        Arm V;
        V.target = engine->createScene("verifyfull");
        V.doc = iris::Scene::create();
        auto g = iris::SceneNode::create();
        V.doc->getRootNode()->addChild(g);
        const int count = 2000;
        std::vector<iris::SceneNodePtr> props;
        for (int i = 0; i < count; ++i) {
            auto m = iris::MeshNode::create();
            m->setMesh(gCube);
            m->setMaterial(gMaterial);
            m->setLocalPos(iris::Vec3(float(i % 50), 0.0f, float(i / 50)));
            g->addChild(m);
            props.push_back(m);
        }
        V.doc->getRootNode()->applyStaticDefaults();
        V.mirror.reset(new SceneMirror(V.target));
        qunsetenv("JAH_MIRROR_VERIFY");
        V.mirror->setVerifierBudget(0);
        V.mirror->setSource(V.doc);
        V.mirror->sync();
        for (int k = 0; V.mirror->staticSettleInProgress() && k < 100000; ++k) V.mirror->sync();
        const iris::SceneNodePtr victim = props[size_t(count / 3)];
        const bool settledBefore = victim->isStaticInGraph();
        victim->setLocalPos(victim->getLocalPos() + iris::Vec3(0.0f, 0.5f, 0.0f));   // rule 4
        const quint32 B = SceneMirror::kStaticSettleNodesPerSync;
        const int nodes = count + 2;                                  // + the root and the group
        const int bound = int(SceneMirror::kStaticSettleFrames) + int((quint32(nodes) + B - 1) / B) + 8;
        int syncs = 0;
        bool demoted = false;
        for (; syncs < 20 * bound; ++syncs) {
            V.mirror->sync();
            if (!victim->isStaticInGraph()) demoted = true;
            else if (demoted) break;
        }
        std::printf("    JAH_MIRROR_VERIFY=full: the written prop came back after %d syncs (bound %d)\n",
                    syncs, bound);
        CHECK(settledBefore && demoted, "verify=full: the write demoted the prop (and the scene had settled)");
        CHECK(victim->isStaticInGraph() && syncs <= bound,
              "verify=full: the write's quiet clock was stamped and the prop re-promoted (F1)");
        V.mirror->setSource(iris::ScenePtr());
    }

    R.mirror->setSource(iris::ScenePtr());
    I.mirror->setSource(iris::ScenePtr());
    std::printf("%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
