// mirror.extract_movers / mirror.extract_movers_10k — THE MIRROR'S EXTRACT WITH EVERY NODE MOVING (ENGINE-UPGRADE-U4 stage 5).
//
// The sync's journal take and its emit: each written node's entry looked up ONCE, the owed fields and the written stamp
// ON the entry (the side maps deleted), the order the node KEYS (never an Entry* into the QHash — Qt 6 moves nodes on
// rehash and erase), and a mirrored node named with no stack. The arms, interleaved so they meet the same contention:
// a FULL walk (requestFullWalk), 100 % movers (every cube written), still. The law for every size: the list degrades to
// the walk and never past it (100 % movers < 1.5 x the full walk — mirror.scale's own law, at this size); the measure
// row (`--nodes 10000`, by hand) also holds the stage's bar: 100 % movers <= 12 ms at 10k (the deep read's app capture
// measured 38.4 ms on the UI thread).
//
// AND THE SETTLE'S HOLE (stage 5 (b), mirror.settle_incremental §5's rule): a write during a running rotation owes a next
// rotation only behind the running one — the node already visited, or its CURRENT parent visited. The arm here is the
// one §5's mutation may never hit: an unvisited, demoted node REPARENTED UNDER AN ALREADY-VISITED parent part-way
// through the rotation; the running rotation never reaches it (its queued entry fails the parent check), so the write
// must owe the next one — after the settle the node is static again, as the one-shot applyStaticDefaults says.
#include <QGuiApplication>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../support/testmesh.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, ...) do { char m_[512]; std::snprintf(m_, sizeof m_, __VA_ARGS__); \
    if (cond) std::printf("ok:   %s\n", m_); else { std::printf("FAIL: %s\n", m_); ++failures; } } while (0)

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

struct Doc {
    Scene *target = nullptr;
    iris::ScenePtr doc;
    std::vector<iris::SceneNodePtr> groups, cubes;
    std::unique_ptr<SceneMirror> mirror;
};

static void build(Doc &d, Engine *engine, const char *label, int count, int rows)
{
    d.target = engine->createScene(label);
    d.doc = iris::Scene::create();
    auto mesh = testmesh::load(":assets/models/cube.obj");
    // THE EDITOR'S OWN SHAPE (mirror.scale's): every primitive carries its own material — the full walk the movers are
    // held to pays its material memo probes, as the app's does.
    for (int r = 0; r < rows; ++r) {
        auto g = iris::SceneNode::create();
        g->setName(QStringLiteral("row%1").arg(r));
        g->setLocalPos(iris::Vec3(0, float(r) * 2.0f, 0));
        d.doc->getRootNode()->addChild(g);
        d.groups.push_back(g);
    }
    for (int i = 0; i < count; ++i) {
        auto m = iris::MeshNode::create();
        m->setMesh(mesh);
        m->setMaterial(iris::PbrMaterial::create());
        m->setLocalPos(iris::Vec3(float(i % 100) * 1.1f - 55.0f, 0, float((i / 100) % 100) * 1.1f - 55.0f));
        d.groups[size_t(i % rows)]->addChild(m);
        d.cubes.push_back(m);
    }
    d.doc->getRootNode()->applyStaticDefaults();
    d.mirror.reset(new SceneMirror(d.target));
    d.mirror->setVerifierBudget(0);   // the walk is measured, not the amortised verifier (mirror.scale's note)
    d.mirror->setSource(d.doc);
    d.mirror->sync();
}

