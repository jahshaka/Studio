// One-shot migration tool (§3b acceptance): re-saves a shipped .effect preset
// THROUGH the real load path — NodeGraph::deserialize migrates its
// graph["properties"] + PropertyNode instances into real nodes, then
// NodeGraph::serialize writes the new-format graph (no "properties").
//
//   resave_presets <file.effect> [relImage ...]
//
// The optional relImage arguments re-target the preset's texture properties
// (in property order — the same pairing the old template loader used) to
// app-relative image names ("wood.jpg", "materials_to_graph/brick diff.jpg").
// EffectsPage::loadGraphFromTemplate imports those against the shadergraph
// asset folder at instantiation, so the re-saved presets need no property
// list to find their images.
//
// Stale vertexShaderSource/fragmentShaderSource keys, if present, pass
// through verbatim (phase 5 deleted every writer; readers stay tolerant
// and the shipped presets no longer carry them);
// "pbrMaterial" is refreshed from the evaluator like MaterialHelper::serialize.
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/models/properties.h"
#include "modules/materials/nodes/test.h"
#include "modules/materials/core/pbrgraphevaluator.h"

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    if (argc < 2) {
        std::fprintf(stderr, "usage: resave_presets <file.effect> [relImage ...]\n");
        return 2;
    }

    const QString path = QString::fromLocal8Bit(argv[1]);
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    QJsonObject obj = QJsonDocument::fromJson(in.readAll()).object();
    in.close();
    if (!obj.contains("shadergraph")) {
        std::fprintf(stderr, "%s has no shadergraph\n", argv[1]);
        return 1;
    }

    // THE migration: the real load path
    auto graph = NodeGraph::deserialize(obj["shadergraph"].toObject(), new LibraryV1());
    if (graph == nullptr || graph->getMasterNode() == nullptr) {
        std::fprintf(stderr, "%s did not deserialize to a graph with a master\n", argv[1]);
        return 1;
    }

    // re-target texture properties' migrated nodes to app-relative images
    int argIndex = 2;
    for (auto prop : graph->properties) {
        if (prop->type != PropertyType::Texture) continue;
        if (argIndex >= argc) break;
        const QString rel = QString::fromLocal8Bit(argv[argIndex++]);
        for (auto it = graph->migratedPropertyNodes.constBegin();
             it != graph->migratedPropertyNodes.constEnd(); ++it) {
            if (it.value() != prop->id) continue;
            if (auto texNode = dynamic_cast<TextureNode*>(graph->getNode(it.key())))
                texNode->setTextureGuid(rel); // stored verbatim; resolved at instantiation
        }
    }

    obj["shadergraph"] = graph->serialize();   // new format: no "properties"
    obj["properties"] = QJsonArray();          // the top-level mirror empties too

    // refreshed evaluated output, as MaterialHelper::serialize writes it
    auto evaluated = PbrGraphEvaluator::evaluate(graph, nullptr);
    QJsonObject pbrObj;
    pbrObj["values"] = evaluated.values;
    pbrObj["unsupportedNodes"] = QJsonArray::fromStringList(evaluated.unsupportedNodes);
    pbrObj["surfaceType"] = evaluated.hasPbrMaster ? "pbr" : "surface";
    obj["pbrMaterial"] = pbrObj;

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    out.write(QJsonDocument(obj).toJson());
    out.close();

    std::printf("resaved %s (%d nodes, %d connections, %d migrated)\n", argv[1],
                (int)graph->nodes.size(), (int)graph->connections.size(),
                (int)graph->migratedPropertyNodes.size());
    return 0;
}
