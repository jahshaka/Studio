// PbrGraphEvaluator characterisation (Option B phase 1): shader graphs built in
// code evaluate to iris::PbrMaterial values and texture paths on the CPU.
//
// No GL, no engine: node widgets exist (the module's nodes are widget-backed)
// but nothing is shown or rendered. QT_QPA_PLATFORM=offscreen.
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPair>
#include <QUuid>
#include <cmath>
#include <cstdio>
#include <string>

#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/nodes/pbrmasternode.h"
#include "modules/materials/nodes/test.h" // FloatNodeModel, ColorPickerNode, TextureNode
#include "modules/materials/models/properties.h"
#include "modules/materials/models/library.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/core/graphbaker.h"
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

// A node-owned texture (§3b: PropertyNode is retired; texture nodes carry
// their image path/guid themselves).
static TextureNode* makeTextureNode(NodeGraph* graph, const QString& path)
{
    auto node = new TextureNode();
    node->setTexturePath(path);
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
        graph->addConnection(cutoff, 0, master, 6); // value -> Alpha Cutoff (socket layout 2)

        auto result = PbrGraphEvaluator::evaluate(graph);
        CHECK(graph->getMasterNode()->typeName == QLatin1String("PbrMaterial"),
              "graph 1: master recognised as PbrMaterial");
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
    // ---- graph 2: texture nodes into the PBR master --------------------------
    // texture node -> Base Color (becomes baseColorMap), texture node -> Normal
    {
        auto graph = new NodeGraph();
        auto master = new PbrMasterNode();
        graph->addNode(master);
        graph->setMasterNode(master);

        auto diffuse = makeTextureNode(graph, texPath);
        auto normal = makeTextureNode(graph, texPath);
        graph->addConnection(diffuse, 0, master, 0); // texture -> Base Color
        graph->addConnection(normal, 0, master, 3);  // texture -> Normal

        auto result = PbrGraphEvaluator::evaluate(graph);
        CHECK(result.unsupportedNodes.isEmpty(), "graph 2: nothing unsupported");
        CHECK(result.values["baseColorMap"].toString() == texPath, "graph 2: baseColorMap path folded");
        CHECK(result.values["normalMap"].toString() == texPath, "graph 2: normalMap path folded");
        CHECK(!result.values.contains("baseColor"), "graph 2: no constant baseColor emitted");

        auto material = PbrGraphEvaluator::createMaterial(graph);
        CHECK(!!material, "graph 2: material created");
        CHECK(material->textures.contains("u_baseColorMap"), "graph 2: u_baseColorMap texture bound");
        CHECK(material->textures.contains("u_normalMap"), "graph 2: u_normalMap texture bound");
        CHECK(material->textures.value("u_baseColorMap")
                  && material->textures.value("u_baseColorMap")->source == texPath,
              "graph 2: bound texture carries the source path");
    }

    // ---- graph 3: animated chain folds at t=0 and is listed ------------------
    // pulsate -> Roughness: since the Materials Evaluator program this folds
    // to sin(0*speed)*0.5+0.5 = 0.5 and the node is named in approximatedNodes
    // (it used to be honest-unsupported; the bake seam now evaluates it).
    {
        auto graph = new NodeGraph();
        auto master = new PbrMasterNode();
        graph->addNode(master);
        graph->setMasterNode(master);

        auto pulsate = new PulsateNode();
        graph->addNode(pulsate);
        graph->addConnection(pulsate, 0, master, 2); // Result -> Roughness

        auto result = PbrGraphEvaluator::evaluate(graph);
        CHECK(result.unsupportedNodes.isEmpty(), "graph 3: pulsate no longer unsupported");
        CHECK(result.values["roughness"].toDouble() == 0.5, "graph 3: pulsate folds to 0.5 at t=0");
        CHECK(result.approximatedNodes.size() == 1
                  && result.approximatedNodes.first().contains("pulsate"),
              "graph 3: pulsate named in approximatedNodes");
        CHECK(result.animated, "graph 3: result carries animated=true");

        auto material = PbrGraphEvaluator::createMaterial(graph);
        CHECK(!!material, "graph 3: material created");
    }

    // ---- graph 4: CONVERT-ON-LOAD (LEGACY-MASTER-CRUD) ----------------------
    //
    // The Blinn-Phong "Surface Material" master is DELETED. A saved graph whose
    // master type is "Material" is not approximated at bake time any more — it
    // is CONVERTED once, when it loads, into a real PBR graph that then re-saves
    // as PBR and that the user can see and edit.
    //
    // These cases build the legacy JSON by hand (the class they describe no
    // longer exists to build one with) and drive the real loader.
    {
        auto legacyGraph = [](const QJsonArray& nodes, const QJsonArray& cons) {
            QJsonObject g;
            g["nodes"] = nodes;
            g["connections"] = cons;
            g["masternode"] = QStringLiteral("m");
            g["materialGuid"] = QString();
            return g;
        };
        auto node = [](const char* id, const char* type, const QJsonValue& value,
                       const char* title = "") {
            QJsonObject n;
            n["id"] = id; n["type"] = type; n["value"] = value;
            n["title"] = title; n["x"] = 0; n["y"] = 0;
            return n;
        };
        auto con = [](const char* left, int leftSock, const char* right, int rightSock) {
            QJsonObject c;
            c["id"] = QUuid::createUuid().toString();
            c["leftNodeId"] = left;   c["leftNodeSocketIndex"] = leftSock;
            c["rightNodeId"] = right; c["rightNodeSocketIndex"] = rightSock;
            return c;
        };
        const QJsonObject master = node("m", "Material", QString(), "Surface Material");

        // (a) a CONSTANT Shininess, the 0-1 gloss convention every shipped
        //     preset used: roughness is its complement, in the graph, visible.
        {
            QJsonArray nodes { master, node("g", "float", 0.15, "Float Property") };
            QJsonArray cons  { con("g", 0, "m", 2) };
            auto* graph = NodeGraph::deserialize(legacyGraph(nodes, cons), new LibraryV1());
            CHECK(graph && graph->getMasterNode(), "convert: a legacy graph loads");
            CHECK(graph && graph->getMasterNode()->typeName == QLatin1String("PbrMaterial"),
                  "convert: its master IS the PBR master");
            CHECK(graph && graph->getMasterNode()->title == QLatin1String("PBR Material"),
                  "convert: titled \"PBR Material\", the only master there is");
            const auto result = PbrGraphEvaluator::evaluate(graph);
            CHECK(near(float(result.values["roughness"].toDouble(-1)), 0.85f),
                  "convert: gloss 0.15 -> Roughness 0.85 (1 - gloss)");
            CHECK(result.unsupportedNodes.isEmpty(),
                  "convert: nothing is reported unsupported — the socket is a real PBR one now");
            CHECK(graph && graph->migrationNotes.size() == 1,
                  "convert: the user gets ONE line saying what happened");
            CHECK(graph && graph->migrationNotes.value(0).contains(QStringLiteral("PBR Material")),
                  "convert: ... and it names the master it converted to");
            // and it RE-SAVES as PBR, which is what stops it converting forever
            CHECK(graph && graph->serialize()["nodes"].toArray().at(0).toObject()["type"]
                              .toString().contains(QStringLiteral("PbrMaterial"))
                  == (graph->serialize()["nodes"].toArray().at(0).toObject()["id"].toString() == "m"),
                  "convert: the re-save carries the PBR master type");
        }

        // (b) THE FLOOR. Legacy gloss 1.0 — the slider's own maximum, the value
        //     every one of the nine presets used to carry — is 1 - 1 = roughness
        //     ZERO, a GGX needle that sparks on any normal-mapped texel. The
        //     conversion lands it on the floor, as a real number in the graph.
        {
            QJsonArray nodes { master, node("g", "float", 1.0, "Float Property") };
            QJsonArray cons  { con("g", 0, "m", 2) };
            auto* graph = NodeGraph::deserialize(legacyGraph(nodes, cons), new LibraryV1());
            const auto result = PbrGraphEvaluator::evaluate(graph);
            CHECK(near(float(result.values["roughness"].toDouble(-1)),
                       float(NodeGraph::kConvertedGlossRoughnessFloor), 1e-4f),
                  "convert: legacy gloss 1.0 lands ON the floor, never on roughness 0");
        }

        // (c) A BLINN EXPONENT (anything above 1) is the other convention that
        //     shared that socket. alpha = sqrt(2/(n+2)), perceptual roughness =
        //     sqrt(alpha): n = 100 is 0.374, a polished surface. The old baker
        //     divided by 100 and sent exactly that value to roughness 0.
        {
            QJsonArray nodes { master, node("g", "float", 100.0, "Float Property") };
            QJsonArray cons  { con("g", 0, "m", 2) };
            auto* graph = NodeGraph::deserialize(legacyGraph(nodes, cons), new LibraryV1());
            const auto result = PbrGraphEvaluator::evaluate(graph);
            const double expected = std::sqrt(std::sqrt(2.0 / 102.0));
            CHECK(near(float(result.values["roughness"].toDouble(-1)), float(expected), 1e-4f),
                  "convert: Blinn exponent 100 -> roughness sqrt(sqrt(2/(n+2))) = 0.374");
        }

        // (d) A TEXTURE-FED Shininess. That socket had no map target at all, so
        //     a gloss map used to be dropped as unsupported; the conversion
        //     splices a real One Minus node in front of Roughness and the map
        //     reaches the renderer for the first time.
        {
            QJsonArray nodes { master, node("t", "texture", QString(texPath), "gloss") };
            QJsonArray cons  { con("t", 0, "m", 2) };
            auto* graph = NodeGraph::deserialize(legacyGraph(nodes, cons), new LibraryV1());
            CHECK(graph && graph->getNodesByTypeName("oneminus").size() == 1,
                  "convert: a gloss MAP gets a One Minus node, in the graph, deletable");
            const auto info = PbrGraphEvaluator::bakeInfo(graph)["perSocket"].toObject();
            CHECK(info["Roughness"].toString() == QLatin1String("baked"),
                  "convert: ... and Roughness bakes instead of being dropped");
        }

        // (e) SPECULAR AND AMBIENT have no PBR equivalent and are dropped —
        //     including the saturated-specular "fake metal" case, because
        //     Metallic 1 + Base Color = the specular colour is a judgement
        //     about the picture that a converter must not make silently. What
        //     it must do is SAY so, which is what is asserted.
        {
            QJsonArray nodes { master,
                               node("s", "color", QJsonObject{{"r",0.9},{"g",0.7},{"b",0.2},{"a",1.0}}, "Specular"),
                               node("a", "color", QJsonObject{{"r",0.1},{"g",0.1},{"b",0.1},{"a",1.0}}, "Ambient") };
            QJsonArray cons  { con("s", 0, "m", 1), con("a", 0, "m", 4) };
            auto* graph = NodeGraph::deserialize(legacyGraph(nodes, cons), new LibraryV1());
            CHECK(graph && graph->connections.isEmpty(),
                  "convert: the Specular and Ambient connections are dropped");
            CHECK(graph && graph->nodes.size() == 3,
                  "convert: ... but the nodes that fed them stay in the graph");
            const QString note = graph ? graph->migrationNotes.value(0) : QString();
            CHECK(note.contains(QStringLiteral("Specular")) && note.contains(QStringLiteral("Ambient")),
                  "convert: the ONE line names both dropped inputs");
            const auto result = PbrGraphEvaluator::evaluate(graph);
            CHECK(near(float(result.values["metallic"].toDouble(0.0)), 0.0f),
                  "convert: a faked-metal specular is NOT guessed into Metallic");
        }

        // (f) EVERY OTHER SOCKET moves by name, not by index: the two masters'
        //     tails do not line up (Emission 5 -> Emissive 4, Alpha 6 -> 5).
        {
            QJsonArray nodes { master,
                               node("d", "color", QJsonObject{{"r",0.2},{"g",0.4},{"b",0.6},{"a",1.0}}, "Diffuse"),
                               node("e", "color", QJsonObject{{"r",1.0},{"g",0.0},{"b",0.0},{"a",1.0}}, "Emission"),
                               node("al", "float", 0.4, "Alpha") };
            QJsonArray cons  { con("d", 0, "m", 0), con("e", 0, "m", 5), con("al", 0, "m", 6) };
            auto* graph = NodeGraph::deserialize(legacyGraph(nodes, cons), new LibraryV1());
            const auto result = PbrGraphEvaluator::evaluate(graph);
            auto base = result.values["baseColor"].toObject();
            CHECK(near(float(base["r"].toDouble()), 0.2f) && near(float(base["g"].toDouble()), 0.4f)
                      && near(float(base["b"].toDouble()), 0.6f),
                  "convert: Diffuse became Base Color");
            auto emis = result.values["emissiveColor"].toObject();
            CHECK(near(float(emis["r"].toDouble()), 1.0f) && near(float(emis["g"].toDouble()), 0.0f),
                  "convert: Emission (socket 5) became Emissive (socket 4)");
            CHECK(near(float(result.values["alpha"].toDouble(-1)), 0.4f),
                  "convert: Alpha (socket 6) became Alpha (socket 5)");
        }
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
            CHECK(glass->getMasterNode()->typeName == QLatin1String("PbrMaterial"), "glass: PBR master");
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
            CHECK(silver->getMasterNode()->typeName == QLatin1String("PbrMaterial"), "silver: PBR master");
            CHECK(near(result.values["metallic"].toDouble(), 1.0), "silver: metallic 1.0");
            CHECK(near(result.values["roughness"].toDouble(), 0.22), "silver: roughness 0.22");
        }

        // gold.effect was the odd one out (hygiene lane, 2026-09-09): it was
        // still authored on the LEGACY Blinn "Surface Material" master, which
        // has no Metallic slot at all, so it baked metallic 0 with the graph's
        // "Specular <- float" listed as an unsupported node — a rough yellow
        // PLASTIC where the drawer offers a metal. Its colour node also carried
        // alpha 0. Re-authored on a PbrMaterial master, the same shape as
        // silver, so this assertion is the same assertion.
        auto gold = loadEffect("gold.effect");
        CHECK(gold && gold->getMasterNode(), "gold.effect deserializes with a master node");
        if (gold) {
            auto result = PbrGraphEvaluator::evaluate(gold);
            CHECK(gold->getMasterNode()->typeName == QLatin1String("PbrMaterial"),
                  "gold: PBR master (was the legacy Blinn one)");
            CHECK(result.unsupportedNodes.isEmpty(), "gold: no unsupported nodes left");
            CHECK(near(result.values["metallic"].toDouble(), 1.0), "gold: metallic 1.0");
            CHECK(near(result.values["roughness"].toDouble(), 0.25), "gold: roughness 0.25");
            auto goldCol = result.values["baseColor"].toObject();
            CHECK(near(goldCol["r"].toDouble(), 1.0) && near(goldCol["g"].toDouble(), 0.85) &&
                      near(goldCol["b"].toDouble(), 0.01),
                  "gold: the authored gold hue survives the re-authoring");
            CHECK(near(goldCol["a"].toDouble(), 1.0), "gold: ... at alpha 1 (it was 0)");
        }

        // THE STORED BLOCK AND THE GRAPH AGREE. A preset ships BOTH halves —
        // the graph and the pre-evaluated `pbrMaterial` block the loader reads
        // without running the evaluator — and gold's two halves disagreeing is
        // how a re-authoring goes half-done.
        for (const char *name : { "gold.effect", "silver.effect" }) {
            QFile f(QString(JAHSHAKA_TEST_APP_DIR) + name);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { CHECK(false, name); continue; }
            const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
            const QJsonObject stored = obj["pbrMaterial"].toObject()["values"].toObject();
            NodeGraph *graph = NodeGraph::deserialize(obj["shadergraph"].toObject(), lib);
            if (!graph) { CHECK(false, name); continue; }
            const auto evaluated = PbrGraphEvaluator::evaluate(graph);
            bool agree = true;
            for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
                const QJsonValue mine = evaluated.values.value(it.key());
                if (it.value().isDouble() && !near(mine.toDouble(), it.value().toDouble()))
                    agree = false;
            }
            CHECK(agree, (QString(name) + ": the stored pbrMaterial block matches what its "
                                          "graph evaluates to").toUtf8().constData());
        }
    }

    // ---- graph 6: EVERY SHIPPED TEMPLATE IS A PBR TEMPLATE -------------------
    //
    // This used to be the "nine legacy presets and the gloss floor" block: they
    // were authored on the Blinn-Phong master and the BAKER approximated them
    // on every bake, forever. They are re-authored now (LEGACY-MASTER-CRUD),
    // and this is the guard that keeps them that way:
    //
    //   * EVERY .effect under app/shadergraph — recursively, so a new one
    //     cannot slip in — loads with a "PbrMaterial" master. A template that
    //     is not PBR is a template a UI user lands on, and until this lane
    //     almost every starter and preset in the Create New dialog was one.
    //   * NOTHING converts on load any more: a shipped file that still needed
    //     the conversion would carry a migration note, and none may.
    //   * Each of the nine textured presets carries the roughness somebody
    //     chose for the material it depicts, the file's pre-evaluated
    //     `pbrMaterial` block agrees with what its graph evaluates to (the two
    //     are read by different paths — the module evaluates the graph, the
    //     drawer reads the block), and nothing is reported unsupported: the
    //     specular maps that had no PBR target are gone rather than dropped
    //     silently on every bake.
    {
        auto lib = new LibraryV1();

        // THE PRESETS' IMAGES MUST BE FINDABLE, or the test measures the wrong
        // thing. Their texture nodes carry APP-RELATIVE names
        // ("materials_to_graph/brick diff.jpg"); TextureNode::deserializeWidget
        // Value only takes the path branch when QFileInfo::exists() says so,
        // and otherwise files the string as an asset guid with no path — which
        // is a texture socket the baker then reads as UNCONNECTED. In the app
        // MaterialHelper::resolveAppRelativeTextures resolves them against the
        // shadergraph asset folder before anything evaluates; here, working
        // from that folder is the same thing with no database.
        const QString cwdBefore = QDir::currentPath();
        QDir::setCurrent(JAHSHAKA_TEST_APP_DIR);

        auto loadFile = [&](const QString& rel) -> QPair<QJsonObject, NodeGraph*> {
            QFile f(rel);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return { QJsonObject(), nullptr };
            const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
            return { obj, NodeGraph::deserialize(obj["shadergraph"].toObject(), lib) };
        };

        // (1) the folder guard — a legacy template can never be re-added
        {
            QStringList files;
            QDirIterator it(QStringLiteral("."), { "*.effect", "*.json" }, QDir::Files,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) files.append(it.next());
            files.sort();
            CHECK(files.size() >= 17, "shipped: found the template files");
            int notPbr = 0, migrated = 0;
            for (const QString& rel : files) {
                auto loaded = loadFile(rel);
                if (!loaded.second || !loaded.second->getMasterNode()
                    || loaded.second->getMasterNode()->typeName != QLatin1String("PbrMaterial")) {
                    std::printf("      NOT a PBR template: %s\n", qPrintable(rel));
                    ++notPbr;
                    continue;
                }
                if (!loaded.second->migrationNotes.isEmpty()) {
                    std::printf("      still converts on load: %s\n", qPrintable(rel));
                    ++migrated;
                }
            }
            CHECK(notPbr == 0, "shipped: EVERY template under app/shadergraph has a PbrMaterial master");
            CHECK(migrated == 0, "shipped: ... and not one of them still needs converting on load");
        }

        // (2) the conversion table, per template, as numbers
        struct Row { const char* file; double roughness; };
        const Row rows[] = {
            { "materials_to_graph/Brick.effect",         0.85 }, // gloss 0.15
            { "materials_to_graph/Stone.effect",         0.85 }, // gloss 0.15
            { "materials_to_graph/Marble tile.effect",   0.35 }, // gloss 0.65
            { "materials_to_graph/Wood.effect",          0.55 }, // gloss 0.45
            { "materials_to_graph/Leather.effect",       0.60 }, // gloss 0.40
            { "materials_to_graph/Painted metal.effect", 0.35 }, // gloss 0.65
            { "materials_to_graph/sand.effect",          0.90 }, // gloss 0.10
            { "materials_to_graph/Grass2.effect",        0.80 }, // gloss 0.20
            { "materials_to_graph/Patchy grass.effect",  0.85 }, // gloss 0.15
            { "gold.effect",                             0.25 }, // already PBR
        };

        for (const Row& row : rows) {
            const std::string name(row.file);
            auto loaded = loadFile(QString(row.file));
            if (!loaded.second || !loaded.second->getMasterNode()) {
                CHECK(false, (name + ": deserializes with a master node").c_str());
                continue;
            }
            const auto result = PbrGraphEvaluator::evaluate(loaded.second);
            const double r = result.values["roughness"].toDouble(-1.0);
            char msg[320];

            std::snprintf(msg, sizeof(msg), "%s: re-authored roughness %.2f (evaluated %.3f)",
                          row.file, row.roughness, r);
            CHECK(near(float(r), float(row.roughness), 0.005f), msg);

            std::snprintf(msg, sizeof(msg),
                          "%s: roughness is at or above the converted-gloss floor %.3f",
                          row.file, NodeGraph::kConvertedGlossRoughnessFloor);
            CHECK(r >= NodeGraph::kConvertedGlossRoughnessFloor - 1e-9, msg);

            // The file's own pre-evaluated block is what the drawer reads
            // without ever touching the graph: the two must not drift.
            const double stored = loaded.first["pbrMaterial"].toObject()["values"]
                                      .toObject()["roughness"].toDouble(-1.0);
            std::snprintf(msg, sizeof(msg),
                          "%s: the stored pbrMaterial block agrees (%.3f vs %.3f)",
                          row.file, stored, r);
            CHECK(near(float(stored), float(r), 0.005f), msg);

            std::snprintf(msg, sizeof(msg),
                          "%s: bakes with NO dropped socket — the specular map is gone, not silent",
                          row.file);
            CHECK(result.unsupportedNodes.isEmpty(), msg);
        }

        QDir::setCurrent(cwdBefore);
    }

    // ---- blend modes (IMAGE_PLANE_SPEC §9): MaterialSettings passes through --
    // The master's Blend Mode is material state: the evaluator lands it on
    // alphaMode (Additive=4, Modulate=5 — 3 is Glass), Opaque keeps the auto
    // rules, and the setting round-trips serialization under the new names
    // (legacy "Blend" still reads as Translucent).
    {
        auto makeGraph = []() {
            auto graph = new NodeGraph();
            auto master = new PbrMasterNode();
            graph->addNode(master);
            graph->setMasterNode(master);
            return graph;
        };

        auto graph = makeGraph();
        auto color = makeColor(graph, 1.0, 0.5, 0.25);
        graph->addConnection(color, 0, graph->masterNode, 0);

        auto result = PbrGraphEvaluator::evaluate(graph);
        CHECK(!result.values.contains("alphaMode") || result.values["alphaMode"].toInt() == 0,
              "blend: Opaque default leaves the auto alpha rules alone");

        const struct { BlendMode mode; int alphaMode; const char* name; } rows[] = {
            { BlendMode::Masked,      1, "Masked -> alphaMode 1" },
            { BlendMode::Translucent, 2, "Translucent -> alphaMode 2" },
            { BlendMode::Additive,    4, "Additive -> alphaMode 4" },
            { BlendMode::Modulate,    5, "Modulate -> alphaMode 5" },
        };
        for (const auto& row : rows) {
            MaterialSettings s = graph->settings;
            s.blendMode = row.mode;
            graph->setMaterialSettings(s);
            result = PbrGraphEvaluator::evaluate(graph);
            CHECK(result.values["alphaMode"].toInt() == row.alphaMode, row.name);
            // material state only: the folded colour is untouched by blend mode
            auto col = result.values["baseColor"].toObject();
            CHECK(near(col["r"].toDouble(), 1.0) && near(col["g"].toDouble(), 0.5),
                  "blend: bake output unaffected by blend mode");
        }

        // an explicit setting overrides the auto rule (cutoff would say Masked)
        auto cutoff = makeFloat(graph, 0.5);
        graph->addConnection(cutoff, 0, graph->masterNode, 6);   // Alpha Cutoff
        MaterialSettings s = graph->settings;
        s.blendMode = BlendMode::Additive;
        graph->setMaterialSettings(s);
        result = PbrGraphEvaluator::evaluate(graph);
        CHECK(result.values["alphaMode"].toInt() == 4,
              "blend: explicit Additive overrides the connected-cutoff auto rule");

        // serialization round-trip: every mode survives serialize/deserialize
        const BlendMode all[] = { BlendMode::Opaque, BlendMode::Masked, BlendMode::Translucent,
                                  BlendMode::Additive, BlendMode::Modulate };
        for (BlendMode mode : all) {
            MaterialSettings ms = graph->settings;
            ms.blendMode = mode;
            graph->setMaterialSettings(ms);
            const QJsonObject obj = graph->serializeMaterialSettings();
            const MaterialSettings back = NodeGraph::deserializeMaterialSettings(obj);
            CHECK(back.blendMode == mode, "blend: mode survives settings serialize round-trip");
        }
        // legacy files: "Blend" (also what Additive wrongly serialized as
        // before this feature) reads as Translucent
        QJsonObject legacy = graph->serializeMaterialSettings();
        legacy["blendMode"] = "Blend";
        CHECK(NodeGraph::deserializeMaterialSettings(legacy).blendMode == BlendMode::Translucent,
              "blend: legacy 'Blend' string reads as Translucent");
    }

    QFile::remove(texPath);
    std::printf(failures ? "FAILED: %d check(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
