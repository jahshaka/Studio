// shadergraph.panel_migration: old-format graph JSON (graph["properties"] +
// PropertyNode instances, the deleted master) is REFUSED — there is no
// migration (FORWARD-ONLY-1) — and the shipped presets load clean.
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
    // 1. a synthetic pre-§3b graph (graph-global properties, PropertyNode
    //    instances, a texCoords feed) is REFUSED (FORWARD-ONLY-1).
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

        // FORWARD-ONLY-1: the §3b migration is DELETED. A pre-§3b file carries
        // no socket layout, so it is refused whole, with a reason.
        QString reason;
        auto graph = NodeGraph::deserialize(graphObj, new LibraryV1(), &reason);
        CHECK(graph == nullptr, "old format: a pre-§3b graph is REFUSED, not migrated");
        CHECK(reason.contains(QStringLiteral("older version")), "old format: the refusal says why");

        // Even stamped with today's layout, the retired shapes are not nodes:
        // 'property' and 'texCoords' are unknown types and are skipped, and the
        // graph-global 'properties' array is neither read nor written.
        graphObj["socketLayout"] = NodeGraph::kSocketLayoutVersion;
        graph = NodeGraph::deserialize(graphObj, new LibraryV1(), &reason);
        CHECK(graph && graph->getMasterNode() && graph->nodes.size() == 1,
              "retired shapes: only the master loads (no PropertyNode / texCoords conversion)");
        CHECK(graph && !graph->serialize().contains("properties"),
              "retired shapes: a save writes no 'properties'");
    }

    // ------------------------------------------------------------------
    // 2. real old-format fixtures (pre-migration snapshots of the shipped
    //    presets): every one is written on the DELETED master, so every one
    //    is refused
    // ------------------------------------------------------------------
    auto loadEffect = [](const QString& path, QString* reason = nullptr) -> NodeGraph* {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return nullptr;
        auto obj = QJsonDocument::fromJson(f.readAll()).object();
        if (!obj.contains("shadergraph")) return nullptr;
        return NodeGraph::deserialize(obj["shadergraph"].toObject(), new LibraryV1(), reason);
    };

    {
        // THE THREE REAL OLD-FORMAT FIXTURES ARE REFUSED (LEGACY-CONVERT-CRUD,
        // 2026-09-20). They are genuine pre-migration snapshots of the shipped
        // presets and every one of them is written on the deleted Blinn-Phong
        // "Surface Material" master, so they are now exactly the evidence that
        // a legacy file cannot be opened: null, with a sentence naming the
        // node and what to do instead.

        const char* kFixtures[] = { "checker_oldformat.effect",
                                    "brick_oldformat.effect",
                                    "basic_oldformat.effect" };
        int refused = 0, named = 0;
        for (const char* name : kFixtures) {
            QString reason;
            auto* graph = loadEffect(QString(JAHSHAKA_TEST_FIXTURE_DIR) + name, &reason);
            if (graph == nullptr) ++refused; else std::printf("      loaded: %s\n", name);
            if (reason.contains(QStringLiteral("Surface Material"))
                && reason.contains(QStringLiteral("PBR Material")))
                ++named;
        }
        CHECK(refused == 3, "fixtures: all three legacy-master snapshots are REFUSED, not converted");
        CHECK(named == 3, "fixtures: each refusal names the removed node and the one to use");
    }

    // ------------------------------------------------------------------
    // 3. every shipped preset loads clean:
    //    a master, zero property nodes, and no 'properties' on re-save
    // ------------------------------------------------------------------
    {
        // ONE SHIPPED SET (PRESET-UNIFY-1) — see test_pbr_evaluator for the
        // guard that a preset's graph and its authored values agree.
        const QString graphDir = QString(JAHSHAKA_TEST_PRESET_DIR) + "graphs/";
        QStringList files;
        QDirIterator it(graphDir, { "*.effect" }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files.append(it.next());
        CHECK(files.size() == 20, "shipped: found the twenty preset graphs");

        int bad = 0;
        int textured = 0;
        int legacyMaster = 0;
        for (const auto& file : files) {
            QString reason;
            auto graph = loadEffect(file, &reason);
            // THE GUARD (LEGACY-MASTER-CRUD, sharpened by LEGACY-CONVERT-CRUD):
            // every shipped preset is authored on the ONE master. A legacy one
            // re-added here would now be REFUSED — a preset tile a user clicks
            // and cannot open — so the refusal is what this counts, by its
            // reason, and it is a shipping defect, not a user's old file.
            if (graph == nullptr && !reason.isEmpty()) {
                std::printf("      NOT a PBR preset: %s (%s)\n",
                            qPrintable(file), qPrintable(reason));
                ++legacyMaster;
            }
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
        CHECK(legacyMaster == 0,
              "shipped: every preset's master is \"PBR Material\" — none is refused on load");
        CHECK(textured >= 25, "shipped: the textured presets kept their image references");

        // the drawer-synced constants still evaluate to their known values
        auto glass = loadEffect(graphDir + "Glass-pbr.effect");
        if (glass) {
            auto result = PbrGraphEvaluator::evaluate(glass, nullptr);
            CHECK(near(result.values["roughness"].toDouble(), 0.05, 1e-3)
                  && near(result.values["alpha"].toDouble(), 0.3, 1e-3),
                  "shipped: Glass PBR's graph still folds to the drawer's values after re-save");
        } else {
            CHECK(false, "shipped: Glass PBR's graph loads");
        }
    }

    QFile::remove(texPath);
    if (failures == 0) std::printf("ALL OK\n");
    else std::printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
