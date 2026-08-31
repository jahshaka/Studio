// §3b migration suite (MATERIALS_EVALUATOR_SPEC §6, shadergraph.panel_migration):
// old-format graph JSON — graph["properties"] + PropertyNode instances — loads
// into real constant/texture nodes with positions preserved, outputs
// reconnected 1:1, multi-reference properties becoming independent copies
// (owner-locked call), evaluation matching the folded property values, and
// re-saves that never write "properties" again. The shipped presets (re-saved
// through this very migration) round-trip as the acceptance test.
//
// No GL, no engine. QT_QPA_PLATFORM=offscreen.
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>
#include <cstdio>

#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/connectionmodel.h"
#include "modules/materials/models/library.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/models/properties.h"
#include "modules/materials/nodes/pbrmasternode.h"
#include "modules/materials/nodes/test.h"
#include "modules/materials/core/pbrgraphevaluator.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

// Builds an old-format node entry exactly as pre-§3b saves wrote PropertyNodes:
// type "property", value = the property id.
static QJsonObject propertyNodeJson(const QString& nodeId, const QString& propId,
                                    double x, double y)
{
    QJsonObject obj;
    obj["id"] = nodeId;
    obj["type"] = "property";
    obj["value"] = propId;
    obj["x"] = x;
    obj["y"] = y;
    return obj;
}

static QJsonObject connectionJson(const QString& left, int leftIdx,
                                  const QString& right, int rightIdx)
{
    QJsonObject obj;
    obj["id"] = left + right; // ids are opaque to the loader
    obj["leftNodeId"] = left;
    obj["leftNodeSocketIndex"] = leftIdx;
    obj["rightNodeId"] = right;
    obj["rightNodeSocketIndex"] = rightIdx;
    return obj;
}

