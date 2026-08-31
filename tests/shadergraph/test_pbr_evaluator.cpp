// PbrGraphEvaluator characterisation (Option B phase 1): shader graphs built in
// code evaluate to iris::PbrMaterial values and texture paths on the CPU.
//
// No GL, no engine: node widgets exist (the module's nodes are widget-backed)
// but nothing is shown or rendered. QT_QPA_PLATFORM=offscreen.
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>
#include <cstdio>

#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/nodes/pbrmasternode.h"
#include "modules/materials/nodes/test.h" // FloatNodeModel, ColorPickerNode, PropertyNode, SurfaceMasterNode
#include "modules/materials/models/properties.h"
#include "modules/materials/models/library.h"
#include "modules/materials/core/pbrgraphevaluator.h"

#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/assets/texture2d.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }
static bool nearColor(const QColor& c, float r, float g, float b) {
    return near(c.redF(), r) && near(c.greenF(), g) && near(c.blueF(), b);
}

// A float constant node set through the same path the editor uses.
static FloatNodeModel* makeFloat(NodeGraph* graph, double value)
{
    auto node = new FloatNodeModel();
    node->deserializeWidgetValue(QJsonValue(value));
    graph->addNode(node);
    return node;
}

static ColorPickerNode* makeColor(NodeGraph* graph, double r, double g, double b, double a = 1.0)
{
    auto node = new ColorPickerNode();
    QJsonObject obj;
    obj["r"] = r; obj["g"] = g; obj["b"] = b; obj["a"] = a;
    // the override is private; the NodeModel interface is the public route
    static_cast<NodeModel*>(node)->deserializeWidgetValue(obj);
    graph->addNode(node);
    return node;
}

