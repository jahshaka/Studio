// §3a selection suite (MATERIALS_EVALUATOR_SPEC §6, shadergraph.selection):
// GraphNodeScene's nodeSelected signal + selectNodeById/selectedNodeId/
// deselectAll (the machinery behind graph.selectNode/selectedNode/deselect),
// and NodePropertiesPanel behavior — the selected node's values in the panel,
// panel edits flowing through deserializeWidgetValue -> valueChanged ->
// graphInvalidated (one write path), inline widgets and panel staying in
// step, master -> material settings view, empty -> graph settings view.
//
// No GL, no engine. QT_QPA_PLATFORM=offscreen.
#include <QApplication>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>
#include <cstdio>

#include "modules/materials/graph/graphnodescene.h"
#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/library.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/nodes/pbrmasternode.h"
#include "modules/materials/nodes/test.h"
#include "modules/materials/widgets/nodepropertiespanel.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

// the panel's editor boxes on the currently shown stack page
template <typename T>
static QList<T*> visibleBoxes(NodePropertiesPanel* panel)
{
    QList<T*> out;
    for (auto box : panel->findChildren<T*>())
        if (box->isVisibleTo(panel))
            out.append(box);
    return out;
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    auto graph = new NodeGraph;
    graph->setNodeLibrary(new LibraryV1());
    auto master = new PbrMasterNode();
    graph->addNode(master);
    graph->setMasterNode(master);

    auto floatNode = graph->library->createNode("float");
    floatNode->deserializeWidgetValue(QJsonValue(0.5));
    graph->addNode(floatNode);

    auto vecNode = graph->library->createNode("vector3");
    {
        QJsonObject v; v["x"] = 0.1; v["y"] = 0.2; v["z"] = 0.3;
        vecNode->deserializeWidgetValue(v);
    }
    graph->addNode(vecNode);

    auto texNode = graph->library->createNode("texture");
    graph->addNode(texNode);

    auto scene = new GraphNodeScene(nullptr);
    scene->setNodeGraph(graph);

    auto panel = new NodePropertiesPanel;
    panel->setGraph(graph);
    panel->setScene(scene);
    panel->resize(320, 600);
    panel->show(); // offscreen: gives stacked-page visibility real values

    // ---- selection signal + programmatic selection ----
    NodeModel* lastSelected = nullptr;
    int selectedEmits = 0;
    QObject::connect(scene, &GraphNodeScene::nodeSelected, [&](NodeModel* model) {
        lastSelected = model;
        ++selectedEmits;
    });
    int invalidations = 0;
    QObject::connect(scene, &GraphNodeScene::graphInvalidated, [&]() { ++invalidations; });

    CHECK(panel->currentView() == NodePropertiesPanel::View::GraphSettings,
          "panel starts on the graph settings view");
    CHECK(scene->selectedNodeId().isEmpty(), "nothing selected initially");

    CHECK(scene->selectNodeById(floatNode->id), "selectNodeById finds the float node");
    CHECK(lastSelected == floatNode && selectedEmits >= 1,
          "nodeSelected carried the float node's model");
    CHECK(scene->selectedNodeId() == floatNode->id, "selectedNodeId round-trips");
    CHECK(panel->currentNode() == floatNode, "panel follows the selection");
    CHECK(panel->currentView() == NodePropertiesPanel::View::Node,
          "panel shows the node editor view");
    CHECK(!scene->selectNodeById("no-such-id"), "selectNodeById rejects unknown ids");

    // ---- the float editor shows THAT node's value; edits write through ----
    {
        auto boxes = visibleBoxes<QDoubleSpinBox>(panel);
        CHECK(boxes.size() == 1, "float node: one number box in the panel");
        if (boxes.size() == 1) {
            CHECK(near(boxes[0]->value(), 0.5), "float node: panel shows the node's value");

            invalidations = 0;
            boxes[0]->setValue(0.42);
            CHECK(near(floatNode->serializeWidgetValue().toDouble(), 0.42),
                  "panel edit lands on the node through deserializeWidgetValue");
            CHECK(invalidations == 1,
                  "panel edit invalidates the graph exactly once (one write path)");

            // the node's inline title-bar editor is the same value
            // the float editor moved from the title bar into the node body
            // (owner request, phase 5): it is `widget` now, not `headerWidget`
            auto inline_ = qobject_cast<QDoubleSpinBox*>(floatNode->widget);
            CHECK(inline_ != nullptr && near(inline_->value(), 0.42),
                  "the inline node editor mirrors the panel edit");

            // and an inline edit flows back into the panel
            if (inline_ != nullptr) {
                inline_->setValue(0.9);
                CHECK(near(boxes[0]->value(), 0.9),
                      "an inline edit refreshes the panel editor");
                CHECK(near(floatNode->serializeWidgetValue().toDouble(), 0.9),
                      "the inline edit reached the node value");
            }
        }
    }

    // ---- vector3 ----
    CHECK(scene->selectNodeById(vecNode->id), "selectNodeById finds the vector3 node");
    CHECK(panel->currentNode() == vecNode, "panel switched to the vector3 node");
    {
        auto boxes = visibleBoxes<QDoubleSpinBox>(panel);
        CHECK(boxes.size() == 3, "vector3 node: three component boxes");
        if (boxes.size() == 3) {
            CHECK(near(boxes[0]->value(), 0.1) && near(boxes[1]->value(), 0.2)
                  && near(boxes[2]->value(), 0.3),
                  "vector3 node: panel shows the components");
            invalidations = 0;
            boxes[2]->setValue(0.75);
            auto v = vecNode->serializeWidgetValue().toObject();
            CHECK(near(v["z"].toDouble(), 0.75) && near(v["x"].toDouble(), 0.1),
                  "vector3 panel edit writes z and leaves x alone");
            CHECK(invalidations == 1, "vector3 edit invalidates exactly once");
        }
    }

    // ---- texture node view ----
    CHECK(scene->selectNodeById(texNode->id), "selectNodeById finds the texture node");
    CHECK(panel->currentView() == NodePropertiesPanel::View::Node,
          "texture node: node view shown (picker button hosts the flow)");

    // ---- master -> material settings view; edits carry bakeResolution ----
    graph->settings.name = "TestMat";
    graph->settings.bakeResolution = 1024;
    CHECK(scene->selectNodeById(master->id), "selectNodeById finds the master");
    CHECK(panel->currentView() == NodePropertiesPanel::View::MaterialSettings,
          "master selected: material settings view");
    {
        bool edited = false;
        MaterialSettings editedSettings;
        QObject::connect(panel, &NodePropertiesPanel::settingsEdited,
                         [&](MaterialSettings s) { edited = true; editedSettings = s; });
        auto spins = visibleBoxes<QSpinBox>(panel);
        CHECK(spins.size() == 1, "master view: one bake-resolution box");
        if (spins.size() == 1) {
            CHECK(spins[0]->value() == 1024, "bake resolution defaults to 1024");
            spins[0]->setValue(2048);
            CHECK(edited, "editing bake resolution emits settingsEdited");
            CHECK(editedSettings.bakeResolution == 2048,
                  "settingsEdited carries the new bake resolution");
            CHECK(editedSettings.name == "TestMat",
                  "settingsEdited keeps the untouched fields");
        }
    }

    // ---- bakeResolution serializes with the graph ----
    {
        graph->settings.bakeResolution = 2048;
        auto saved = graph->serialize();
        CHECK(saved["settings"].toObject()["bakeResolution"].toInt() == 2048,
              "bakeResolution serializes under settings.bakeResolution");
        auto settings = NodeGraph::deserializeMaterialSettings(saved["settings"].toObject());
        CHECK(settings.bakeResolution == 2048, "bakeResolution round-trips");
        auto defaults = NodeGraph::deserializeMaterialSettings(QJsonObject());
        CHECK(defaults.bakeResolution == 1024, "absent bakeResolution defaults to 1024");
    }

    // ---- deselect ----
    scene->deselectAll();
    CHECK(scene->selectedNodeId().isEmpty(), "deselectAll clears the selection");
    CHECK(lastSelected == nullptr, "nodeSelected(null) announced the empty selection");
    CHECK(panel->currentView() == NodePropertiesPanel::View::GraphSettings,
          "empty selection: graph settings view");

    if (failures == 0) std::printf("ALL OK\n");
    else std::printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
