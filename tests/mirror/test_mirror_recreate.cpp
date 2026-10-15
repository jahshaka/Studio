// mirror.full_walk_recreate — A SAME-ID RE-CREATE UNDER A FULL WALK IS DRAWN (ENGINE-UPGRADE-U3 stage 3 fix round,
// decision 33). An undo rebuilds a fragment with its SESSION ids (src/io/scenereader.cpp's replay), so a delete and
// its undo in ONE tick hand the mirror a new document node under the id of one it still holds. The dirty path
// releases the evicted entry before it adopts; the full walk (a bind, a re-take, an overflow, the play edge,
// requestFullWalk, JAH_MIRROR_VERIFY=full) used to adopt FIRST — createDocumentNode(X) refused "already a node" —
// and remove after, deleting the OLD node: the new one was never drawn and every later command for X was refused.
// Here: a cube drawn, then deleted and re-created under its id in one tick, under a forced full walk; after ONE sync
// and ONE frame the new node is the engine's node X and draws the same picture; the frame refused nothing.
#include <QGuiApplication>
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

static int litPixels(View *view, std::vector<unsigned char> *bytes = nullptr)
{
    Image img;
    if (!view->readPixels(img)) return -1;
    if (bytes) *bytes = img.rgba;
    int lit = 0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            if (c.r + c.g + c.b > 0.05f) ++lit;
        }
    return lit;
}

static iris::SceneNodePtr makeCube(const iris::MeshPtr &mesh, const iris::MaterialPtr &mat)
{
    auto mn = iris::MeshNode::create();
    mn->setMesh(mesh);
    mn->setMaterial(mat);
    return mn;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    std::printf("== mirror.full_walk_recreate: delete + undo in one tick under a full walk, the node drawn after\n");
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test_mirror_recreate-ogre.log"), err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    View *view = engine->createOffscreenView("recreate", 64, 64, Colour(0, 0, 0));
    Scene *target = engine->createScene("recreate");
    CHECK(view && target && view->setScene(target), "an offscreen view on the mirror's scene");
    if (!view || !target) return 1;
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 5.0f), Vec3(0.0f, 0.0f, 0.0f));
    target->setAmbient(Colour(0.6f, 0.6f, 0.6f), Colour(0.6f, 0.6f, 0.6f));

    iris::MeshPtr cube = testmesh::load(":assets/models/cube.obj");
    CHECK(!cube.isNull(), "the cube mesh loads");
    auto mat = iris::PbrMaterial::create();
    mat->setValue(QStringLiteral("baseColor"), QColor(220, 120, 60));
    iris::ScenePtr doc = iris::Scene::create();
    SceneMirror mirror(target);
    mirror.setSource(doc);
    iris::SceneNodePtr a = makeCube(cube, mat);
    doc->getRootNode()->addChild(a, false);
    mirror.sync();
    enginetest::frames(engine, 2);
    const NodeId x = mirror.engineNode(a.data());
    const int litBefore = litPixels(view);
    CHECK(x == NodeId(a->nodeId) && litBefore > 100, "the cube is engine node %llu and draws (%d pixels)",
          (unsigned long long)x, litBefore);
    const size_t errorsBefore = engine->published().frameErrors.size();
    const unsigned long long refusedBefore = engine->published().input.messagesRefused;
    // THE CONTROL: on the original node, the same write shows (the rig sees a move at all).
    std::vector<unsigned char> still, shifted;
    litPixels(view, &still);
    a->setLocalPos(iris::Vec3(1.2f, 0.0f, 0.0f));
    mirror.sync();
    enginetest::frames(engine, 1);
    litPixels(view, &shifted);
    CHECK(still != shifted, "the control: a write to the original node moves its picture");
    a->setLocalPos(iris::Vec3(0.0f, 0.0f, 0.0f));
    mirror.sync();
    enginetest::frames(engine, 1);

    // ---- ONE TICK: the delete and its undo (the fragment keeps its session id), under a forced full walk ---------
    a->removeFromParent();
    iris::SceneNodePtr b = makeCube(cube, mat);
    b->setNodeId(a->nodeId);   // the undo's replay (scenereader.cpp)
    doc->getRootNode()->addChild(b, false);
    mirror.requestFullWalk();
    mirror.sync();
    enginetest::frames(engine, 1);

    std::vector<unsigned char> afterBytes, movedBytes;
    const int litAfter = litPixels(view, &afterBytes);
    std::printf("    lit pixels %d before, %d after the re-create\n", litBefore, litAfter);
    CHECK(mirror.engineNode(b.data()) == x, "the re-created node is the engine's node %llu (%llu)",
          (unsigned long long)x, (unsigned long long)mirror.engineNode(b.data()));
    CHECK(litAfter == litBefore, "...and it DRAWS, the same picture, after one sync and one frame");
    CHECK(engine->published().frameErrors.size() == errorsBefore &&
              engine->published().input.messagesRefused == refusedBefore,
          "the engine refused no command (%llu refused)", engine->published().input.messagesRefused - refusedBefore);
    // A later command for X lands: a move of the re-created node shows.
    b->setLocalPos(iris::Vec3(1.2f, 0.0f, 0.0f));
    mirror.sync();
    enginetest::frames(engine, 1);
    const int litMoved = litPixels(view, &movedBytes);
    // THE PICTURE'S BYTES, not its lit count (a shift keeps the count): the cube's left edge now sits right of
    // the centre column — the pixels that were the cube's left half are the background.
    size_t changed = 0;
    for (size_t i = 0; i < afterBytes.size() && i < movedBytes.size(); ++i) changed += afterBytes[i] != movedBytes[i];
    const unsigned w = 64, row = 32;
    auto litAt = [&](const std::vector<unsigned char> &px, unsigned x) {
        const size_t o = (size_t(row) * w + x) * 4u;
        return o + 2 < px.size() && int(px[o]) + px[o + 1] + px[o + 2] > 12;
    };
    int leftLitAfter = 0, leftLitMoved = 0, rightLitAfter = 0, rightLitMoved = 0;
    for (unsigned x = 0; x < w / 2; ++x) { leftLitAfter += litAt(afterBytes, x); leftLitMoved += litAt(movedBytes, x); }
    for (unsigned x = w / 2; x < w; ++x) { rightLitAfter += litAt(afterBytes, x); rightLitMoved += litAt(movedBytes, x); }
    std::printf("    after the move: %zu bytes changed; centre row lit left %d -> %d, right %d -> %d (%d lit)\n", changed,
                leftLitAfter, leftLitMoved, rightLitAfter, rightLitMoved, litMoved);
    CHECK(changed > 0 && leftLitMoved < leftLitAfter && rightLitMoved > rightLitAfter,
          "a later write to it lands: the cube moved right in the picture (+x)");

    std::printf(gFailures ? "mirror.full_walk_recreate: FAILED\n" : "mirror.full_walk_recreate: PASS\n");
    return gFailures ? 1 : 0;
}
