// atom.dag_chain_validate — DAG-CHAIN-VALIDATE-1 (ATOM-SHADOWS-1's F1).
//
// ATOM-SHADOWS-1 fed a BAKED MeshData with its DAG kept and its chain stripped
// (lodIndices / lodErrors / lodBounds cleared) through Scene::createMesh and Ogre's
// caster queue crashed (RenderQueue::addRenderable at RQ 10, a VAO list indexed past
// its end). The brief's rule — "a DAG deeper than its chain is refused" — would
// refuse the PRODUCT's own meshes: a bake's DAG is routinely deeper than its chain
// (printed below for every subject). What indexes an Item's VAO list with a level is
// a surface CARD (`MeshCardDesc::lodLevel`, spent on the Item by the capture), so
// createMesh refuses a card that names a level past the chain. This suite:
//   A. every mesh the product's bake makes (the chain, the DAG and the cards) is
//      ACCEPTED — its DAG depth, chain length and deepest card level printed;
//   B. the hand-built input — the DAG kept, the chain stripped — is accepted when it
//      carries no card and DRAWS: 30 frames through the view's shadow node (a sun, a
//      spot, a point lamp) with the split on, no crash;
//   C. the same input with the bake's cards kept is REFUSED, the reason naming the card.
#include "cluster_fixtures.h"

#include <QCoreApplication>
#include <algorithm>
#include <cstdio>
#include <string>

