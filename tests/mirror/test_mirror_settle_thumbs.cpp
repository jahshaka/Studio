// mirror.settle_thumbnails — A SETTLE IS NOT STARVED BY ANOTHER DOCUMENT'S
// WRITES (MIRROR-SETTLE-1 fix round 1, lead read 1 item 2).
//
// The settle used to be gated on the PROCESS's transform-write counter and
// restarted on any SceneNode destruction anywhere: an Assets tile renders a
// thumbnail by building a throwaway document (EngineThumbnailRenderer —
// MeshNode::create + addChild per tile, the document dropped after), which is
// a write and a destruction every tile, so a 10k scene's settle never ended
// while thumbnails were generated. The settle now runs on THIS document's own
// writes and per-node quiet clocks.
//
// The proof, in COUNTS: a 10,000-node scene is demoted by one edit, then
// synced with a thumbnail rendered on EVERY sync. The settle must complete in
// kStaticSettleFrames + one rotation (ceil(nodes / budget)) + a small slack,
// and put back exactly the static count it had before the edit.
#include <QGuiApplication>
#include <QImage>

#include <cstdio>
#include <memory>
#include <vector>

#include "../support/testmesh.h"
#include "bridge/enginethumbnailrenderer.h"
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
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static size_t staticCount(const std::vector<iris::SceneNodePtr> &all)
{
    size_t n = 0;
    for (const auto &x : all) if (x->isStaticInGraph()) ++n;
    return n;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    testmesh::installShippedResolver();
    const auto cfg = enginetest::testEngineConfig("test_mirror_settle_thumbs-ogre.log");
    std::string err;
    std::shared_ptr<Engine> engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    View *view = engine->createOffscreenView("settle-thumbs", 64, 64, Colour(0, 0, 0));
    CHECK(view != nullptr, "offscreen view");
    if (!view) return 1;
    Scene *target = engine->createScene("settle-thumbs");
    view->setScene(target);

    auto loan = EngineThumbnailRenderer::borrow(engine, "mirror.settle_thumbnails");
    CHECK(bool(loan), "the thumbnail renderer can be borrowed");
    if (!loan) return 1;

    iris::MeshPtr cube = testmesh::load(":assets/models/cube.obj");
    iris::MaterialPtr material = iris::PbrMaterial::create();

    // THE SCENE: 10 districts x 20 blocks x 49 props = 9,800 props + 210 groups.
    iris::ScenePtr doc = iris::Scene::create();
    std::vector<iris::SceneNodePtr> all, blocks;
    for (int d = 0; d < 10; ++d) {
        auto district = iris::SceneNode::create();
        district->setLocalPos(iris::Vec3(float(d) * 30.0f, 0, 0));
        doc->getRootNode()->addChild(district);
        all.push_back(district);
        for (int b = 0; b < 20; ++b) {
            auto block = iris::SceneNode::create();
            block->setLocalPos(iris::Vec3(0, 0, float(b) * 10.0f));
            district->addChild(block);
            all.push_back(block);
            blocks.push_back(block);
            for (int p = 0; p < 49; ++p) {
                auto m = iris::MeshNode::create();
                m->setMesh(cube);
                m->setMaterial(material);
                m->setLocalPos(iris::Vec3(float(p % 7), 0, float(p / 7)));
                block->addChild(m);
                all.push_back(m);
            }
        }
    }
    doc->getRootNode()->applyStaticDefaults();
    SceneMirror mirror(target);
    mirror.setVerifierBudget(0);
    mirror.setSource(doc);
    mirror.sync();
    for (int g = 0; mirror.staticSettleInProgress() && g < 100000; ++g) mirror.sync();
    const size_t settled = staticCount(all);
    const size_t nodes = all.size() + 1;
    std::printf("    %zu document nodes, %zu static after the bind's settle, budget %u nodes/sync\n",
                nodes, settled, SceneMirror::kStaticSettleNodesPerSync);
    CHECK(settled > nodes / 2, "the bound scene settled static");

    // THE EDIT: every block moved once — rule 4 demotes all 9,800 props.
    for (auto &b : blocks) b->setLocalPos(b->getLocalPos() + iris::Vec3(0.25f, 0, 0));
    mirror.sync();
    const size_t demoted = staticCount(all);
    CHECK(demoted < settled / 10, "the edit demoted the scene");

    // THE STARVATION SCENARIO: a thumbnail on every sync until the settle ends.
    const quint64 r0 = mirror.staticRepromotionCount();
    int syncs = 0, tiles = 0, blankTiles = 0;
    for (; syncs < 100000; ++syncs) {
        QImage tile;
        {
            auto node = iris::MeshNode::create();
            node->setMesh(cube);
            node->setMaterial(material);
            tile = loan->renderNode(node, QSize(32, 32));
        }   // the tile's node and document die here: a write AND a destruction
        ++tiles;
        if (tile.isNull()) ++blankTiles;
        mirror.sync();
        if (!mirror.staticSettleInProgress() && mirror.staticRepromotionCount() != r0) break;
    }
    const quint32 B = SceneMirror::kStaticSettleNodesPerSync;
    const int rotation = int((nodes + B - 1) / B);
    const int bound = int(SceneMirror::kStaticSettleFrames) + rotation + 8;
    std::printf("    settle under thumbnails: %d syncs (%d tiles, %d null), bound %d = %u quiet + "
                "%d rotation + 8; static %zu -> %zu (was %zu)\n",
                syncs, tiles, blankTiles, bound, SceneMirror::kStaticSettleFrames, rotation,
                demoted, staticCount(all), settled);
    CHECK(blankTiles == 0, "every thumbnail rendered");
    CHECK(syncs <= bound, "the settle completed within its quiet window + one rotation");
    CHECK(staticCount(all) == settled, "the settle put back the whole static set");

    mirror.setSource(iris::ScenePtr());
    std::printf("%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