static NodeModel* nodeOfType(NodeGraph* graph, const QString& type)
{
    for (auto node : graph->nodes.values())
        if (node->typeName == type) return node;
    return nullptr;
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // a real image so the texture property migrates to a resolvable path
    const QString texPath = QDir::current().absoluteFilePath("test_migration_tex.png");
    {
        QImage img(2, 2, QImage::Format_RGBA8888);
        img.fill(QColor(10, 200, 30));
        img.save(texPath);
    }

    // ------------------------------------------------------------------
    // 1. synthetic old-format graph: every property type, one property
    //    referenced TWICE (multi-reference), a texture property with the
    //    rgba/normal output shape and a uv input feed.
    // ------------------------------------------------------------------
    {
        QJsonObject graphObj;

        // properties, exactly in the shape Property::serialize wrote
        QJsonArray props;
        {
            QJsonObject f; f["id"] = "prop-f"; f["name"] = "property0";
            f["displayName"] = "Shine"; f["type"] = "float"; f["value"] = 0.7;
            f["minValue"] = 0; f["maxValue"] = 1; f["step"] = 0.1;
            props.append(f);

            QJsonObject v3; v3["id"] = "prop-v3"; v3["name"] = "property1";
            v3["displayName"] = "Tint"; v3["type"] = "vec3";
            QJsonObject v3val; v3val["x"] = 0.0; v3val["y"] = 1.0; v3val["z"] = 0.0;
            v3["value"] = v3val;
            props.append(v3);

            QJsonObject col; col["id"] = "prop-col"; col["name"] = "property2";
            col["displayName"] = "Glow"; col["type"] = "color";
            QJsonObject colVal; colVal["r"] = 255; colVal["g"] = 0; colVal["b"] = 0; colVal["a"] = 255;
            col["value"] = colVal;
            props.append(col);

            QJsonObject tex; tex["id"] = "prop-tex"; tex["name"] = "property3";
            tex["displayName"] = "Bumps"; tex["type"] = "texture"; tex["value"] = texPath;
            props.append(tex);

            QJsonObject b; b["id"] = "prop-b"; b["name"] = "property4";
            b["displayName"] = "Flag"; b["type"] = "bool"; b["value"] = true;
            props.append(b);
        }
        graphObj["properties"] = props;

        QJsonArray nodes;
        {
            QJsonObject master;
            master["id"] = "master-id"; master["type"] = "PbrMaterial";
            master["value"] = ""; master["x"] = 500; master["y"] = 100;
            nodes.append(master);

            QJsonObject texCoords;
            texCoords["id"] = "uv-id"; texCoords["type"] = "texCoords";
            texCoords["value"] = ""; texCoords["x"] = 0; texCoords["y"] = 400;
            nodes.append(texCoords);

            nodes.append(propertyNodeJson("float-a", "prop-f", 10, 20));
            nodes.append(propertyNodeJson("float-b", "prop-f", 30, 40)); // 2nd reference
            nodes.append(propertyNodeJson("vec3-a", "prop-v3", 50, 60));
            nodes.append(propertyNodeJson("col-a", "prop-col", 70, 80));
            nodes.append(propertyNodeJson("tex-a", "prop-tex", 90, 100));
            nodes.append(propertyNodeJson("bool-a", "prop-b", 110, 120));
        }
        graphObj["nodes"] = nodes;

        QJsonArray cons;
        cons.append(connectionJson("float-a", 0, "master-id", 2)); // Roughness
        cons.append(connectionJson("float-b", 0, "master-id", 1)); // Metallic
        cons.append(connectionJson("vec3-a", 0, "master-id", 0));  // Base Color
        cons.append(connectionJson("col-a", 0, "master-id", 5));   // Emissive
        cons.append(connectionJson("tex-a", 2, "master-id", 3));   // normal out -> Normal
        cons.append(connectionJson("uv-id", 0, "tex-a", 0));       // uv feed (drops)
        graphObj["connections"] = cons;
        graphObj["masternode"] = "master-id";

        auto graph = NodeGraph::deserialize(graphObj, new LibraryV1());
        CHECK(graph != nullptr && graph->getMasterNode() != nullptr,
              "synthetic: old-format graph loads with a master");
        CHECK(graph->getNodesByTypeName("property").isEmpty(),
              "synthetic: no 'property' nodes survive the load");
        CHECK(graph->nodes.size() == 8, "synthetic: every instance became a real node");
        CHECK(graph->migratedPropertyNodes.size() == 6, "synthetic: six migrations recorded");

        // types
        auto floatA = graph->getNode("float-a");
        auto floatB = graph->getNode("float-b");
        auto vec3A = graph->getNode("vec3-a");
        auto colA = graph->getNode("col-a");
        auto texA = graph->getNode("tex-a");
        auto boolA = graph->getNode("bool-a");
        CHECK(floatA && floatA->typeName == "float" && floatB && floatB->typeName == "float",
              "synthetic: Float property instances -> float nodes (ids preserved)");
        CHECK(vec3A && vec3A->typeName == "vector3", "synthetic: Vec3 property -> vector3 node");
        CHECK(colA && colA->typeName == "color", "synthetic: Color property -> color node");
        CHECK(texA && texA->typeName == "texture", "synthetic: Texture property -> texture node");
        CHECK(boolA && boolA->typeName == "float", "synthetic: Bool property -> float node");

        // positions + titles
        CHECK(floatA && near(floatA->getX(), 10) && near(floatA->getY(), 20)
              && floatB && near(floatB->getX(), 30) && near(floatB->getY(), 40),
              "synthetic: node positions preserved");
        CHECK(floatA && floatA->title == "Shine" && texA && texA->title == "Bumps",
              "synthetic: property display names carry onto the nodes");

        // values
        CHECK(floatA && near(floatA->serializeWidgetValue().toDouble(), 0.7),
              "synthetic: float value folded into the node");
        auto v3val = vec3A ? vec3A->serializeWidgetValue().toObject() : QJsonObject();
        CHECK(near(v3val["x"].toDouble(), 0.0) && near(v3val["y"].toDouble(), 1.0)
              && near(v3val["z"].toDouble(), 0.0),
              "synthetic: vec3 value folded into the node");
        auto colVal = colA ? colA->serializeWidgetValue().toObject() : QJsonObject();
        CHECK(near(colVal["r"].toDouble(), 1.0, 1e-2) && near(colVal["g"].toDouble(), 0.0, 1e-2),
              "synthetic: color value folded into the node");
        CHECK(boolA && near(boolA->serializeWidgetValue().toDouble(), 1.0),
              "synthetic: bool true folds to 1.0");
        CHECK(texA && static_cast<TextureNode*>(texA)->getTexturePath() == texPath,
              "synthetic: texture property's path lands on the texture node");

        // connections: 5 survive (the uv feed drops), the normal output
        // collapses onto the texture node's single output
        CHECK(graph->connections.size() == 5,
              "synthetic: 5 of 6 connections survive (uv feed dropped)");
        bool texConnRemapped = false;
        for (auto con : graph->connections.values()) {
            if (con->leftSocket->node->id == "tex-a")
                texConnRemapped = (con->leftSocket->node->outSockets.indexOf(con->leftSocket) == 0);
        }
        CHECK(texConnRemapped, "synthetic: texture rgba/normal output remapped to output 0");

        // multi-reference = independent copies (owner-locked)
        if (floatA && floatB) {
            floatA->deserializeWidgetValue(QJsonValue(0.2));
            CHECK(near(floatB->serializeWidgetValue().toDouble(), 0.7),
                  "synthetic: editing one copy leaves the other at 0.7 (independent copies)");
            floatA->deserializeWidgetValue(QJsonValue(0.7)); // restore for evaluation
        }

        // evaluation matches the folded property values
        auto result = PbrGraphEvaluator::evaluate(graph, nullptr);
        CHECK(near(result.values["roughness"].toDouble(), 0.7, 1e-4), "synthetic: roughness folds to 0.7");
        CHECK(near(result.values["metallic"].toDouble(), 0.7, 1e-4), "synthetic: metallic folds to 0.7");
        auto base = result.values["baseColor"].toObject();
        CHECK(near(base["g"].toDouble(), 1.0, 1e-4) && near(base["r"].toDouble(), 0.0, 1e-4),
              "synthetic: baseColor folds from the migrated vector3");
        auto emissive = result.values["emissiveColor"].toObject();
        CHECK(near(emissive["r"].toDouble(), 1.0, 1e-2), "synthetic: emissive folds from the migrated color");
        CHECK(result.values["normalMap"].toString() == texPath,
              "synthetic: normalMap path flows from the migrated texture node");

        // re-save: no "properties", nodes keep types/values; round-trips
        auto saved = graph->serialize();
        CHECK(!saved.contains("properties"), "synthetic: re-save writes no 'properties'");
        auto reloaded = NodeGraph::deserialize(saved, new LibraryV1());
        CHECK(reloaded->nodes.size() == graph->nodes.size()
              && reloaded->connections.size() == graph->connections.size(),
              "synthetic: re-saved graph round-trips node/connection counts");
        auto reFloat = reloaded->getNode("float-a");
        CHECK(reFloat && reFloat->typeName == "float"
              && near(reFloat->serializeWidgetValue().toDouble(), 0.7)
              && reFloat->title == "Shine",
              "synthetic: round-trip keeps values and migrated titles");
        auto result2 = PbrGraphEvaluator::evaluate(reloaded, nullptr);
        CHECK(result2.values == result.values, "synthetic: evaluation identical after round-trip");
    }

    // ------------------------------------------------------------------
    // 2. real old-format fixtures (pre-migration snapshots of the shipped
    //    presets)
    // ------------------------------------------------------------------
    auto loadEffect = [](const QString& path) -> NodeGraph* {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return nullptr;
        auto obj = QJsonDocument::fromJson(f.readAll()).object();
        if (!obj.contains("shadergraph")) return nullptr;
        return NodeGraph::deserialize(obj["shadergraph"].toObject(), new LibraryV1());
    };

    {
        auto checker = loadEffect(QString(JAHSHAKA_TEST_FIXTURE_DIR) + "checker_oldformat.effect");
        CHECK(checker != nullptr && checker->getMasterNode() != nullptr,
              "fixtures: old-format checker.effect loads");
        CHECK(checker && checker->getNodesByTypeName("property").isEmpty()
              && checker->getNodesByTypeName("texture").size() == 1,
              "fixtures: checker's texture property became a texture node");
        CHECK(checker && checker->connections.size() == 1,
              "fixtures: checker's rgba->Diffuse connection survives");
        CHECK(checker && !checker->serialize().contains("properties"),
              "fixtures: checker re-save drops 'properties'");

        auto brick = loadEffect(QString(JAHSHAKA_TEST_FIXTURE_DIR) + "brick_oldformat.effect");
        CHECK(brick != nullptr && brick->getMasterNode() != nullptr,
              "fixtures: old-format Brick.effect loads");
        CHECK(brick && brick->getNodesByTypeName("texture").size() == 3
              && brick->getNodesByTypeName("float").size() == 1,
              "fixtures: Brick's 3 texture + 1 float properties became nodes");
        CHECK(brick && brick->connections.size() == 4,
              "fixtures: Brick's 4 connections survive");
        bool allLeftZero = true;
        if (brick) {
            for (auto con : brick->connections.values())
                if (con->leftSocket->node->typeName == "texture"
                    && con->leftSocket->node->outSockets.indexOf(con->leftSocket) != 0)
                    allLeftZero = false;
        }
        CHECK(allLeftZero, "fixtures: Brick's texture outputs all remap to output 0");

        auto basic = loadEffect(QString(JAHSHAKA_TEST_FIXTURE_DIR) + "basic_oldformat.effect");
        CHECK(basic != nullptr, "fixtures: old-format basic.effect loads");
        if (basic) {
            auto result = PbrGraphEvaluator::evaluate(basic, nullptr);
            auto base = result.values["baseColor"].toObject();
            CHECK(near(base["g"].toDouble(), 1.0, 1e-4) && near(base["r"].toDouble(), 0.0, 1e-4),
                  "fixtures: basic's vec3 property still evaluates to green baseColor");
        }
    }

    // ------------------------------------------------------------------
    // 3. every shipped preset (re-saved through the migration) loads clean:
    //    a master, zero property nodes, and no 'properties' on re-save
    // ------------------------------------------------------------------
    {
        QStringList files;
        QDirIterator it(QString(JAHSHAKA_TEST_APP_DIR), { "*.effect" }, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) files.append(it.next());
        CHECK(files.size() >= 17, "shipped: found the preset .effect files");

        int bad = 0;
        int textured = 0;
        for (const auto& file : files) {
            auto graph = loadEffect(file);
            if (graph == nullptr || graph->getMasterNode() == nullptr
                || !graph->getNodesByTypeName("property").isEmpty()
                || graph->serialize().contains("properties")) {
                std::printf("      bad preset: %s\n", qPrintable(file));
                ++bad;
                continue;
            }
            // re-saved texture nodes must still carry their image reference
            for (auto node : graph->getNodesByTypeName("texture")) {
                auto stored = node->serializeWidgetValue().toString();
                if (stored.isEmpty()) {
                    std::printf("      empty texture value: %s\n", qPrintable(file));
                    ++bad;
                }
                ++textured;
            }
        }
        CHECK(bad == 0, "shipped: every preset loads with a master, no property nodes, no 'properties' on save");
        CHECK(textured >= 12, "shipped: the textured presets kept their image references");

        // the drawer-synced constants still evaluate to their known values
        auto glass = loadEffect(QString(JAHSHAKA_TEST_APP_DIR) + "glass.effect");
        if (glass) {
            auto result = PbrGraphEvaluator::evaluate(glass, nullptr);
            CHECK(near(result.values["roughness"].toDouble(), 0.05, 1e-3)
                  && near(result.values["alpha"].toDouble(), 0.3, 1e-3),
                  "shipped: glass.effect still folds to the drawer's values after re-save");
        } else {
            CHECK(false, "shipped: glass.effect loads");
        }
    }

    QFile::remove(texPath);
    if (failures == 0) std::printf("ALL OK\n");
    else std::printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