static PropertyNode* makeTextureProperty(NodeGraph* graph, const QString& name, const QString& path)
{
    auto prop = new TextureProperty();
    prop->displayName = name;
    prop->name = name;
    prop->setValue(path);
    graph->addProperty(prop);

    auto node = new PropertyNode();
    node->setProperty(prop);
    graph->addNode(node);
    return node;
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv); // widget-backed nodes need a QApplication

    // a real image on disk, so the material's texture load succeeds headlessly
    const QString texPath = QDir::current().absoluteFilePath("test_pbr_basecolor.png");
    {
        QImage img(4, 4, QImage::Format_RGBA8888);
        img.fill(QColor(255, 128, 0));
        CHECK(img.save(texPath), "test texture written to disk");
    }

    // ---- graph 1: constants into the PBR master ------------------------------
    // color -> Base Color, float -> Roughness, float -> Alpha Cutoff
    {
        auto graph = new NodeGraph();
        auto master = new PbrMasterNode();
        graph->addNode(master);
        graph->setMasterNode(master);

        auto color = makeColor(graph, 1.0, 0.5, 0.25);
        auto rough = makeFloat(graph, 0.25);
        auto cutoff = makeFloat(graph, 0.5);
        graph->addConnection(color, 0, master, 0);  // RGBA -> Base Color
        graph->addConnection(rough, 0, master, 2);  // value -> Roughness
        graph->addConnection(cutoff, 0, master, 7); // value -> Alpha Cutoff

        auto result = PbrGraphEvaluator::evaluate(graph);
        CHECK(result.hasPbrMaster, "graph 1: master recognised as PbrMaterial");
        CHECK(result.unsupportedNodes.isEmpty(), "graph 1: nothing unsupported");
        CHECK(near(result.values["roughness"].toDouble(), 0.25), "graph 1: roughness value folded");
        CHECK(near(result.values["alphaCutoff"].toDouble(), 0.5), "graph 1: alphaCutoff folded");
        CHECK(result.values["alphaMode"].toInt() == 1, "graph 1: connected cutoff implies cutout alphaMode");
        auto col = result.values["baseColor"].toObject();
        CHECK(near(col["r"].toDouble(), 1.0) && near(col["g"].toDouble(), 0.5) && near(col["b"].toDouble(), 0.25),
              "graph 1: baseColor folded from color node");

        auto material = PbrGraphEvaluator::createMaterial(graph);
        CHECK(!!material, "graph 1: material created");
        CHECK(nearColor(material->baseColor, 1.0f, 0.5f, 0.25f), "graph 1: PbrMaterial.baseColor set");
        CHECK(near(material->roughnessFactor, 0.25f), "graph 1: PbrMaterial.roughnessFactor set");
        CHECK(near(material->alphaCutoff, 0.5f), "graph 1: PbrMaterial.alphaCutoff set");
        CHECK(material->alphaMode == 1, "graph 1: PbrMaterial.alphaMode is cutout");
        CHECK(material->textures.isEmpty(), "graph 1: no maps bound");
    }

    // ---- graph 1b: evaluator numeric contract (audit D5 + D6) ----------------
    // vector2 folds like its siblings; FloatSlot landings clamp to [0,1].
    {
        auto graph = new NodeGraph();
        auto master = new PbrMasterNode();
        graph->addNode(master);
        graph->setMasterNode(master);

        auto vec2 = new Vector2Node();
        QJsonObject v2; v2["x"] = 0.5; v2["y"] = 0.25;
        static_cast<NodeModel*>(vec2)->deserializeWidgetValue(v2);
        graph->addNode(vec2);
        graph->addConnection(vec2, 0, master, 0);   // Result -> Base Color

        auto hot = makeFloat(graph, 1.5);
        graph->addConnection(hot, 0, master, 1);    // 1.5 -> Metallic

        auto cold = makeFloat(graph, -0.5);
        graph->addConnection(cold, 0, master, 2);   // -0.5 -> Roughness

        auto result = PbrGraphEvaluator::evaluate(graph);
        CHECK(result.unsupportedNodes.isEmpty(), "graph 1b: vector2 no longer unsupported (D5)");
        auto col = result.values["baseColor"].toObject();
        CHECK(near(col["r"].toDouble(), 0.5) && near(col["g"].toDouble(), 0.25) && near(col["b"].toDouble(), 0.0),
              "graph 1b: vector2 folds to (x, y, 0) on a color slot (D5)");
        CHECK(result.values["metallic"].toDouble() == 1.0, "graph 1b: float(1.5) -> Metallic clamps to 1.0 (D6)");
        CHECK(result.values["roughness"].toDouble() == 0.0, "graph 1b: float(-0.5) -> Roughness clamps to 0.0 (D6)");

        auto material = PbrGraphEvaluator::createMaterial(graph);
        CHECK(!!material && near(material->metallicFactor, 1.0f),
              "graph 1b: clamped metallic lands on PbrMaterial");
    }

    // ---- graph 2: texture properties into the PBR master ---------------------
    // texture property -> Base Color (becomes baseColorMap), texture property -> Normal
    {
        auto graph = new NodeGraph();
        auto master = new PbrMasterNode();
        graph->addNode(master);
        graph->setMasterNode(master);

        auto diffuse = makeTextureProperty(graph, "diffuseTex", texPath);
        auto normal = makeTextureProperty(graph, "normalTex", texPath);
        graph->addConnection(diffuse, 0, master, 0); // texture -> Base Color
        graph->addConnection(normal, 0, master, 3);  // texture -> Normal

        auto result = PbrGraphEvaluator::evaluate(graph);
        CHECK(result.unsupportedNodes.isEmpty(), "graph 2: nothing unsupported");
        CHECK(result.values["baseColorMap"].toString() == texPath, "graph 2: baseColorMap path folded");
        CHECK(result.values["normalMap"].toString() == texPath, "graph 2: normalMap path folded");
        CHECK(!result.values.contains("baseColor"), "graph 2: no constant baseColor emitted");

        auto material = PbrGraphEvaluator::createMaterial(graph);
        CHECK(!!material, "graph 2: material created");
        CHECK(material->useBaseColorMap, "graph 2: PbrMaterial uses base color map");
        CHECK(material->textures.contains("u_baseColorMap"), "graph 2: u_baseColorMap texture bound");
        CHECK(material->textures.contains("u_normalMap"), "graph 2: u_normalMap texture bound");
        CHECK(material->textures.value("u_baseColorMap")
                  && material->textures.value("u_baseColorMap")->source == texPath,
              "graph 2: bound texture carries the source path");
    }

    // ---- graph 3: unsupported chain falls back, does not crash ---------------
    // pulsate (animated, non-bakeable) -> Roughness
    {
        auto graph = new NodeGraph();
        auto master = new PbrMasterNode();
        graph->addNode(master);
        graph->setMasterNode(master);

        auto pulsate = new PulsateNode();
        graph->addNode(pulsate);
        graph->addConnection(pulsate, 0, master, 2); // Result -> Roughness

        auto result = PbrGraphEvaluator::evaluate(graph);
        CHECK(result.unsupportedNodes.size() == 1, "graph 3: pulsate reported unsupported");
        CHECK(!result.unsupportedNodes.isEmpty()
                  && result.unsupportedNodes.first().contains("pulsate"),
              "graph 3: report names the node type");
        CHECK(!result.values.contains("roughness"), "graph 3: no roughness emitted");

        auto material = PbrGraphEvaluator::createMaterial(graph);
        CHECK(!!material, "graph 3: material still created (defaults)");
    }

    // ---- graph 4: legacy Surface master approximates onto PBR keys -----------
    // color -> Diffuse (the shape of the 15 shipped presets' masters)
    {
        auto graph = new NodeGraph();
        auto master = new SurfaceMasterNode();
        graph->addNode(master);
        graph->setMasterNode(master);

        auto color = makeColor(graph, 0.2, 0.4, 0.6);
        graph->addConnection(color, 0, master, 0); // RGBA -> Diffuse

        auto result = PbrGraphEvaluator::evaluate(graph);
        CHECK(!result.hasPbrMaster, "graph 4: legacy master recognised");
        auto col = result.values["baseColor"].toObject();
        CHECK(near(col["r"].toDouble(), 0.2) && near(col["g"].toDouble(), 0.4) && near(col["b"].toDouble(), 0.6),
              "graph 4: Diffuse folded to baseColor");
    }

    // ---- graph 5: the shipped Glass/Silver preset graphs (drawer sync) -------
    // app/shadergraph/{glass,silver}.effect are the module-side siblings of the
    // drawer's Glass PBR / Silver PBR materials: they must deserialize through
    // the real loader path and fold to the drawer's values.
    {
        auto lib = new NodeLibrary();
        lib->addNode("float", "Float", QIcon(), NodeCategory::Constants,
                     []() -> NodeModel * { return new FloatNodeModel(); });
        lib->addNode("color", "Color", QIcon(), NodeCategory::Constants,
                     []() -> NodeModel * { return new ColorPickerNode(); });

        auto loadEffect = [&](const char *name) -> NodeGraph * {
            QFile f(QString(JAHSHAKA_TEST_APP_DIR) + name);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return nullptr;
            const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
            return NodeGraph::deserialize(obj["shadergraph"].toObject(), lib);
        };

        auto glass = loadEffect("glass.effect");
        CHECK(glass && glass->getMasterNode(), "glass.effect deserializes with a master node");
        if (glass) {
            auto result = PbrGraphEvaluator::evaluate(glass);
            CHECK(result.hasPbrMaster, "glass: PBR master");
            auto col = result.values["baseColor"].toObject();
            CHECK(near(col["r"].toDouble(), 0.933) && near(col["g"].toDouble(), 0.957) && near(col["b"].toDouble(), 0.973),
                  "glass: base colour matches the drawer's Glass PBR");
            CHECK(near(result.values["roughness"].toDouble(), 0.05), "glass: roughness 0.05");
            CHECK(near(result.values["alpha"].toDouble(), 0.3), "glass: alpha 0.3");
        }

        auto silver = loadEffect("silver.effect");
        CHECK(silver && silver->getMasterNode(), "silver.effect deserializes with a master node");
        if (silver) {
            auto result = PbrGraphEvaluator::evaluate(silver);
            CHECK(result.hasPbrMaster, "silver: PBR master");
            CHECK(near(result.values["metallic"].toDouble(), 1.0), "silver: metallic 1.0");
            CHECK(near(result.values["roughness"].toDouble(), 0.22), "silver: roughness 0.22");
        }
    }

    QFile::remove(texPath);
    std::printf(failures ? "FAILED: %d check(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
