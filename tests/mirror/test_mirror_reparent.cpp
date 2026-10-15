// mirror.reparent_local — A REPARENT COSTS THE FRAGMENT, NEVER THE SCENE (ENGINE-UPGRADE-1 stage 2). A law in counts:
// the same 500-node fragment, moved between two hosts inside a 1,000-node and a 10,000-node lattice (the
// scenegraph bench's shape: an 8-ary tree, mesh leaves, one cube, one material), must cost the SAME counts at both
// scales — the fragment and its two hosts, never O(scene).
//
//   (A) THE EDITOR'S PATH, hostB->addChild(fragment, false) (a same-scene move): sync() visits exactly 502 nodes
//       (500 + the two hosts), evicts nothing, builds no material, walks the dirty list, owes no settle, and makes
//       the same number of engine pushes at both scales.
//   (B) THE DELETE + RE-ADD PATH, removeChild then addChild (the bench's c.reparent_500): sync() visits 502, evicts
//       500, mirrors N + 502 nodes after; the engine destroys and re-creates exactly 500 Items and asks the GI side
//       for AT MOST ONE invalidation for the whole sync, and the mirror runs no O(every entry) reclaim sweep (the
//       resources released are re-taken in the same sync — stage 2b).
//   (C) THE COMMAND'S REAL CALL, addChild(fragment, keepTransform=true): visits 502, the fragment is still static
//       after, no settle owed, and NOT ONE class switch — the keepTransform write re-expresses the pose in the new
//       parent's space (graph::ReexpressScope), it moves nothing, so it changes no staticness.
//
// RED ON THE BASE (82d38ccb5), measured: (A) and (C) visited 563,752 at 1k and 4,501,252 at 10k — every re-adopted
// node's attach re-asserted its material's shading model, setShadingModel's idempotent success read as a switch, and
// every other user of the material was queued (SceneMirror::visitNode, shadingModelOf now asked first); (B) asked for
// 1,000 GI invalidations (one per released node, one per attach) and its removeChild scanned the whole mesh registry
// once per node below the fragment's root (iris::Scene::removeNode recursed under SceneNode::removeFromScene's own
// recursion); (D) — a static fragment moved without keepTransform drew at its OLD world pose (Ogre refreshes a static
// node only on a static dirty; attach issued none — stage 2b's read); (C) cost 1,000 class switches (rule 4 demoted the 500-node fragment on the keepTransform write and the
// hint re-promoted it — stage 2b). Two frames per scale (the drawn-pose case); the mirror's sync is the subject.
#include <QGuiApplication>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "../support/testmesh.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } std::fflush(stdout); } while (0)

