// gi.extract_cost — THE EXTRACT AT K = 100 (ENGINE-UPGRADE-U3 stage 5; ENGINE_UPGRADE_SPEC's row, U2's measurement:
// 0.44 ms). A 10,000-node document mirrored into an engine scene; each tick writes 100 nodes' transforms and the
// mirror's sync — the EXTRACT into the record, the game thread's whole per-tick cost for the scene — is timed: the
// median over 60 ticks <= 1 ms (Debug). The bar is the design's law (the game thread never waits on the scene's
// size: a still scene and a busy one cost what their writes cost).
#include <QGuiApplication>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "../support/enginetesthelpers.h"
#include "../support/testmesh.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } std::fflush(stdout); } while (0)

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    std::printf("== gi.extract_cost: the mirror's extract with 100 of 10,000 nodes written per tick\n");
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test_mirror_extract_cost-ogre.log"), err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    View *view = engine->createOffscreenView("extract", 64, 64, Colour(0, 0, 0));
    Scene *target = engine->createScene("extract");
    if (!view || !target || !view->setScene(target)) { std::printf("FAIL: view/scene\n"); return 1; }
    iris::MeshPtr cube = testmesh::load(":assets/models/cube.obj");
    CHECK(!cube.isNull(), "the cube mesh loads");
    auto mat = iris::PbrMaterial::create();
    iris::ScenePtr doc = iris::Scene::create();
    std::vector<iris::SceneNodePtr> nodes;
    for (int i = 0; i < 10000; ++i) {
        auto mn = iris::MeshNode::create();
        mn->setMesh(cube);
        mn->setMaterial(mat);
        mn->setLocalPos(iris::Vec3(float(i % 100) * 0.5f, 0.0f, float(i / 100) * 0.5f));
        doc->getRootNode()->addChild(mn, false);
        nodes.push_back(mn);
    }
    SceneMirror mirror(target);
    mirror.setSource(doc);
    mirror.sync();
    enginetest::frames(engine, 2);
    for (int w = 0; w < 10; ++w) { mirror.sync(); enginetest::frames(engine, 1); }   // warm: the settle's first pass
    std::vector<double> us;
    for (int t = 0; t < 60; ++t) {
        for (int k = 0; k < 100; ++k) {
            auto &n = nodes[size_t((t * 100 + k * 97) % int(nodes.size()))];
            n->setLocalPos(n->getLocalPos() + iris::Vec3(0.0f, 0.001f, 0.0f));
        }
        const auto t0 = std::chrono::steady_clock::now();
        mirror.sync();
        us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
        enginetest::frames(engine, 1);
    }
    std::sort(us.begin(), us.end());
    const double median = us[us.size() / 2];
    std::printf("GI_EXTRACT_COST k=100 nodes=10000 median_us=%.1f min_us=%.1f max_us=%.1f\n", median, us.front(),
                us.back());
    CHECK(median <= 1000.0, "the extract of 100 written nodes in a 10,000-node scene costs <= 1 ms (median %.1f us)",
          median);
    std::printf(gFailures ? "gi.extract_cost: FAILED\n" : "gi.extract_cost: PASS\n");
    return gFailures ? 1 : 0;
}
