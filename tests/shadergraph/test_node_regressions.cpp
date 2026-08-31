// Regressions from the 2026-08-31 node-library audit + the inline-editor work:
//  - D1: a graph saved while TruncNode wrote typeName "truncate" (registry key
//        "trunc") segfaulted NodeGraph::deserialize; it must LOAD, and new
//        saves must write the registry key.
//  - D2: a bare PropertyNode (no setProperty) had an uninitialized Property*
//        and crashed on serializeWidgetValue/process.
//  - D4/D13: Vector2/3/4 constants truncated fractional components to int, and
//        Vector4's W spinbox was wired to Z.
//  - Titles: a node created through the library carries its drawer display
//        name verbatim (nothing renames it afterwards).
//
// No GL, no engine. QT_QPA_PLATFORM=offscreen.
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>
#include <cstdio>

#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/library.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/nodes/pbrmasternode.h"
#include "modules/materials/nodes/test.h"
#include "modules/materials/core/pbrgraphevaluator.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) < eps; }

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv); // widget-backed nodes need a QApplication

    // ---------------------------------------------------------------- D1
    {
        // build a graph with a master and a Truncate node, serialize it,
        // then rewrite the type to the legacy "truncate" spelling
        auto graph = new NodeGraph();
        graph->setNodeLibrary(new LibraryV1());
        auto master = new PbrMasterNode();
        graph->addNode(master);
        graph->setMasterNode(master);

        auto trunc = graph->library->createNode("trunc");
        CHECK(trunc != nullptr, "D1: 'trunc' is a valid library key");
        graph->addNode(trunc);
        CHECK(trunc->typeName == "trunc", "D1: TruncNode serializes its registry key");
        graph->addConnection(trunc, 0, master, 1); // wire it so connections load too

        auto json = graph->serialize();

        // the legacy spelling, exactly as broken saves carry it
        auto nodes = json["nodes"].toArray();
        for (int i = 0; i < nodes.size(); ++i) {
            auto obj = nodes[i].toObject();
            if (obj["type"].toString() == "trunc") obj["type"] = "truncate";
            nodes[i] = obj;
        }
        json["nodes"] = nodes;

        // used to SIGSEGV (nullptr from createNode, immediate deref)
        auto loaded = NodeGraph::deserialize(json, new LibraryV1());
        CHECK(loaded != nullptr, "D1: graph with legacy 'truncate' node loads");
        CHECK(loaded->nodes.size() == 2, "D1: the Truncate node survives the load");
        bool migrated = false;
        for (auto node : loaded->nodes.values())
            if (node->typeName == "trunc") migrated = true;
        CHECK(migrated, "D1: legacy 'truncate' migrates to 'trunc' on load");
        CHECK(loaded->connections.size() == 1, "D1: its connection survives too");

        // an entirely unknown type must skip cleanly, not crash
        auto bogus = json;
        auto bogusNodes = bogus["nodes"].toArray();
        {
            QJsonObject obj;
            obj["id"] = "bogus-id";
            obj["type"] = "no-such-node";
            obj["value"] = "";
            obj["x"] = 0; obj["y"] = 0;
            bogusNodes.append(obj);
        }
        bogus["nodes"] = bogusNodes;
        auto loaded2 = NodeGraph::deserialize(bogus, new LibraryV1());
        CHECK(loaded2 != nullptr && loaded2->nodes.size() == 2,
              "D1: unknown node type is skipped instead of crashing the load");
    }

    // ---------------------------------------------------------------- D2
    {
        LibraryV1 library;
        auto bare = library.createNode("property");
        CHECK(bare != nullptr, "D2: bare property node constructs");
        // both used to read an uninitialized Property* (SIGSEGV)
        auto value = bare->serializeWidgetValue();
        CHECK(value.toString().isEmpty(), "D2: bare property serializes to empty, no crash");
        bare->process(nullptr);
        CHECK(true, "D2: bare property process() is a guarded no-op");
    }

    // ------------------------------------------------------------ D4/D13
    {
        LibraryV1 library;

        auto v2 = library.createNode("vector2");
        QJsonObject o2; o2["x"] = 0.5; o2["y"] = -1.25;
        v2->deserializeWidgetValue(o2);
        auto r2 = v2->serializeWidgetValue().toObject();
        CHECK(near(r2["x"].toDouble(), 0.5) && near(r2["y"].toDouble(), -1.25),
              "D4: vector2 keeps fractional/negative components");

        auto v3 = library.createNode("vector3");
        QJsonObject o3; o3["x"] = 2.0; o3["y"] = -1.0; o3["z"] = 0.5;
        v3->deserializeWidgetValue(o3);
        auto r3 = v3->serializeWidgetValue().toObject();
        CHECK(near(r3["z"].toDouble(), 0.5), "D4: vector3 z=0.5 survives the round-trip");

        auto v4node = library.createNode("vector4");
        QJsonObject o4; o4["x"] = 0.1; o4["y"] = 0.2; o4["z"] = 0.3; o4["w"] = 0.4;
        v4node->deserializeWidgetValue(o4);
        auto r4 = v4node->serializeWidgetValue().toObject();
        CHECK(near(r4["x"].toDouble(), 0.1) && near(r4["y"].toDouble(), 0.2)
              && near(r4["z"].toDouble(), 0.3) && near(r4["w"].toDouble(), 0.4),
              "D4: vector4 {0.1,0.2,0.3,0.4} survives the round-trip");

        // D13: the W spinbox drives w (it was copy-pasted onto Z)
        auto vec4 = static_cast<Vector4Node*>(v4node);
        vec4->wSpinBox->setValue(0.6);
        auto rw = v4node->serializeWidgetValue().toObject();
        CHECK(near(rw["w"].toDouble(), 0.6) && near(rw["z"].toDouble(), 0.3),
              "D13: W spinbox writes w and leaves z alone");

        // and the evaluator sees the un-truncated values
        auto graph = new NodeGraph();
        graph->setNodeLibrary(new LibraryV1());
        auto master = new PbrMasterNode();
        graph->addNode(master);
        graph->setMasterNode(master);
        graph->addNode(v3);
        graph->addConnection(v3, 0, master, 0); // Base Color
        auto result = PbrGraphEvaluator::evaluate(graph, nullptr);
        auto base = result.values["baseColor"].toObject();
        CHECK(near(base["b"].toDouble(), 0.5, 0.01),
              "D4: evaluator folds vector3 z=0.5 into baseColor.b");
    }

    // ------------------------------------------------- drawer-name titles
    {
        LibraryV1 library;
        int mismatches = 0;
        for (auto item : library.getItems()) {
            auto node = library.createNode(item->name);
            if (node == nullptr || node->title != item->displayName) {
                std::printf("      title mismatch: key '%s' drawer '%s' title '%s'\n",
                            qPrintable(item->name), qPrintable(item->displayName),
                            node ? qPrintable(node->title) : "<null>");
                ++mismatches;
            }
        }
        CHECK(mismatches == 0, "titles: every library node carries its drawer name verbatim");

        auto f = library.createNode("float");
        CHECK(f->title == "Float", "titles: the Float node is titled 'Float'");
    }

    // --------------------------------------------- float value round-trip
    {
        LibraryV1 library;
        auto f = library.createNode("float");
        f->deserializeWidgetValue(QJsonValue(0.7));
        CHECK(near(f->serializeWidgetValue().toDouble(), 0.7),
              "float: inline editor value round-trips through (de)serialize");
    }

    if (failures == 0) std::printf("ALL OK\n");
    else std::printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
