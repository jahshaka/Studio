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
#include "modules/materials/models/connectionmodel.h"
#include "modules/materials/models/socketmodel.h"
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
        // RE-ANCHORED (PRESET-UNIFY-1), and the old assertion was the defect.
        // It read "no constant baseColor emitted" — true, and the reason the
        // picture was wrong: baseColor MULTIPLIES baseColorMap, and the
        // material's default is `defaultmaterial::baseColor()` = 200/255 grey,
        // not white. So a graph whose Base Color socket is a texture rendered
        // its own image at 0.784x, silently, in every module material. The
        // evaluator now neutralises that factor exactly as it has always
        // neutralised metallic and roughness when a map fills their slot.
        {
            const auto tint = result.values["baseColor"].toObject();
            CHECK(near(tint["r"].toDouble(), 1.0) && near(tint["g"].toDouble(), 1.0)
                      && near(tint["b"].toDouble(), 1.0),
                  "graph 2: a map on Base Color emits a WHITE tint, so the map is the colour");
        }

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

    // ---- graph 4: REFUSE-ON-LOAD (LEGACY-CONVERT-CRUD) ----------------------
    //
    // The Blinn-Phong "Surface Material" master is DELETED, and so is the
    // socket-by-socket conversion that used to open a graph written on it (the
    // owner, 2026-09-20: "we should have no legacy graphs, remove it"). Such a
    // file is now REFUSED WHOLE: null, with one plain sentence for the user.
    //
    // Refusing, rather than half-loading, is the part worth guarding. A graph
    // whose master never got built loads as a canvas with no master — it bakes
    // nothing, it looks empty, and the next save writes that emptiness over
    // the user's file. Null cannot be mistaken for a material.
    {
        auto graphJson = [](const QJsonArray& nodes, const QJsonArray& cons,
                            const char* masterId) {
            QJsonObject g;
            g["nodes"] = nodes;
            g["connections"] = cons;
            g["masternode"] = QString::fromLatin1(masterId);
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

        // (a) THE DELETED MASTER. A real pre-2026-09-19 graph: Diffuse and a
        //     gloss constant on the Blinn master.
        {
            QJsonArray nodes { node("m", "Material", QString(), "Surface Material"),
                               node("g", "float", 0.15, "Float Property") };
            QJsonArray cons  { con("g", 0, "m", 2) };
            QString reason;
            auto* graph = NodeGraph::deserialize(graphJson(nodes, cons, "m"),
                                                 new LibraryV1(), &reason);
            CHECK(graph == nullptr, "refuse: a legacy \"Surface Material\" graph does NOT load");
            CHECK(!reason.isEmpty(), "refuse: ... and the caller is given a reason to show");
            CHECK(reason.contains(QStringLiteral("Surface Material")),
                  "refuse: the reason NAMES the node the file was written on");
            CHECK(reason.contains(QStringLiteral("PBR Material")),
                  "refuse: ... and says what to do instead");
        }

        // (b) ANY OTHER master type the same way — a file from a build that
        //     had a master this one does not. Nothing is guessed.
        {
            QJsonArray nodes { node("m", "SomeFutureMaster", QString(), "") };
            QString reason;
            auto* graph = NodeGraph::deserialize(graphJson(nodes, QJsonArray{}, "m"),
                                                 new LibraryV1(), &reason);
            CHECK(graph == nullptr, "refuse: an unknown master type does not load either");
            CHECK(reason.contains(QStringLiteral("SomeFutureMaster")),
                  "refuse: ... and the reason names it");
        }

        // (c) THE PBR MASTER STILL LOADS, and the reason is cleared: the
        //     refusal must not be a new way for ordinary graphs to fail.
        {
            QJsonArray nodes { node("m", "PbrMaterial", QString(), ""),
                               node("r", "float", 0.35, "Roughness") };
            QJsonArray cons  { con("r", 0, "m", 2) };
            QString reason = QStringLiteral("stale");
            auto* graph = NodeGraph::deserialize(graphJson(nodes, cons, "m"),
                                                 new LibraryV1(), &reason);
            CHECK(graph && graph->getMasterNode()
                      && graph->getMasterNode()->typeName == QLatin1String("PbrMaterial"),
                  "refuse: a PBR graph loads exactly as before");
            CHECK(reason.isEmpty(), "refuse: ... with no reason set");
            const auto result = PbrGraphEvaluator::evaluate(graph);
            CHECK(near(float(result.values["roughness"].toDouble(-1)), 0.35f),
                  "refuse: ... and its values are untouched by the guard");
            CHECK(graph && graph->migrationNotes.isEmpty(),
                  "refuse: a PBR graph carries no migration note");
        }

        // (d) AN UNKNOWN NON-MASTER NODE IS STILL ONLY SKIPPED. The refusal is
        //     about the master and nothing else: the load-time renames and the
        //     skip-unknown-node rule are untouched (LEGACY-CONVERT-CRUD brief).
        {
            QJsonArray nodes { node("m", "PbrMaterial", QString(), ""),
                               node("x", "no-such-node", QString(), "") };
            QString reason;
            auto* graph = NodeGraph::deserialize(graphJson(nodes, QJsonArray{}, "m"),
                                                 new LibraryV1(), &reason);
            CHECK(graph != nullptr && graph->nodes.size() == 1,
                  "refuse: an unknown ORDINARY node is skipped, not refused");
            CHECK(reason.isEmpty(), "refuse: ... and sets no reason");
        }
    }

    // ---- the shipped presets: ONE list, and every one of them is a graph ----
    //
    // PRESET-UNIFY-1 (the owner, 2026-09-20: "the old presets should be gone
    // and we should only have the new ones, we dont need duplicates"). There
    // used to be TWO shipped families and three arms here to guard them: the
    // `.effect` graph TEMPLATES under app/shadergraph/ (a folder guard, a
    // per-template roughness table, and a "the stored pbrMaterial block agrees
    // with the graph" check) and, separately, the `.material` presets the
    // editor's tray applies. Fifteen of them were the same material twice.
    //
    // There is one set now: `app/content/materials/<name>.material` authors the
    // preset's VALUES and names the graph beside it in `graphs/`. So there is
    // one guard, and it is the one that matters — THE TWO HALVES OF EACH
    // PRESET DESCRIBE THE SAME MATERIAL. A graph that disagrees with the
    // values beside it is a preset that changes the moment a user customises
    // it and saves, which is exactly the drift the old "stored block agrees"
    // check existed to catch, asked of the pair that actually ships.
    {
        auto lib = new LibraryV1();

        // The graphs name their images RELATIVE to the preset file that owns
        // them — the same spelling as the map slot beside them — so
        // evaluating one means working from the PRESET folder:
        // TextureNode::deserializeWidgetValue takes the path branch only when
        // QFileInfo::exists() says so, and files anything else as an asset
        // guid with no path — a socket the baker then reads as UNCONNECTED.
        // (In the app, MaterialHelper::resolveAppRelativeTextures imports them
        // first; here, the current directory is the same thing with no
        // database.)
        const QString cwdBefore = QDir::currentPath();
        const QString presetDir = QString(JAHSHAKA_TEST_PRESET_DIR);
        QDir::setCurrent(presetDir);

        QStringList presetFiles;
        {
            QDirIterator it(presetDir, { "*.material" }, QDir::Files);
            while (it.hasNext()) presetFiles.append(it.next());
            presetFiles.sort();
        }
        CHECK(presetFiles.size() == 20,
              "shipped: twenty presets, and they are the WHOLE shipped set");

        int noGraph = 0, notPbr = 0, migrated = 0, unsupported = 0, disagreed = 0;
        int texturedSockets = 0, bakedSockets = 0;
        for (const QString &file : presetFiles) {
            QFile mf(file);
            if (!mf.open(QIODevice::ReadOnly | QIODevice::Text)) { ++noGraph; continue; }
            const QJsonObject preset = QJsonDocument::fromJson(mf.readAll()).object();
            const QString name = preset["name"].toString();
            const QString graphRel = preset["graph"].toString();
            if (graphRel.isEmpty()) {
                std::printf("      no graph: %s\n", qPrintable(name));
                ++noGraph;
                continue;
            }
            QFile gf(presetDir + graphRel);
            if (!gf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                std::printf("      graph missing: %s\n", qPrintable(graphRel));
                ++noGraph;
                continue;
            }
            const QJsonObject effect = QJsonDocument::fromJson(gf.readAll()).object();
            NodeGraph *graph = NodeGraph::deserialize(effect["shadergraph"].toObject(), lib);
            if (!graph || !graph->getMasterNode()
                || graph->getMasterNode()->typeName != QLatin1String("PbrMaterial")) {
                std::printf("      NOT a PBR graph: %s\n", qPrintable(name));
                ++notPbr;
                continue;
            }
            if (!graph->migrationNotes.isEmpty()) {
                std::printf("      still migrates on load: %s\n", qPrintable(name));
                ++migrated;
            }
            const auto result = PbrGraphEvaluator::evaluate(graph);
            if (!result.unsupportedNodes.isEmpty()) {
                std::printf("      unsupported in %s: %s\n", qPrintable(name),
                            qPrintable(result.unsupportedNodes.join(", ")));
                ++unsupported;
            }

            // THE TWO HALVES AGREE, on every row the evaluator can produce.
            const auto say = [&](const char *key, double mine, double authored) {
                std::printf("      %s: graph %s=%.3f but the preset authors %.3f\n",
                            qPrintable(name), key, mine, authored);
                ++disagreed;
            };
            const bool hasBaseMap = !preset["baseColorMap"].toString().isEmpty();
            const bool hasRoughMap = !preset["roughnessMap"].toString().isEmpty();
            if (!hasBaseMap && result.values.contains("baseColor")) {
                QColor authored;
                authored.setNamedColor(preset["baseColor"].toString("#FFFFFF"));
                const auto col = result.values["baseColor"].toObject();
                if (!near(col["r"].toDouble(), authored.redF(), 0.004)
                    || !near(col["g"].toDouble(), authored.greenF(), 0.004)
                    || !near(col["b"].toDouble(), authored.blueF(), 0.004))
                    say("baseColor", col["r"].toDouble(), authored.redF());
            }
            if (result.values.contains("metallic")
                && !near(result.values["metallic"].toDouble(), preset["metallic"].toDouble(0.0), 0.004))
                say("metallic", result.values["metallic"].toDouble(), preset["metallic"].toDouble(0.0));
            if (!hasRoughMap && result.values.contains("roughness")
                && !near(result.values["roughness"].toDouble(), preset["roughness"].toDouble(0.5), 0.004))
                say("roughness", result.values["roughness"].toDouble(), preset["roughness"].toDouble(0.5));
            if (result.values.contains("alpha")
                && !near(result.values["alpha"].toDouble(), preset["alpha"].toDouble(1.0), 0.004))
                say("alpha", result.values["alpha"].toDouble(), preset["alpha"].toDouble(1.0));
            // THE TILING (fix round). It was the gap this arm did not look
            // through: a textured preset authored at 4x whose graph said
            // nothing about tiling passed every check here and then lost the
            // tiling the first time a customised copy was saved, because the
            // UV rows are the GRAPH's whenever it has a texture node.
            {
                const double authored = preset["textureScale"].toDouble(1.0);
                double folded = 1.0;
                const QJsonValue scale = result.values.value("textureScale");
                if (scale.isArray()) folded = scale.toArray().at(0).toDouble(1.0);
                else if (scale.isDouble()) folded = scale.toDouble(1.0);
                const bool textured = !preset["baseColorMap"].toString().isEmpty()
                                      || !preset["roughnessMap"].toString().isEmpty()
                                      || !preset["normalMap"].toString().isEmpty();
                if (textured && !near(folded, authored, 0.004))
                    say("textureScale", folded, authored);
            }

            // A MAP THE PRESET AUTHORS IS A TEXTURE NODE IN ITS GRAPH, and it
            // PASSES THROUGH: a preset that resampled its own image into a
            // baked map would look the same at a glance and be permanently
            // lower resolution.
            const auto info = PbrGraphEvaluator::bakeInfo(graph, nullptr)["perSocket"].toObject();
            for (auto s = info.begin(); s != info.end(); ++s) {
                if (s.value().toString() == "passthrough") ++texturedSockets;
                if (s.value().toString() != "baked") continue;
                std::printf("      %s: %s resamples\n", qPrintable(name), qPrintable(s.key()));
                ++bakedSockets;
            }
            if (hasBaseMap && !preset["normalMap"].toString().isEmpty()
                && graph->getNodesByTypeName("texture").size() < 2) {
                std::printf("      %s: fewer texture nodes than authored maps\n", qPrintable(name));
                ++disagreed;
            }
            // A ROUGHNESS-MAPPED PRESET WIRES ITS MAP INTO ROUGHNESS (fix
            // round). The value check above SKIPS roughness for these nine,
            // because the authored value is the map's factor and not a number
            // the graph folds — so without this the nine could have lost the
            // map entirely and the arm would still have been green, leaving
            // `roughnessLowerBound`/`roughnessUpperBound` with nothing to
            // remap and a specular map's pixels nowhere.
            if (hasRoughMap) {
                bool wired = false;
                NodeModel *master = graph->getMasterNode();
                SocketModel *roughIn = master && master->inSockets.size() > 2
                                           ? master->inSockets[2] : nullptr;   // Roughness
                for (auto *c : graph->connections.values()) {
                    if (!c || !roughIn || c->rightSocket != roughIn) continue;
                    if (c->leftSocket && c->leftSocket->node
                        && c->leftSocket->node->typeName == QLatin1String("texture"))
                        wired = true;
                }
                if (!wired) {
                    std::printf("      %s: its roughness MAP is not wired into Roughness\n",
                                qPrintable(name));
                    ++disagreed;
                }
            }
        }
        CHECK(noGraph == 0, "shipped: EVERY preset names a graph, and it is there");
        CHECK(notPbr == 0, "shipped: ... every one of them has a PbrMaterial master");
        CHECK(migrated == 0, "shipped: ... and not one still needs migrating on load");
        CHECK(unsupported == 0, "shipped: ... nothing in them evaluates unsupported");
        CHECK(disagreed == 0,
              "shipped: a preset's GRAPH and its authored VALUES describe the same material");
        CHECK(bakedSockets == 0, "shipped: no preset socket resamples — Passthrough unchanged");
        CHECK(texturedSockets >= 30, "shipped: the textured presets DO pass their images through");

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