int main(int argc, char **argv)
{
    int nodes = 2000, profileRounds = 0;
    for (int i = 1; i + 1 < argc; ++i) if (!std::strcmp(argv[i], "--nodes")) nodes = std::atoi(argv[i + 1]);
    // --profile N: after the build, N rounds of (every cube written, one sync) and nothing else — for a profiler.
    for (int i = 1; i + 1 < argc; ++i) if (!std::strcmp(argv[i], "--profile")) profileRounds = std::atoi(argv[i + 1]);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const auto cfg = enginetest::testEngineConfig("test_mirror_extract_movers-ogre.log");
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    View *view = engine->createOffscreenView("extract", 32, 32, Colour(0, 0, 0));
    CHECK(view != nullptr, "offscreen view (the graph device)");
    if (!view) return 1;

    // ---- THE EXTRACT WITH EVERY NODE MOVING ------------------------------------------------------------------------
    {
        Doc d;
        build(d, engine.get(), "extract", nodes, 20);
        for (int i = 0; i < SceneMirror::kStaticSettleFrames * 6; ++i) d.mirror->sync();   // the first settle spent
        float lift = 0.0f;
        const auto moveAll = [&]() {
            lift = lift > 0.0f ? 0.0f : 0.001f;
            for (const auto &c : d.cubes) c->setLocalPos(c->getLocalPos() + iris::Vec3(0, lift > 0.0f ? lift : -0.001f, 0));
        };
        const auto timed = [&]() { const auto t0 = Clock::now(); d.mirror->sync(); return msSince(t0); };
        if (profileRounds > 0) {
            std::printf("PROFILE_START\n");
            std::fflush(stdout);
            const auto t0 = Clock::now();
            for (int i = 0; i < profileRounds; ++i) { moveAll(); d.mirror->sync(); }
            std::printf("PROFILE_END %d rounds %.3f ms each\n", profileRounds, msSince(t0) / profileRounds);
            return 0;
        }
        for (int i = 0; i < 3; ++i) { d.mirror->requestFullWalk(); timed(); moveAll(); timed(); timed(); }
        // THE WALK AT EQUAL WORK (mirror.scale's law as MAIN-REDS-1 stage 4a restated it): every node moved, then the
        // full walk — both arms pay each written node's extract, so the ratio reads the list's own overhead.
        std::vector<double> fullMs, fullMovedMs, allMs, stillMs;
        std::vector<std::pair<std::string, std::vector<double>>> stages;   // the movers sync's MirrorStage spans
        d.mirror->setStageLog(true);
        for (int i = 0; i < 9; ++i) {
            d.mirror->requestFullWalk(); fullMs.push_back(timed());
            moveAll();
            d.mirror->requestFullWalk(); fullMovedMs.push_back(timed());
            moveAll();                   allMs.push_back(timed());
            for (const auto &st : d.mirror->lastStageLog()) {
                auto it = std::find_if(stages.begin(), stages.end(), [&](const auto &p) { return p.first == st.first; });
                if (it == stages.end()) { stages.push_back({ st.first, {} }); it = stages.end() - 1; }
                it->second.push_back(st.second);
            }
            stillMs.push_back(timed());
        }
        d.mirror->setStageLog(false);
        auto med = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        const double full = med(fullMs), fullMoved = med(fullMovedMs), all = med(allMs), still = med(stillMs);
        std::printf("    %d cubes: full walk %7.3f ms (every node moved %7.3f) | 100%% movers %7.3f (%.3f us/node) | still "
                    "%7.3f\n", nodes, full, fullMoved, all, all * 1000.0 / double(nodes), still);
        std::printf("    the movers sync's stages (median ms):");
        for (auto &st : stages) if (med(st.second) >= 0.05) std::printf(" %s %.3f", st.first.c_str(), med(st.second));
        std::printf("\n");
        CHECK(all < fullMoved * 1.5, "100%% movers cost about what the full walk of the same moves cost — the list "
              "degrades to it, never past (%.3f vs %.3f ms)", all, fullMoved);
        // THE ORACLE (DIRTY_SET_MIRROR_SPEC §7.1): the full walk straight after a movers sync pushes NOTHING — the dirty
        // path (its transform-only fast path included) left no latch behind.
        moveAll();
        d.mirror->sync();
        const quint64 late = d.mirror->verifyAgainstFullWalk();
        CHECK(late == 0, "the full walk after the movers sync pushes nothing (%llu late)", (unsigned long long)late);
        if (nodes >= 10000)
            CHECK(all <= 12.0, "THE STAGE'S BAR: 100%% movers <= 12 ms at %d nodes (%.3f)", nodes, all);
    }

    // ---- THE SETTLE'S HOLE: A REPARENT UNDER AN ALREADY-VISITED PARENT -----------------------------------------------
    {
        Doc s;
        build(s, engine.get(), "settle", 1000, 20);   // 1,021 nodes: a rotation spans ~43 syncs at 24 visits a sync
        for (int i = 0; i < SceneMirror::kStaticSettleFrames * 6; ++i) s.mirror->sync();
        iris::SceneNodePtr x = s.cubes.back();         // in the last row: visited near the rotation's end
        iris::SceneNodePtr first = s.groups.front();   // the first row: visited in the rotation's first slice
        CHECK(x->isStaticInGraph(), "the settled document holds X static");
        x->setLocalPos(x->getLocalPos() + iris::Vec3(0.01f, 0, 0));   // rule 4: one write demotes it; a rotation is owed
        s.mirror->sync();
        CHECK(!x->isStaticInGraph(), "the write demoted X");
        // Until the rotation has run a few slices: the first row is passed, the last row not yet.
        int slices = 0, guard = 0;
        while (slices < 3 && guard++ < 2000) {
            s.mirror->sync();
            if (s.mirror->staticSettleVisits() > 0) ++slices;
        }
        CHECK(slices == 3 && s.mirror->staticSettleInProgress(), "a rotation is running (%d slices in)", slices);
        first->addChild(x);   // X, unvisited, reparented under the already-visited first row
        const quint64 repro0 = s.mirror->staticRepromotionCount();
        guard = 0;
        while (guard++ < 20000) {
            s.mirror->sync();
            if (!s.mirror->staticSettleInProgress() && s.mirror->staticRepromotionCount() > repro0) break;
        }
        std::printf("    the settle after the reparent: %llu re-derivations, X static %d\n",
                    (unsigned long long)(s.mirror->staticRepromotionCount() - repro0), int(x->isStaticInGraph()));
        CHECK(!s.mirror->staticSettleInProgress(), "the settle ended");
        CHECK(x->isStaticInGraph(), "X — reparented under a parent the running rotation had passed — is static again, "
              "as the one-shot says: its write owed the next rotation");
        s.doc->getRootNode()->applyStaticDefaults();
        CHECK(x->isStaticInGraph(), "...and the one-shot agrees");
    }

    view->setScene(nullptr);
    engine->destroyView(view);
    std::printf(failures ? "mirror.extract_movers: FAILED (%d)\n" : "mirror.extract_movers: PASS\n", failures);
    return failures ? 1 : 0;
}