#include "jahshaka/engine/Engine.h"
#include "irisgl/import/meshbake.h"
#include "irisgl/mirror/scenemirror.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-atom-dag-chain-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    // THE ORDER THE ENGINE REQUIRES: a view before its scene (trap 1).
    View *view = engine->createOffscreenView("dag-chain", 320, 180, Colour(0, 0, 0));
    Scene *scene = engine->createScene("dag-chain");
    if (!view || !scene) { std::printf("FAIL: view/scene: %s\n", engine->lastError().c_str()); return 1; }
    view->setScene(scene);
    view->setShadows(true);
    scene->setAmbient(Colour(0.2f, 0.2f, 0.2f), Colour(0.1f, 0.1f, 0.1f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -0.8f, -0.3f), 3.0f);
    {
        const NodeId n = scene->createNode();
        scene->setNodeTransform(n, Vec3(0.0f, 3.0f, 0.0f), Quat(), Vec3(1, 1, 1));
        LightDesc l; l.type = LightType::Spot; l.intensity = 30.0f; l.range = 20.0f; l.spotAngleDegrees = 50.0f;
        scene->setLight(n, l);
        const NodeId p = scene->createNode();
        scene->setNodeTransform(p, Vec3(1.5f, 1.5f, 1.0f), Quat(), Vec3(1, 1, 1));
        LightDesc pl; pl.type = LightType::Point; pl.intensity = 20.0f; pl.range = 10.0f;
        scene->setLight(p, pl);
    }
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.2f, 3.5f), Vec3(0, 0.3f, 0));
    PbrParams pp; pp.albedo = Colour(0.7f, 0.5f, 0.4f); pp.roughness = 0.6f;
    const MaterialId mat = scene->createPbrMaterial(pp);

    std::vector<clusterfix::Named> meshes =
        clusterfix::shippedMeshes(JAHSHAKA_TEST_SOURCE_DIR, CLUSTER_FIXTURE_DIR, false);
    meshes.insert(meshes.begin(), { "sphere-20k", clusterfix::uvSphere() });
    int withDag = 0, deeperThanChain = 0, drawn = 0, refused = 0, carded = 0;
    for (const clusterfix::Named &n : meshes) {
        clusterfix::Fixture f;
        if (!clusterfix::bake(f, n.mesh, n.name) || f.data.clusterGroups.empty()) continue;
        ++withDag;
        // THE PRODUCT'S CARDS on the same mesh (the bake's own generator).
        iris::MeshBake::buildCards(f.mesh, 16);
        MeshData full;
        if (!SceneMirror::toMeshData(f.mesh.data(), full)) continue;
        int maxDepth = 0, maxCard = -1;
        for (const MeshClusterGroup &g : full.clusterGroups) maxDepth = std::max(maxDepth, g.depth);
        for (const MeshCardDesc &c : full.cards) maxCard = std::max(maxCard, int(c.lodLevel));
        if (size_t(maxDepth) > full.lodIndices.size()) ++deeperThanChain;
        std::printf("    %-24s %8zu tris: DAG %d deep, chain %zu levels, %zu cards (deepest level %d)\n", n.name.c_str(),
                    f.triangles, maxDepth, full.lodIndices.size() + 1, full.cards.size(), maxCard);
        // A. THE PRODUCT'S BAKE IS ACCEPTED.
        const MeshId ok = scene->createMesh(full);
        CHECK(ok != 0, "%s: the baked mesh (chain, DAG, cards) is accepted%s%s", n.name.c_str(), ok ? "" : ": ",
              ok ? "" : engine->lastError().c_str());
        if (ok) scene->destroyMesh(ok);
        // B. THE HAND-BUILT INPUT, no cards: the DAG kept, the chain stripped — draws.
        MeshData stripped = full;
        stripped.lodIndices.clear();
        stripped.lodErrors.clear();
        stripped.lodBounds.clear();
        const std::vector<MeshCardDesc> cards = stripped.cards;
        stripped.cards.clear();
        const MeshId bare = scene->createMesh(stripped);
        CHECK(bare != 0, "%s: its DAG with the chain stripped and no card is accepted", n.name.c_str());
        if (bare) {
            const NodeId node = scene->createNode();
            const float sc = 1.2f / std::max(0.01f, f.extent);
            scene->setNodeTransform(node, Vec3(0, 0.6f, 0), Quat(), Vec3(sc, sc, sc));
            CHECK(scene->attachMesh(node, bare, mat), "%s: ...and attaches", n.name.c_str());
            for (int i = 0; i < 30; ++i) engine->renderOneFrame();   // the shadow node's casters, the split
            ++drawn;
            scene->removeNode(node);
            engine->renderOneFrame();
            scene->destroyMesh(bare);
        }
        // C. ...WITH THE BAKE'S CARDS KEPT: a card names a level the chain no longer has.
        if (maxCard > 0) {
            ++carded;
            stripped.cards = cards;
            const MeshId bad = scene->createMesh(stripped);
            const std::string why = bad ? std::string() : engine->lastError();
            CHECK(bad == 0 && why.find("card") != std::string::npos,
                  "%s: its cards past the stripped chain are REFUSED (%s)", n.name.c_str(), why.c_str());
            if (bad) scene->destroyMesh(bad);
            else ++refused;
        }
    }
    // D. THE CHAIN THAT IS BUILT (the Fable read's (d)): a HAND-BUILT MeshData with coarser
    // levels but NO bounds keeps level 0 alone at upload (buildMeshV2's `accepted`), so a
    // card at level 1 would walk past the VAO list — refused; the same data WITH bounds
    // builds both levels and is accepted; and a DYNAMIC one keeps level 0 alone, refused.
    {
        MeshData hand;
        hand.positions = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
        hand.normals = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
        hand.indices = { 0, 1, 2, 0, 2, 3 };
        hand.lodIndices = { { 0, 1, 2 } };
        MeshCardDesc card;
        card.axis = 4; card.lodLevel = 1; card.halfU = card.halfV = 0.5f; card.halfDepth = 0.1f;
        hand.cards = { card };
        MeshId id = scene->createMesh(hand);
        std::string why = id ? std::string() : engine->lastError();
        CHECK(id == 0 && why.find("card") != std::string::npos,
              "hand-built: coarser levels with no bounds build level 0 alone; a level-1 card is REFUSED (%s)", why.c_str());
        if (id) scene->destroyMesh(id);
        hand.lodBounds = { 0.01f };
        id = scene->createMesh(hand);
        CHECK(id != 0, "hand-built: the same data WITH its bound builds two levels; the level-1 card is accepted (%s)",
              id ? "" : engine->lastError().c_str());
        if (id) scene->destroyMesh(id);
        hand.dynamic = true;
        id = scene->createMesh(hand);
        CHECK(id == 0, "hand-built: a DYNAMIC mesh builds level 0 alone; the level-1 card is refused");
        if (id) scene->destroyMesh(id);
    }
    std::printf("    %d meshes with a DAG, %d of them deeper than their chain; %d stripped inputs drawn, %d card refusals\n",
                withDag, deeperThanChain, drawn, refused);
    CHECK(withDag >= 3 && drawn == withDag, "every stripped DAG drew without a crash (%d of %d)", drawn, withDag);
    CHECK(carded >= 1 && refused == carded, "every carded stripped mesh was refused (%d of %d)", refused, carded);
    engine->destroyScene(scene);
    engine->destroyView(view);
    engine.reset();
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
