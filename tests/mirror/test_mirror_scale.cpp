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
//   4. A still sync BUILDS NO MATERIAL DESCRIPTION (SceneMirror::
//      materialBuildCount). This is the sharpest of the four and the one that
//      fails first. (An earlier draft counted heap allocations through a global
//      operator new; under ASan that fights the sanitizer's own interceptors and
//      crashed on the first free. The mirror's own counter says the same thing
//      and says it about the mirror rather than about the process.)
//
// ...and a FIFTH, which is what earns the memo the right to exist at all:
//
//   5. EVERY AUTHORED PROPERTY OF A MATERIAL MOVES ITS FINGERPRINT. The memo is
//      keyed on a hash of the document fields the conversion reads, and the
//      argument for that over a revision counter (see MaterialSync in
//      scenemirror.h) is only sound if a field the conversion reads can never be
//      missing from the hash. So the suite walks PbrMaterial's OWN property rows
//      — the material's authoring surface — writes a different value into each
//      one through setValue, and asserts the next sync rebuilds that material's
//      description. A field added to the material and forgotten in the
//      fingerprint fails here, which is the whole point.
#include <QColor>
#include <QDir>
#include <QImage>
#include <QGuiApplication>
#include <QStringList>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <memory>
#include <vector>
#include <functional>
#include <string>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/core/properties/property.h"
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
    // THE TIMED ARMS MEASURE THE WALK, which is what this suite's header says
    // and what its four shapes are about. The amortised VERIFIER is a separate,
    // deliberately-sized background pass (DIRTY_SET_MIRROR_SPEC §3.8) — it
    // re-reads a fixed number of nodes and materials a sync WHATEVER the scene
    // does, so leaving it on would put a constant in every ratio below and,
    // with a material per primitive, would make shape 3 measure the verifier's
    // sampling rate rather than the memo. It has its own arm at the end.
    a.mirror->setVerifierBudget(0);
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

    // ---- shape 0: THE WALK ACTUALLY HAPPENED -------------------------------
    // Every ratio below is a division, and a mirror that adopted NOTHING makes
    // all of them pass: stillMs and firstMs both collapse towards zero, and
    // materialBuilds is 0 because nothing was ever looked at. So the node
    // counts and the adopting cost are pinned FIRST, and the suite refuses to
    // report on a walk that did not take place.
    CHECK(a.visited == 1020, "walk: the 1000-cube lattice mirrored 1020 nodes");
    CHECK(b.visited == 2020, "walk: the 2000-cube lattice mirrored 2020 nodes");
    CHECK(c.visited == 2020, "walk: the shared-material lattice mirrored 2020 nodes");
    CHECK(a.firstMs > 0.0 && b.firstMs > 0.0 && c.firstMs > 0.0,
          "walk: every adopting sync took measurable time");
    CHECK(a.stillMs() > 0.0 && b.stillMs() > 0.0 && c.stillMs() > 0.0,
          "walk: every still sync took measurable time");
    if (failures) {   // the ratios below are meaningless without the above
        std::printf("FAILURES (the walk itself)\n");
        return 1;
    }

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

    // ---- shape 5: every authored property moves the FINGERPRINT ------------
    //
    // THE CASE THE MEMO'S WHOLE ARGUMENT RESTS ON. `MaterialSync` is reused
    // across frames whenever a hash of the material's document fields has not
    // moved, and the reason that was chosen over a revision counter is that a
    // counter is only as good as the writer that remembers to bump it. The
    // hash's own failure mode is narrower but real: a field the conversion
    // READS that nobody remembered to HASH. This is what catches it.
    //
    // The subject is PbrMaterial's own property rows — the material's authoring
    // surface, the list the panel and `material.set` are generated from — so a
    // row added to the material and forgotten in materialFingerprint fails
    // here without anyone having to think of it.
    {
        auto probeDoc = iris::Scene::create();
        Scene *probeTarget = engine->createScene("fingerprint");
        auto probeMesh = iris::MeshNode::create();
        probeMesh->setMesh(iris::Mesh::loadMesh(":assets/models/cube.obj"));
        auto probeMat = iris::PbrMaterial::create();
        probeMesh->setMaterial(probeMat);
        probeDoc->getRootNode()->addChild(probeMesh);
        SceneMirror probe(probeTarget);
        probe.setSource(probeDoc);
        probe.sync();
        probe.sync();
        CHECK(probe.materialBuildCount() == 0, "fingerprint: the probe material settles first");

        // A REAL IMAGE ON DISK for the texture rows. A missing file loads as a
        // NULL texture and `set*Map(null)` clears the slot (PbrMaterial::
        // loadTexture), so a bogus path on an empty slot is a genuine no-op —
        // the memo is RIGHT to stand, and a probe that used one would be
        // testing nothing. (It is also how the first draft of this case read as
        // eleven fingerprint misses that were not there.)
        const QString probePng = QDir::current().filePath(QStringLiteral("fingerprint-probe.png"));
        {
            QImage img(4, 4, QImage::Format_RGBA8888);
            img.fill(QColor(200, 80, 40));
            CHECK(img.save(probePng), "fingerprint: a real 4x4 texture is on disk for the map rows");
        }

        // Rows the ENGINE BOUNDARY genuinely does not read, named one by one so
        // that adding a row the mirror ignores is a decision and not a
        // shrug. `name` is the material's label; the two custom-piece paths are
        // bound once at material creation (HLMS_ADOPTION P5) and deliberately
        // not re-pushed per frame.
        const QStringList notPushedPerFrame = {
            QStringLiteral("name"),
            QStringLiteral("customPiecePixel"), QStringLiteral("customPieceVertex"),
        };

        // PbrMaterial's rows are OWNED by the material (Material::properties,
        // built at construction and rewritten by setValue) — copied by pointer
        // here and never deleted.
        const QList<iris::Property *> rows = probeMat->properties;
        int covered = 0, skipped = 0, missed = 0;
        for (iris::Property *row : rows) {
            if (!row) continue;
            const QString name = row->name;
            const QVariant before = row->getValue();
            // A DIFFERENT value for each type. Nothing here has to be sensible
            // — it has to be different, and it has to arrive through the
            // material's own setValue, which is the funnel every panel, script
            // and reader writes through.
            QVariant after;
            switch (row->type) {
            case iris::PropertyType::Bool:  after = !before.toBool(); break;
            case iris::PropertyType::Int:
            case iris::PropertyType::List:  after = before.toInt() + 1; break;
            case iris::PropertyType::Float: after = before.toFloat() + 0.371f; break;
            case iris::PropertyType::Color: {
                const QColor c = before.value<QColor>();
                after = QColor(c.red() ^ 0x5A, c.green() ^ 0x3C, c.blue() ^ 0x27, 255);
                break;
            }
            case iris::PropertyType::Texture:
            case iris::PropertyType::File:
                after = probePng;
                break;
            default: after = QVariant(); break;
            }
            if (!after.isValid()) { ++skipped; continue; }

            probeMat->setValue(name, after);
            probe.sync();
            const bool moved = probe.materialBuildCount() == 1;
            if (notPushedPerFrame.contains(name)) {
                ++skipped;
            } else if (moved) {
                ++covered;
            } else {
                ++missed;
                std::printf("    FINGERPRINT MISS: '%s' (type %d) changed and the memo stood\n",
                            qPrintable(name), int(row->type));
            }
            // Settle again so the next row starts from a clean count.
            probe.sync();
        }
        std::printf("    fingerprint coverage: %d of %d rows move it, %d deliberately not pushed\n",
                    covered, covered + missed, skipped);
        CHECK(covered > 20, "fingerprint: the material's authoring surface is really being walked");
        CHECK(missed == 0,
              "fingerprint: EVERY authored property of a material moves its fingerprint");
        probe.setSource(iris::ScenePtr());
    }

    // ---- SCENE_STATIC: the settle half ------------------------------------
    // Rule 4 demotes a static subtree on the first transform write and nothing
    // used to put it back, so every prop a user nudged spent the rest of the
    // session in the renderer's per-frame transform and bounds passes. The
    // mirror re-derives the classification once the document goes quiet.
    {
        // ONE WARM-UP CYCLE FIRST, so the baseline is the steady state.
        // Binding a mirror MIGRATES the document's Ogre nodes into its scene
        // manager and they are reborn dynamic; the hints are replayed, and the
        // count right after that is a replay rather than the resolution rule's
        // own answer. A nudge-and-settle puts the tree where the rule says it
        // belongs, and everything below compares against THAT.
        auto nudge = [&](int count) {
            int done = 0;
            for (const auto &n : b.keep) {
                if (n->getSceneNodeType() != iris::SceneNodeType::Mesh) continue;
                n->setLocalPos(n->getLocalPos() + iris::Vec3(0.001f, 0, 0));
                if (++done >= count) break;
            }
        };
        auto settle = [&]() {
            for (quint32 i = 0; i < SceneMirror::kStaticSettleFrames + 2; ++i) b.mirror->sync();
        };
        nudge(1); b.mirror->sync(); settle();
        const quint64 settled0 = iris::graph::staticNodeCount();

        // THE SETTLE IS GATED ON A REAL DEMOTION (lead review F4). A quiet
        // spell that follows no demotion — a camera move, an undo, a scene open
        // — must not buy a whole-tree re-derivation, so a second settle with
        // nothing dragged in between costs nothing at all.
        const quint64 idleRepro = b.mirror->staticRepromotionCount();
        settle();
        CHECK(b.mirror->staticRepromotionCount() == idleRepro,
              "settle: a quiet spell with NO demotion re-derives nothing");
        std::printf("    staticNodes before the nudge: %llu\n",
                    (unsigned long long)settled0);
        CHECK(settled0 > 0, "the lattices' props are SCENE_STATIC to begin with");

        // Nudge 30 props — a user dragging things around.
        nudge(30);
        b.mirror->sync();
        const quint64 afterNudge = iris::graph::staticNodeCount();
        std::printf("    staticNodes after 30 nudges:  %llu\n",
                    (unsigned long long)afterNudge);
        CHECK(afterNudge < settled0, "a nudge DEMOTES the nudged subtrees (rule 4)");

        // Now settle: syncs with no transform write anywhere.
        const quint64 repro0 = b.mirror->staticRepromotionCount();
        settle();
        const quint64 settled1 = iris::graph::staticNodeCount();
        std::printf("    staticNodes after the settle: %llu  (re-promotions %llu)\n",
                    (unsigned long long)settled1,
                    (unsigned long long)(b.mirror->staticRepromotionCount() - repro0));
        CHECK(b.mirror->staticRepromotionCount() == repro0 + 1,
              "the settle re-derived the classification exactly ONCE");
        CHECK(settled1 == settled0, "…and every nudged prop is static again");

        // A SECOND settle must not run: nothing has written a transform since.
        settle();
        CHECK(b.mirror->staticRepromotionCount() == repro0 + 1,
              "a scene that never moves again re-derives nothing further");
    }

    // ---- THE DIRTY SET (SPECS/DIRTY_SET_MIRROR_SPEC.md, owner option A) ----
    //
    // The four shapes above are about what a still frame costs PER NODE. This
    // is about how many nodes a frame looks at at all — which, on a still
    // frame, is none. Counts, not milliseconds: they are exact.
    {
        std::printf("  -- the dirty set --\n");
        // A STILL FRAME LOOKS AT NOTHING.
        b.mirror->sync();
        CHECK(b.mirror->visitedCount() == 0, "a STILL sync visits ZERO nodes");
        CHECK(b.mirror->dirtyNodeCount() == 0, "...because the document reported ZERO changes");
        CHECK(std::string(b.mirror->walkMode()) == "dirty", "...in dirty mode");
        CHECK(b.mirror->evictedNodeCount() == 0, "...and released nothing");

        // Find a leaf mesh and its group, and a light-free handle on both.
        iris::SceneNodePtr leaf, group;
        for (const auto &n : b.keep) {
            if (!leaf && n->getSceneNodeType() == iris::SceneNodeType::Mesh) leaf = n;
            if (!group && n->getSceneNodeType() == iris::SceneNodeType::Empty) group = n;
            if (leaf && group) break;
        }
        CHECK(!!leaf && !!group, "the lattice has a mesh leaf and a group to drive");

        // ONE EDIT KIND AT A TIME: the visited count is the MARKED count.
        struct Edit { const char *what; std::function<void()> run; int expectVisited; };
        const int subtree = 1 + int(b.keep.size() - 20) / 20;   // a row group + its cubes
        std::vector<Edit> edits = {
            { "one node moved",      [&] { leaf->setLocalPos(leaf->getLocalPos() + iris::Vec3(0.01f, 0, 0)); }, 1 },
            { "one node hidden",     [&] { leaf->setVisible(false); }, 1 },
            { "one node shown",      [&] { leaf->setVisible(true); }, 1 },
            { "one flag written",    [&] { leaf->setShadowCastingEnabled(!leaf->getShadowCastingEnabled()); }, 1 },
            { "one material edited", [&] {
                  auto *mn = static_cast<iris::MeshNode *>(leaf.data());
                  auto pbr = mn->getMaterial().dynamicCast<iris::PbrMaterial>();
                  if (pbr) pbr->setValue(QStringLiteral("roughness"), 0.31f);
              }, 1 },
        };
        for (const Edit &e : edits) {
            b.mirror->sync();                       // settle
            e.run();
            b.mirror->sync();
            const int visited = b.mirror->visitedCount();
            std::printf("    %-22s dirty %4llu  visited %4d\n", e.what,
                        (unsigned long long)b.mirror->dirtyNodeCount(), visited);
            CHECK(visited == e.expectVisited, e.what);
            // THE ORACLE (§7.1): the full walk straight after the dirty sync
            // must push NOTHING. Anything it pushes is a change the document
            // failed to report.
            const quint64 late = b.mirror->verifyAgainstFullWalk();
            if (late) std::printf("    ORACLE: %llu late pushes after '%s'\n",
                                  (unsigned long long)late, e.what);
            CHECK(late == 0, "...and the full walk after it pushes nothing");
        }

        // A SUBTREE HIDE costs its subtree and nothing else — the change's own
        // price, and the F6 contract (the mirror is the sole pusher of
        // effective visibility, so every descendant MUST be marked).
        b.mirror->sync();
        group->setVisible(false);
        b.mirror->sync();
        const int hid = b.mirror->visitedCount();
        std::printf("    subtree hidden         visited %4d (group of ~%d)\n", hid, subtree);
        CHECK(hid > 1 && hid <= b.visited,
              "hiding a group visits its SUBTREE — every descendant, and nothing else");
        CHECK(b.mirror->verifyAgainstFullWalk() == 0,
              "...and the full walk after it pushes nothing");
        group->setVisible(true);
        b.mirror->sync();
        CHECK(b.mirror->verifyAgainstFullWalk() == 0, "...showing it again likewise");

        // NOTHING WAS MISSED, ever, on this whole suite.
        CHECK(b.mirror->verifierCatchCount() == 0,
              "the change list missed NOTHING on the whole lattice");

        // ---- THE MOVERS ARM (§6 M3) ---------------------------------------
        //
        // The list degrades to the walk and never past it: with every node
        // moving, the dirty sync costs what the old full walk did; with 12 %
        // moving it costs about 12 % of it. Ratios, and against a measured
        // FULL-walk arm rather than a remembered number.
        auto moveSome = [&](int count) {
            int done = 0;
            for (const auto &n : b.keep) {
                if (n->getSceneNodeType() != iris::SceneNodeType::Mesh) continue;
                n->setLocalPos(n->getLocalPos() + iris::Vec3(0.001f, 0, 0));
                if (++done >= count) break;
            }
            return done;
        };
        auto timedSync = [&]() {
            using clock = std::chrono::steady_clock;
            auto t0 = clock::now();
            b.mirror->sync();
            return std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        };
        std::vector<double> fullMs, allMs, someMs, stillMs;
        const int meshCount = int(b.keep.size()) - 20;
        for (int i = 0; i < 9; ++i) {
            b.mirror->requestFullWalk();  fullMs.push_back(timedSync());
            moveSome(meshCount);          allMs.push_back(timedSync());
            moveSome(meshCount / 8);      someMs.push_back(timedSync());
            stillMs.push_back(timedSync());
        }
        auto med = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size()/2]; };
        const double full = med(fullMs), all = med(allMs), some = med(someMs), still = med(stillMs);
        std::printf("    full walk %7.3f ms | 100%% movers %7.3f | 12%% movers %7.3f | still %7.3f\n",
                    full, all, some, still);
        // MEASURED 1.38x (Debug + ASan, 2020 nodes, 2026-09-13). The dirty path
        // pays two things the recursion gets for free: a list entry per node,
        // and a memo probe for the parent's answers (it reads them from the
        // DOCUMENT so that the order of the change list cannot matter, where
        // the walk threads them down). That is the honest price of the worst
        // case — literally everything moving — and the gate is that it stays a
        // CONSTANT FACTOR rather than becoming a different shape.
        CHECK(all < full * 1.5,
              "100% movers cost about what the full walk cost — the list degrades to it, never past");
        CHECK(some < all * 0.45, "12% movers cost a fraction of that");
        CHECK(still < full * 0.08,
              "a STILL frame costs under a tenth of the walk it replaced");

        // ---- WHAT THE VERIFIER COSTS --------------------------------------
        //
        // It is always on in the app, so its price is part of a still frame's.
        // Bounded by its own two budgets and by nothing about the scene.
        b.mirror->setVerifierBudget(64);
        b.mirror->setVerifierMaterialBudget(8);
        std::vector<double> guarded;
        for (int i = 0; i < 9; ++i) guarded.push_back(timedSync());
        const double withVerifier = med(guarded);
        std::printf("    still + verifier %7.3f ms (bare %7.3f, budget 64 nodes / 8 materials)\n",
                    withVerifier, still);
        CHECK(withVerifier < full * 0.35,
              "the always-on verifier keeps a still frame well under the walk it replaced");
        CHECK(b.mirror->verifierVisitCount() == 64, "...and it really re-read its 64 nodes");
        CHECK(b.mirror->verifierCatchCount() == 0, "...and found nothing behind");
        b.mirror->setVerifierBudget(0);
    }

    std::printf("%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