namespace {

constexpr int kFragment = 500;
constexpr int kExpectedVisits = kFragment + 2;   // the fragment and its two hosts

iris::MeshPtr gCube;
iris::MaterialPtr gMaterial;

struct World {
    Scene *target = nullptr;
    iris::ScenePtr doc;
    std::unique_ptr<SceneMirror> mirror;
    std::vector<iris::SceneNodePtr> keep;
    iris::SceneNodePtr hostA, hostB, fragment;
    std::vector<iris::SceneNodePtr> fragmentNodes;
    int n = 0;
};

iris::SceneNodePtr meshNode(float scale)
{
    auto mn = iris::MeshNode::create();
    mn->setMesh(gCube);
    mn->setMaterial(gMaterial);
    mn->setLocalScale(iris::Vec3(scale, scale, scale));
    return mn;
}

/// The bench's lattice (an 8-ary tree; interior groups, mesh leaves) and its (c) fixture under hostA.
void build(World &w, Engine *engine, int n)
{
    w.n = n;
    w.target = engine->createScene(("reparent" + std::to_string(n)).c_str());
    w.doc = iris::Scene::create();
    const int branch = 8;
    std::vector<iris::SceneNodePtr> all(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const bool interior = (branch * (i + 1)) < n;
        iris::SceneNodePtr node = interior ? iris::SceneNode::create() : meshNode(0.5f);
        node->setLocalPos(iris::Vec3(float(i % 20), float((i / 20) % 20), float(i / 400)));
        const int parent = (i / branch) - 1;
        if (parent >= 0) all[size_t(parent)]->addChild(node, false);
        else             w.doc->getRootNode()->addChild(node, false);
        all[size_t(i)] = node;
    }
    w.keep = all;
    w.hostA = iris::SceneNode::create();
    w.hostB = iris::SceneNode::create();
    w.hostA->setLocalPos(iris::Vec3(-5, 0, 0));
    w.hostB->setLocalPos(iris::Vec3(5, 0, 0));
    w.doc->getRootNode()->addChild(w.hostA, false);
    w.doc->getRootNode()->addChild(w.hostB, false);
    for (int i = 0; i < kFragment; ++i) {
        auto mn = meshNode(0.4f);
        mn->setLocalPos(iris::Vec3(float(i % 10) * 0.6f, float((i / 10) % 10) * 0.6f, 0.0f));
        if (i > 0) w.fragmentNodes[size_t((i - 1) / branch)]->addChild(mn, false);
        w.fragmentNodes.push_back(mn);
    }
    w.fragment = w.fragmentNodes.front();
    w.hostA->addChild(w.fragment, false);
    w.doc->getRootNode()->applyStaticDefaults();   // the load's static pass, as the app's reader runs it

    w.mirror.reset(new SceneMirror(w.target));
    w.mirror->setVerifierBudget(0);                // the amortised verifier is its own subject
    w.mirror->setSource(w.doc);
    // THE SETTLE DRAINED FIRST: the bind owes one whole-document rotation (runStaticSettle); a reparent is
    // measured on a quiet mirror.
    int syncs = 0;
    for (; syncs < 4000; ++syncs) {
        w.mirror->sync();
        if (syncs > 2 && !w.mirror->staticSettleInProgress()) break;
    }
    CHECK(!w.mirror->staticSettleInProgress(), "[%d] the settle drained before the reparents (%d syncs)", n, syncs);
}

struct Counts {
    int visited = 0;
    quint64 evicted = 0, materials = 0, pushes = 0, mirrored = 0;
    std::string walk;
    bool settleOwed = false;
    unsigned long long itemsDestroyed = 0, itemsCreated = 0, giInvalidations = 0, staticSwitches = 0;
    quint64 reclaimSweeps = 0;
};

template <typename Edit>
Counts measure(World &w, Edit edit)
{
    Counts c;
    const quint64 pushes0 = w.mirror->visitPushCount();
    const SceneChangeStats cs0 = w.target->changeStats();
    const unsigned long long sw0 = iris::graph::staticSwitchCount();
    const quint64 sweeps0 = w.mirror->reclaimSweepCount();
    edit();
    c.visited = w.mirror->sync();
    const SceneChangeStats cs1 = w.target->changeStats();
    c.staticSwitches = iris::graph::staticSwitchCount() - sw0;
    c.reclaimSweeps = w.mirror->reclaimSweepCount() - sweeps0;
    c.evicted = w.mirror->evictedNodeCount();
    c.materials = w.mirror->materialBuildCount();
    c.pushes = w.mirror->visitPushCount() - pushes0;
    c.mirrored = w.mirror->mirroredNodeCount();
    c.walk = w.mirror->walkMode();
    c.settleOwed = w.mirror->staticSettleInProgress();
    c.itemsDestroyed = cs1.itemsDestroyed - cs0.itemsDestroyed;
    c.itemsCreated = cs1.itemsCreated - cs0.itemsCreated;
    c.giInvalidations = cs1.giInvalidations - cs0.giInvalidations;
    std::printf("    [%5d] visited=%d evicted=%llu materials=%llu pushes=%llu mirrored=%llu walk=%s settleOwed=%d "
                "itemsDestroyed=%llu itemsCreated=%llu giInvalidations=%llu staticSwitches=%llu reclaimSweeps=%llu\n",
                w.n, c.visited, (unsigned long long)c.evicted, (unsigned long long)c.materials,
                (unsigned long long)c.pushes, (unsigned long long)c.mirrored, c.walk.c_str(), int(c.settleOwed),
                c.itemsDestroyed, c.itemsCreated, c.giInvalidations, c.staticSwitches,
                (unsigned long long)c.reclaimSweeps);
    std::fflush(stdout);
    return c;
}

bool fragmentStatic(const World &w)
{
    for (const iris::SceneNodePtr &n : w.fragmentNodes)
        if (!n->isStaticInGraph()) return false;
    return true;
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    std::printf("== mirror.reparent_local: a reparent costs the fragment, never the scene\n");
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test_mirror_reparent-ogre.log"), err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    // A VIEW BEFORE THE FIRST DOCUMENT NODE: the graph's staging scene manager needs the backend's window.
    View *view = engine->createOffscreenView("reparent", 32, 32, Colour(0, 0, 0));
    CHECK(view != nullptr, "offscreen view (the graph device)");
    if (!view) return 1;
    gCube = testmesh::load(":assets/models/cube.obj");
    gMaterial = iris::PbrMaterial::create();

    const int scales[2] = { 1000, 10000 };
    Counts a[2], b[2], c[2];
    for (int k = 0; k < 2; ++k) {
        World w;
        build(w, engine.get(), scales[k]);
        view->setScene(w.target);
        enginetest::frames(engine, 1);
        // (A) hostA -> hostB, a same-scene move
        a[k] = measure(w, [&] { w.hostB->addChild(w.fragment, false); });
        // (D) ...AND IT DRAWS WHERE IT NOW IS (stage 2b's read, item a): the static fragment kept its local and changed
        // its parent, so its world moved; one frame later the pose the renderer DREW (the static pass's cached
        // derived transform) is the new world pose, for the root and for a leaf deep in the fragment.
        enginetest::frames(engine, 1);
        for (const iris::SceneNodePtr &n : { w.fragment, w.fragmentNodes.back() }) {
            const NodeId id = w.mirror->engineNode(n.data());
            Vec3 drawn, now; Quat qd, qn;
            const bool ok = id && w.target->nodeWorldPoseDrawn(id, drawn, qd) && w.target->nodeWorldPose(id, now, qn);
            const float err = std::fabs(drawn.x - now.x) + std::fabs(drawn.y - now.y) + std::fabs(drawn.z - now.z);
            CHECK(ok && n->isStaticInGraph() && err < 1e-4f,
                  "(D %d) the static %s draws at its new world pose a frame after the move (|drawn - now| = %g)",
                  scales[k], n == w.fragment ? "fragment root" : "leaf", double(err));
        }
        CHECK(a[k].visited == kExpectedVisits, "(A %d) the editor's move visits exactly %d nodes (read %d)",
              scales[k], kExpectedVisits, a[k].visited);
        CHECK(a[k].evicted == 0 && a[k].materials == 0 && a[k].walk == "dirty" && !a[k].settleOwed,
              "(A %d) no eviction, no material build, the dirty walk, no settle owed", scales[k]);
        // (B) hostB -> hostA, removeChild + addChild: the fragment leaves the document and comes back
        b[k] = measure(w, [&] { w.hostB->removeChild(w.fragment); w.hostA->addChild(w.fragment, false); });
        CHECK(b[k].visited == kExpectedVisits && b[k].evicted == quint64(kFragment),
              "(B %d) delete + re-add visits %d and evicts %d (read %d / %llu)", scales[k], kExpectedVisits, kFragment,
              b[k].visited, (unsigned long long)b[k].evicted);
        CHECK(b[k].mirrored == quint64(scales[k] + kExpectedVisits), "(B %d) the mirror holds N + %d entries after (%llu)",
              scales[k], kExpectedVisits, (unsigned long long)b[k].mirrored);
        CHECK(b[k].itemsDestroyed == quint64(kFragment) && b[k].itemsCreated == quint64(kFragment),
              "(B %d) the engine destroys and re-creates exactly %d Items (%llu / %llu)", scales[k], kFragment,
              b[k].itemsDestroyed, b[k].itemsCreated);
        CHECK(b[k].giInvalidations <= 1, "(B %d) ONE GI invalidation for the whole sync, at most (read %llu)",
              scales[k], b[k].giInvalidations);
        CHECK(b[k].reclaimSweeps == 0, "(B %d) no O(scene) reclaim sweep: the same mesh and material dropped and re-taken "
              "(read %llu sweeps)", scales[k], (unsigned long long)b[k].reclaimSweeps);
        // (C) hostA -> hostB with keepTransform: the reparent command's own call
        c[k] = measure(w, [&] { w.hostB->addChild(w.fragment, true); });
        CHECK(c[k].visited == kExpectedVisits, "(C %d) the command's move visits exactly %d nodes (read %d)",
              scales[k], kExpectedVisits, c[k].visited);
        CHECK(fragmentStatic(w) && !c[k].settleOwed, "(C %d) the fragment is static again after, no settle owed",
              scales[k]);
        CHECK(c[k].staticSwitches == 0, "(C %d) a move changes poses, never staticness: 0 class switches (read %llu)",
              scales[k], c[k].staticSwitches);
        w.mirror->setSource(iris::ScenePtr());
        view->setScene(nullptr);
        engine->destroyScene(w.target);
    }
    CHECK(a[0].pushes == a[1].pushes, "(A) the same engine pushes at 1k and 10k (%llu / %llu)",
          (unsigned long long)a[0].pushes, (unsigned long long)a[1].pushes);
    CHECK(b[0].visited == b[1].visited && b[0].evicted == b[1].evicted && b[0].itemsCreated == b[1].itemsCreated &&
          b[0].giInvalidations == b[1].giInvalidations, "(B) the same counts at 1k and 10k");
    CHECK(c[0].staticSwitches == c[1].staticSwitches, "(C) the same static switches at 1k and 10k (%llu / %llu)",
          c[0].staticSwitches, c[1].staticSwitches);
    gCube.reset();
    gMaterial.reset();
    std::printf("mirror.reparent_local: %s\n", gFailures ? "FAILED" : "PASSED");
    return gFailures ? 1 : 0;
}
