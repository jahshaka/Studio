// mirror.messages_in_order — THE SYNC'S COMMANDS ARE MESSAGES, IN ORDER, UNDER RESERVED IDS (ENGINE-UPGRADE-U3
// stage 3). One tick creates 100 mesh nodes (a node, its mesh, its material), re-parents half of them under a second
// group and destroys ten: every command the mirror gives is a message in the record (Engine::submitFrame), applied
// at the top of the next frame before the node records, and every create names the id the mirror reserved — so
//   * AFTER one frame every surviving node exists under its DOCUMENT id (the engine node IS the document's id),
//     sits under the parent the reparent named and draws; the destroyed ten are gone;
//   * the engine assigned NO id of its own (SceneChangeStats::engineAssignedIds unmoved) and the frame refused nothing.
// A law in counts; two frames.
#include <QGuiApplication>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../support/enginetesthelpers.h"
#include "../support/testmesh.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/vertexlayout.h"
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
    std::printf("== mirror.messages_in_order: one tick's commands are messages, in order, under reserved ids\n");
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test_mirror_messages-ogre.log"), err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    View *view = engine->createOffscreenView("messages", 64, 64, Colour(0, 0, 0));
    Scene *target = engine->createScene("messages");
    CHECK(view && target && view->setScene(target), "an offscreen view on the mirror's scene");
    if (!view || !target) return 1;
    enginetest::testCameraLookAt(view, Vec3(0.0f, 8.0f, 30.0f), Vec3(0.0f, 0.0f, 0.0f));
    target->setAmbient(Colour(0.6f, 0.6f, 0.6f), Colour(0.6f, 0.6f, 0.6f));   // the cubes show without a lamp

    iris::MeshPtr cube = testmesh::load(":assets/models/cube.obj");
    CHECK(!cube.isNull(), "the cube mesh loads");
    iris::ScenePtr doc = iris::Scene::create();
    SceneMirror mirror(target);
    mirror.setSource(doc);
    mirror.sync();
    enginetest::frames(engine, 2);   // the empty document settles

    const SceneChangeStats before = target->published().changes;
    const unsigned long long messagesBefore = engine->published().input.messagesTotal;
    const size_t errorsBefore = engine->published().frameErrors.size();

    // ---- ONE TICK: 100 creates, half re-parented, ten destroyed --------------------------------------------------
    iris::SceneNodePtr groupA = iris::SceneNode::create(), groupB = iris::SceneNode::create();
    groupB->setLocalPos(iris::Vec3(0.0f, 3.0f, 0.0f));
    doc->getRootNode()->addChild(groupA, false);
    doc->getRootNode()->addChild(groupB, false);
    std::vector<iris::SceneNodePtr> nodes;
    for (int i = 0; i < 100; ++i) {
        auto mn = iris::MeshNode::create();
        mn->setMesh(cube);
        auto mat = iris::PbrMaterial::create();   // its own material: a create per node
        mat->setValue(QStringLiteral("baseColor"), QColor::fromHsv((i * 7) % 360, 200, 220));
        mn->setMaterial(mat);
        mn->setLocalPos(iris::Vec3(float(i % 10) - 4.5f, 0.0f, float(i / 10) - 4.5f));
        mn->setLocalScale(iris::Vec3(0.4f, 0.4f, 0.4f));
        groupA->addChild(mn, false);
        nodes.push_back(mn);
    }
    for (int i = 0; i < 50; ++i) groupB->addChild(nodes[size_t(i)], false);      // reparent half
    for (int i = 90; i < 100; ++i) nodes[size_t(i)]->removeFromParent();          // destroy ten
    const auto syncStart = std::chrono::steady_clock::now();
    mirror.sync();
    const double syncUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - syncStart).count();

    // BEFORE THE FRAME: nothing was applied on the spot.
    const NodeId first = mirror.engineNode(nodes[0].data());
    CHECK(first == NodeId(nodes[0]->nodeId), "the engine node of a document node IS its document id (%llu)",
          (unsigned long long)first);
    CHECK(engine->frameSubmitStatus().messagesPosted > 0, "the sync's commands are in the pending record (%llu posted)",
          engine->frameSubmitStatus().messagesPosted);

    enginetest::frames(engine, 1);

    // AFTER ONE FRAME: everything, in order, under the reserved ids.
    const SceneChangeStats after = target->published().changes;
    const unsigned long long messages = engine->published().input.messagesTotal - messagesBefore;
    std::printf("    one tick: %llu messages applied, %llu items created, %llu engine-assigned ids\n", messages,
                after.itemsCreated - before.itemsCreated, after.engineAssignedIds - before.engineAssignedIds);
    // THE COST OF THE DOOR (decision 35): messages per adopted node and the adopting sync's time per node — the
    // numbers of record, printed (the row's law is the counts; the time is Debug's and the box's).
    std::printf("    the adopting sync: %.2f messages per adopted node, %.1f us per node (%.0f us for 90 + 2 groups)\n",
                double(messages) / 92.0, syncUs / 92.0, syncUs);
    CHECK(messages >= 300, "the tick's creates, attaches and materials arrived as messages (%llu)", messages);
    CHECK(after.itemsCreated - before.itemsCreated == 90,
          "every surviving node drew an Item, none of the destroyed ten did (%llu)",
          after.itemsCreated - before.itemsCreated);
    CHECK(after.engineAssignedIds == before.engineAssignedIds,
          "THE ENGINE ASSIGNED NO ID: every create named the one the mirror reserved (%llu)",
          after.engineAssignedIds - before.engineAssignedIds);
    CHECK(engine->published().frameErrors.size() == errorsBefore, "the frame refused nothing");
    int underB = 0, gone = 0;
    for (int i = 0; i < 100; ++i) {
        const NodeId id = mirror.engineNode(nodes[size_t(i)].data());
        if (i >= 90) { if (id == 0) ++gone; continue; }
        if (i < 50 && id == NodeId(nodes[size_t(i)]->nodeId)) ++underB;
    }
    CHECK(gone == 10, "the destroyed ten have no engine node (%d)", gone);
    CHECK(underB == 50, "the re-parented half kept their reserved ids (%d)", underB);
    Image img;
    CHECK(view->readPixels(img), "the frame reads back");
    int lit = 0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            if (c.r + c.g + c.b > 0.05f) ++lit;
        }
    CHECK(lit > 100, "the cubes draw in that frame (%d pixels)", lit);

    // ---- THE LARGE IMPORT'S SYNC (decision 46 (3), MEASURED): one 512 x 512-vertex grid adopted — the UI thread's
    // cost of handing its mesh to the engine (the host's copy of the vertex data is what a move removes).
    {
        const int N = 512;
        std::vector<float> pos, nrm;
        std::vector<unsigned> idx;
        pos.reserve(size_t(N) * N * 3); nrm.reserve(size_t(N) * N * 3);
        for (int z = 0; z < N; ++z)
            for (int x = 0; x < N; ++x) {
                pos.insert(pos.end(), { float(x) * 0.01f - 2.5f, 0.0f, float(z) * 0.01f - 2.5f });
                nrm.insert(nrm.end(), { 0.0f, 1.0f, 0.0f });
            }
        for (int z = 0; z + 1 < N; ++z)
            for (int x = 0; x + 1 < N; ++x) {
                const unsigned a0 = unsigned(z * N + x), a1 = a0 + 1, b0 = a0 + unsigned(N), b1 = b0 + 1;
                idx.insert(idx.end(), { a0, b0, a1, a1, b0, b1 });
            }
        auto big = iris::Mesh::create();
        auto addBuf = [&big](iris::VertexAttribUsage usage, const std::vector<float> &data, int comps) {
            iris::VertexLayout layout;
            layout.addAttrib(usage, iris::AttribTypeFloat, comps, comps * int(sizeof(float)));
            auto vb = iris::VertexBuffer::create(layout);
            vb->setData(const_cast<float *>(data.data()), unsigned(data.size() * sizeof(float)));
            big->addVertexBuffer(vb);
        };
        addBuf(iris::VertexAttribUsage::Position, pos, 3);
        addBuf(iris::VertexAttribUsage::Normal, nrm, 3);
        auto ib = iris::IndexBuffer::create();
        ib->setData(idx.data(), unsigned(idx.size() * sizeof(unsigned)));
        big->setIndexBuffer(ib);
        big->setVertexCount(N * N);
        auto mn = iris::MeshNode::create();
        mn->setMesh(big);
        mn->setMaterial(iris::PbrMaterial::create());
        doc->getRootNode()->addChild(mn, false);
        const auto t0 = std::chrono::steady_clock::now();
        mirror.sync();
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        std::printf("    the large import's adopting sync: %.0f us for one %d-vertex mesh (the UI thread's)\n", us, N * N);
        enginetest::frames(engine, 1);
        CHECK(mirror.engineNode(mn.data()) != 0, "the large mesh is adopted");
    }

    std::printf(gFailures ? "mirror.messages_in_order: FAILED\n" : "mirror.messages_in_order: PASS\n");
    return gFailures ? 1 : 0;
}
