// shadergraph.ao_removed — HLMS_ADOPTION P2: the ambient-occlusion ghost is
// gone, and a graph authored WITH it is refused.
//
// The ghost: the renderer has no ambient-occlusion input at all, yet the
// master node carried an "Occlusion" socket at index 4, GraphBaker baked it to
// a full-resolution occlusionMap PNG in the user's project, and the mirror
// dropped it on the floor. The socket is deleted.
//
// Deleting a socket from a node whose CONNECTIONS ARE STORED BY INDEX is the
// dangerous half. Reading a layout-1 graph as if it were layout 2 would land
// the old Emissive connection on Normal, Alpha on Emissive, Alpha Cutoff on
// Alpha — silently. So the layout is VERSIONED and deserialize REFUSES any
// other (FORWARD-ONLY-1: the index-shift migration is deleted). This asserts:
//
//   1. new graphs stamp socketLayout 2 and the master has nine sockets, none
//      of them Occlusion;
//   2. a layout-1 graph (ten sockets, Occlusion at 4) is refused whole;
//   3. a layout-2 graph loads as written, and no occlusion* value or socket
//      survives into bakeInfo or the evaluated material.
//
// No GL, no engine. QT_QPA_PLATFORM=offscreen.
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <cstdio>

#include "modules/materials/core/graphbaker.h"
#include "modules/materials/core/pbrgraphevaluator.h"
#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/library.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/models/nodemodel.h"
#include "modules/materials/models/connectionmodel.h"
#include "modules/materials/models/socketmodel.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "modules/materials/nodes/pbrmasternode.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// A graph with a float wired into each of several master inputs, saved in
// LAYOUT 2 (what the code writes today).
static QJsonObject buildLayout2Graph(LibraryV1* lib, QString* emissiveNodeId,
                                     QString* alphaNodeId)
{
    NodeGraph graph;
    graph.setNodeLibrary(lib);
    auto* master = new PbrMasterNode();
    graph.addNode(master);
    graph.setMasterNode(master);

    auto* emissive = lib->createNode("vector3");
    graph.addNode(emissive);
    graph.addConnection(emissive, 0, master, 4);   // layout 2: Emissive
    *emissiveNodeId = emissive->id;

    auto* alpha = lib->createNode("float");
    alpha->deserializeWidgetValue(QJsonValue(0.25));
    graph.addNode(alpha);
    graph.addConnection(alpha, 0, master, 5);      // layout 2: Alpha
    *alphaNodeId = alpha->id;

    return graph.serialize();
}

// The same graph as a LAYOUT-1 file: the socketLayout key absent (it did not
// exist), indices as layout 1 numbered them, plus a connection into the
// Occlusion socket at index 4 — the chain this phase deletes.
static QJsonObject toLayout1(QJsonObject layout2, const QString& emissiveId,
                             const QString& alphaId, const QString& occlusionId)
{
    layout2.remove("socketLayout");
    QJsonArray cons = layout2["connections"].toArray();
    for (int i = 0; i < cons.size(); ++i) {
        QJsonObject c = cons[i].toObject();
        const QString right = c["leftNodeId"].toString();
        if (right == emissiveId) c["rightNodeSocketIndex"] = 5;  // Emissive was 5
        if (right == alphaId)    c["rightNodeSocketIndex"] = 6;  // Alpha was 6
        cons[i] = c;
    }
    QJsonObject occl;
    occl["id"] = QStringLiteral("legacy-occlusion-connection");
    occl["leftNodeId"] = occlusionId;
    occl["leftNodeSocketIndex"] = 0;
    occl["rightNodeId"] = layout2["masternode"].toString();
    occl["rightNodeSocketIndex"] = 4;                            // Occlusion
    cons.append(occl);
    layout2["connections"] = cons;
    return layout2;
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // widget-backed nodes need a QApplication

    LibraryV1 lib;

    // ---- 1. the socket is gone, and the layout is stamped ----
    {
        PbrMasterNode master;
        CHECK(master.inSockets.size() == 9, "master has nine input sockets (was ten)");
        bool sawOcclusion = false;
        for (auto* sock : master.inSockets)
            if (sock->name == QStringLiteral("Occlusion")) sawOcclusion = true;
        CHECK(!sawOcclusion, "no 'Occlusion' socket on the master node");
        CHECK(master.inSockets[4]->name == QStringLiteral("Emissive"),
              "index 4 is Emissive now (it was Occlusion)");
    }

    QString emissiveId, alphaId;
    const QJsonObject saved = buildLayout2Graph(&lib, &emissiveId, &alphaId);
    CHECK(saved["socketLayout"].toInt() == NodeGraph::kSocketLayoutVersion,
          "a saved graph stamps its socket layout");

    // ---- 2. a layout-1 file is REFUSED (FORWARD-ONLY-1: no index shift) ----
    {
        QJsonObject l1 = saved;
        QJsonArray nodes = l1["nodes"].toArray();
        QJsonObject occNode;
        occNode["id"] = QStringLiteral("legacy-occlusion-source");
        occNode["type"] = QStringLiteral("float");
        occNode["value"] = QJsonValue(0.75);
        occNode["title"] = QStringLiteral("AO");
        occNode["x"] = 0.0;
        occNode["y"] = 0.0;
        nodes.append(occNode);
        l1["nodes"] = nodes;
        l1 = toLayout1(l1, emissiveId, alphaId, QStringLiteral("legacy-occlusion-source"));

        QString reason;
        NodeGraph* loaded = NodeGraph::deserialize(l1, &lib, &reason);
        CHECK(loaded == nullptr, "a layout-1 graph is REFUSED, not shifted");
        CHECK(!reason.isEmpty(), "...with a reason the caller can show");
        delete loaded;
    }

    // ---- 3. a layout-2 file loads as written, with nothing occlusion-shaped ----
    {
        NodeGraph* loaded = NodeGraph::deserialize(saved, &lib);
        CHECK(loaded != nullptr, "the layout-2 graph loads");
        if (!loaded) { std::printf("FAILED (%d)\n", failures); return 1; }
        auto* master = loaded->getMasterNode();
        CHECK(master->inSockets[4]->hasConnection() &&
                  master->inSockets[4]->getConnection()->leftSocket->node->id == emissiveId,
              "Emissive stays on index 4");

        const QJsonObject info = materials::GraphBaker::classify(loaded, nullptr);
        CHECK(!info["perSocket"].toObject().contains(QStringLiteral("Occlusion")),
              "bakeInfo has no Occlusion socket to classify");

        const auto result = PbrGraphEvaluator::evaluate(loaded);
        bool sawOcclusionValue = false;
        for (auto it = result.values.begin(); it != result.values.end(); ++it)
            if (it.key().startsWith(QStringLiteral("occlusion"))) sawOcclusionValue = true;
        CHECK(!sawOcclusionValue, "no occlusion* key in the evaluated material values");

        auto material = PbrGraphEvaluator::materialFromValues(result.values);
        bool declaresOcclusion = false;
        for (auto* prop : material->properties)
            if (prop->name.startsWith(QStringLiteral("occlusion"))) declaresOcclusion = true;
        CHECK(!declaresOcclusion, "PbrMaterial declares no occlusion row");
        delete loaded;
    }

    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
