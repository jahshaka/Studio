/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.properties_tabs — THE RIGHT COLUMN IS TWO TABS (PROPERTY_FILTER_SPEC §2,
// lane RIGHT-TABS-1).
//
// The Properties column used to be ONE panel that changed shape by what was
// selected: selecting the scene's ROOT NODE mounted the eight world sections,
// selecting anything else mounted that object's. That is why the World row had
// to exist in the Hierarchy, why opening a scene selected the root, and why
// clicking World after picking an object took two clicks (§311).
//
// Now: a World tab that always holds the world sections, a Selection tab that
// holds the picked object's, and the rules this suite pins —
//   * a pick raises Selection; the root (a verb, a scene open) raises World;
//   * a DESELECT keeps the tab it is on and says "nothing selected";
//   * switching tabs by hand does not change the selection, and switching back
//     shows the same object's rows again;
//   * the tab bar (PropertiesTabStrip) follows the panel, a click on it drives
//     the panel, and its minimum width fits the narrowest the column can be
//     (PanelMetrics::rightColumnMinWidth — the column-width law, ui.properties_width).
//
// The real panel, the real strip, the real blades, the real Qlementine style,
// offscreen QPA — the ui.properties_width shape.

#include <QApplication>
#include <QLabel>
#include <QListWidgetItem>
#include <QPointer>
#include <QScrollArea>
#include <QTabBar>
#include <QUndoStack>
#include <QVBoxLayout>

#include <cstdio>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/materials/pbrmaterial.h"

#include "data/project.h"
#include "data/settingsmanager.h"
#include "ui/controls/accordionbladewidget.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "ui/controls/propertiestabstrip.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/style/panelmetrics.h"
#include "ui/style/thememanager.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("ok:   %s\n", name); } \
    else { std::printf("FAIL: %s\n", name); ++failures; } \
} while (0)

namespace {

using Tab = SceneNodePropertiesWidget::Tab;

/// The titles of the blades the panel currently has ON its layout — what the
/// user sees in the column, in order.
QStringList mountedSections(QWidget *panel)
{
    QStringList out;
    QLayout *layout = panel->layout();
    for (int i = 0; layout && i < layout->count(); ++i) {
        QWidget *w = layout->itemAt(i)->widget();
        if (!w || !w->isVisibleTo(panel)) continue;
        bool named = false;
        for (QLabel *l : w->findChildren<QLabel *>(QStringLiteral("content_title"))) {
            out.append(l->text());
            named = true;
            break;
        }
        if (!named && qobject_cast<QLabel *>(w))
            out.append(QStringLiteral("<message>"));
    }
    return out;
}

bool hasSection(QWidget *panel, const QString &title)
{
    for (const QString &s : mountedSections(panel))
        if (s.compare(title, Qt::CaseInsensitive) == 0 ||
            s.startsWith(title, Qt::CaseInsensitive)) return true;
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    enginetest::DocumentGraph graph("ui-properties-tabs-ogre.log");
    if (!graph.require()) return 1;

    auto *settings = SettingsManager::getDefaultManager();
    settings->settings->remove(ThemeManager::settingsKey());
    ThemeManager::applyAtStartup(app);

    QUndoStack stack;
    UndoService undo(&stack);
    StudioServices services;
    services.undo = &undo;

    Project project;
    project.setProjectGuid(QStringLiteral("ui-properties-tabs"));

    auto scene = iris::Scene::create();
    auto mesh = iris::MeshNode::create();
    mesh->setName(QStringLiteral("mesh"));
    mesh->setMaterial(iris::PbrMaterial::create());
    auto light = iris::LightNode::create();
    light->setName(QStringLiteral("light"));
    scene->getRootNode()->addChild(mesh.staticCast<iris::SceneNode>());
    scene->getRootNode()->addChild(light.staticCast<iris::SceneNode>());

    // The dock, as shell/mainwindow.cpp setupDockWidgets() builds it: the strip
    // ABOVE the scroll area, the panel inside it.
    QWidget host;
    auto *hostLayout = new QVBoxLayout(&host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    auto *panel = new SceneNodePropertiesWidget;
    panel->setServices(&services);
    panel->setDatabase(nullptr);
    panel->setProject(&project);
    auto *strip = new PropertiesTabStrip(panel, &host);
    auto *scroll = new QScrollArea(&host);
    scroll->setMinimumWidth(PanelMetrics::rightColumnMinWidth);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    hostLayout->addWidget(strip);
    hostLayout->addWidget(scroll);
    host.resize(PanelMetrics::rightColumnWidth, 900);
    host.show();
    QApplication::processEvents();

    QTabBar *bar = strip->tabBar();
    auto turn = []() { QApplication::processEvents(); QApplication::sendPostedEvents(); };

    // ---- 1. the strip itself ----------------------------------------------
    CHECK(bar && bar->count() == 2, "properties_tabs: two tabs, World and Selection");
    CHECK(bar && bar->tabText(0) == QStringLiteral("World") &&
          bar->tabText(1) == QStringLiteral("Selection"),
          "properties_tabs: the tabs are named World and Selection");
    // THE COLUMN-WIDTH LAW (ui.properties_width): the strip is a sibling of the
    // scroll area, so its minimum is the dock's minimum too.
    const int stripMin = strip->minimumSizeHint().width();
    CHECK(stripMin <= PanelMetrics::rightColumnMinWidth,
          QStringLiteral("properties_tabs: the strip fits the narrowest column "
                         "(%1 px <= %2)").arg(stripMin)
              .arg(PanelMetrics::rightColumnMinWidth).toUtf8().constData());
    // The bar costs the column HEIGHT, not width, and a sane amount of it.
    const int stripHeight = strip->sizeHint().height();
    CHECK(stripHeight > 0 && stripHeight < 120,
          QStringLiteral("properties_tabs: the tab bar's height is one bar (%1 px)")
              .arg(stripHeight).toUtf8().constData());

    // ---- 2. THE SCENE OPEN: bound to the scene, World in front ------------
    // mainwindow.cpp's open path: setScene(scene) then setSceneNode(root).
    panel->setScene(scene);
    panel->setSceneNode(scene->getRootNode());
    turn();
    CHECK(panel->propertiesTab() == Tab::World,
          "properties_tabs: a scene open shows the World tab");
    CHECK(bar->currentIndex() == 0, "properties_tabs: ...and the bar follows it");
    CHECK(hasSection(panel, QStringLiteral("World")) &&
          hasSection(panel, QStringLiteral("Sky")) &&
          hasSection(panel, QStringLiteral("Photon")) &&
          hasSection(panel, QStringLiteral("Fog")),
          "properties_tabs: the World tab holds the world sections");
    CHECK(!hasSection(panel, QStringLiteral("Transformation")),
          "properties_tabs: ...and not the selection's");

    // ---- 3. a pick raises Selection ---------------------------------------
    panel->setSceneNode(mesh.staticCast<iris::SceneNode>());
    turn();
    CHECK(panel->propertiesTab() == Tab::Selection,
          "properties_tabs: a pick raises the Selection tab");
    CHECK(bar->currentIndex() == 1, "properties_tabs: ...and the bar follows it");
    CHECK(hasSection(panel, QStringLiteral("Transformation")) &&
          hasSection(panel, QStringLiteral("Movement")) &&
          hasSection(panel, QStringLiteral("Mesh")),
          "properties_tabs: the Selection tab holds the picked object's sections");
    CHECK(!hasSection(panel, QStringLiteral("Sky")),
          "properties_tabs: ...and not the world's");

    // ---- 4. a DESELECT keeps the tab and says so --------------------------
    panel->setSceneNode(iris::SceneNodePtr());
    turn();
    CHECK(panel->propertiesTab() == Tab::Selection,
          "properties_tabs: a deselect keeps the tab the user is on (D3)");
    CHECK(mountedSections(panel).contains(QStringLiteral("<message>")),
          "properties_tabs: ...and the Selection tab says nothing is selected");

    // ---- 5. the root raises World, without being a selection --------------
    panel->setSceneNode(light.staticCast<iris::SceneNode>());
    turn();
    CHECK(panel->propertiesTab() == Tab::Selection && hasSection(panel, QStringLiteral("Light")),
          "properties_tabs: the light is picked");
    panel->setSceneNode(scene->getRootNode());          // editor.select(rootId)
    turn();
    CHECK(panel->propertiesTab() == Tab::World,
          "properties_tabs: editor.select(root) raises the World tab");
    CHECK(hasSection(panel, QStringLiteral("Shadows")),
          "properties_tabs: ...and mounts the world sections");

    // ---- 6. switching by hand: the selection does not move ----------------
    panel->setSceneNode(mesh.staticCast<iris::SceneNode>());
    turn();
    panel->setPropertiesTab(Tab::World);                // Ctrl+Shift+P / a tab click
    turn();
    CHECK(bar->currentIndex() == 0, "properties_tabs: the bar follows a programmatic switch");
    CHECK(hasSection(panel, QStringLiteral("Sky")) &&
          !hasSection(panel, QStringLiteral("Transformation")),
          "properties_tabs: the World tab shows the world while an object is selected");
    panel->setPropertiesTab(Tab::Selection);
    turn();
    CHECK(hasSection(panel, QStringLiteral("Mesh")),
          "properties_tabs: switching back shows the SAME object's rows again");

    // ---- 7. a click on the BAR drives the panel ---------------------------
    bar->setCurrentIndex(0);
    turn();
    CHECK(panel->propertiesTab() == Tab::World,
          "properties_tabs: a click on the World tab moves the panel");
    CHECK(hasSection(panel, QStringLiteral("Anti-Aliasing")),
          "properties_tabs: ...and mounts the world sections");
    bar->setCurrentIndex(1);
    turn();
    CHECK(panel->propertiesTab() == Tab::Selection,
          "properties_tabs: a click on the Selection tab moves it back");

    // ---- 8. ONE MOUNT PER PICK (F2, second reader) ------------------------
    //
    // setSceneNode used to raise the tab (which mounts when the tab MOVES) and
    // then mount again unconditionally, so a pick that CROSSED tabs built its
    // blades twice and charged the blade's retired-row ring two generations.
    //
    // Measured against the control instead of against a row count: a pick that
    // does NOT cross tabs mounted once even with the defect, so the two must
    // retire the same number of rows. (clearPanel retires the rows the previous
    // mount built; a second mount in the same pick retires the rows the first
    // one just built, doubling it.)
    auto materialBlade = [panel]() -> AccordianBladeWidget * {
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
            for (QLabel *l : b->findChildren<QLabel *>(QStringLiteral("content_title")))
                if (l->text() == QStringLiteral("Material")) return b;
        return nullptr;
    };
    panel->setSceneNode(mesh.staticCast<iris::SceneNode>());   // builds the blade
    turn();
    AccordianBladeWidget *material = materialBlade();
    CHECK(material != nullptr, "properties_tabs: the material blade exists after a mesh pick");
    if (material) {
        // CONTROL: a pick that does not cross tabs (Selection -> Selection) —
        // it mounted once even with the defect.
        panel->setSceneNode(light.staticCast<iris::SceneNode>());
        turn();
        int before = panel->mountCount();
        panel->setSceneNode(mesh.staticCast<iris::SceneNode>());
        const int sameTab = panel->mountCount() - before;
        turn();

        // THE CASE: from the World tab, so the pick crosses.
        panel->setPropertiesTab(Tab::World);
        turn();
        before = panel->mountCount();
        panel->setSceneNode(mesh.staticCast<iris::SceneNode>());
        const int crossing = panel->mountCount() - before;
        turn();
        CHECK(sameTab == 1,
              QStringLiteral("properties_tabs: a same-tab pick mounts the column once (%1)")
                  .arg(sameTab).toUtf8().constData());
        CHECK(crossing == 1,
              QStringLiteral("properties_tabs: a TAB-CROSSING pick mounts it once too (%1) — "
                             "it used to mount twice, rebuilding every blade and charging the "
                             "retired-row ring two generations").arg(crossing).toUtf8().constData());
        // The same for the root: Selection -> World is one mount, not two.
        panel->setPropertiesTab(Tab::Selection);
        turn();
        before = panel->mountCount();
        panel->setSceneNode(scene->getRootNode());
        CHECK(panel->mountCount() - before == 1,
              QStringLiteral("properties_tabs: editor.select(root) from Selection mounts once (%1)")
                  .arg(panel->mountCount() - before).toUtf8().constData());
        turn();
    }

    // ---- 9. THE WORLD BLADES ARE BOUND ONCE PER SCENE (F1) ----------------
    // A scene open ran the World tab's bind three times — once against the
    // scene being closed — because every mount re-bound. Binding is what
    // rebuilds the five scene-built panels' rows; mounting is a layout move.
    // The Photon panel is one of the five: its rows are retired by its own
    // rebuild, so a re-bind shows up as retired rows.
    auto photonBlade = [panel]() -> AccordianBladeWidget * {
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
            for (QLabel *l : b->findChildren<QLabel *>(QStringLiteral("content_title")))
                if (l->text().startsWith(QStringLiteral("Photon"))) return b;
        return nullptr;
    };
    AccordianBladeWidget *photon = photonBlade();
    CHECK(photon != nullptr, "properties_tabs: the Photon blade exists");
    if (photon) {
        // THE MEASURE is the identity of the blade's first ROW: the Photon
        // panel destroys and rebuilds every row it owns inside setScene (its
        // rebuild()), so a row that is still the same object was not re-bound.
        auto firstRow = [photon]() -> QWidget * {
            QWidget *pane = photon->findChild<QWidget *>(QStringLiteral("contentpane"));
            QLayout *l = pane ? pane->layout() : nullptr;
            return (l && l->count() > 0) ? l->itemAt(0)->widget() : nullptr;
        };
        panel->setPropertiesTab(Tab::World);
        turn();
        QPointer<QWidget> boundRow = firstRow();
        CHECK(!boundRow.isNull(), "properties_tabs: the Photon blade has rows to compare");

        // Three more World mounts, no scene change: not one re-bind.
        panel->setPropertiesTab(Tab::Selection);
        panel->setPropertiesTab(Tab::World);
        panel->setSceneNode(scene->getRootNode());
        turn();
        CHECK(!boundRow.isNull() && firstRow() == boundRow.data(),
              "properties_tabs: re-showing the World tab does not re-bind the world blades "
              "(the rows are the same objects)");

        // A NEW SCENE does re-bind — the memo is per scene, not forever.
        auto second = iris::Scene::create();
        panel->setScene(second);
        turn();
        CHECK(firstRow() != nullptr && firstRow() != boundRow.data(),
              "properties_tabs: a NEW scene re-binds the world blades (fresh rows)");
        // ...and a CLOSE (a null scene) empties the column instead of showing
        // a scene that is gone (F3).
        panel->setScene(iris::ScenePtr());
        turn();
        CHECK(mountedSections(panel).isEmpty(),
              QStringLiteral("properties_tabs: a closed scene leaves the World tab empty (%1)")
                  .arg(mountedSections(panel).join(QStringLiteral(","))).toUtf8().constData());
        panel->setScene(scene);          // put the suite's scene back
        turn();
    }

    // ---- 10. A LIBRARY ASSET SURVIVES A RE-APPLY (F6) ---------------------
    // setAssetItem used to mount its blade directly, so any later re-apply —
    // an undo (refreshFromDocument), a tab toggle and back — replaced the
    // asset's rows with the "nothing selected" line.
    {
        QListWidgetItem item;
        item.setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Sky));
        item.setData(MODEL_GUID_ROLE, QStringLiteral("sky-asset-guid"));
        item.setData(SKY_TYPE_ROLE, static_cast<int>(iris::SkyType::SINGLE_COLOR));
        panel->setAssetItem(&item);
        turn();
        CHECK(panel->propertiesTab() == Tab::Selection,
              "properties_tabs: picking a library asset raises the Selection tab");
        CHECK(hasSection(panel, QStringLiteral("Sky")),
              "properties_tabs: ...and mounts the asset's blade");
        panel->setPropertiesTab(Tab::World);
        turn();
        panel->setPropertiesTab(Tab::Selection);
        turn();
        CHECK(hasSection(panel, QStringLiteral("Sky")) &&
              !mountedSections(panel).contains(QStringLiteral("<message>")),
              "properties_tabs: the asset blade is STILL there after a tab toggle and back");
        // A node pick takes the slot back (they are exclusive).
        panel->setSceneNode(mesh.staticCast<iris::SceneNode>());
        turn();
        CHECK(hasSection(panel, QStringLiteral("Transformation")) &&
              !hasSection(panel, QStringLiteral("Sky")),
              "properties_tabs: picking a node takes the Selection tab back from the asset");
    }

    // ---- 11. the names the verb speaks ------------------------------------
    Tab parsed = Tab::Selection;
    CHECK(SceneNodePropertiesWidget::tabName(Tab::World) == QStringLiteral("world") &&
          SceneNodePropertiesWidget::tabName(Tab::Selection) == QStringLiteral("selection"),
          "properties_tabs: the tab names are 'world' and 'selection'");
    CHECK(SceneNodePropertiesWidget::tabFromName(QStringLiteral("World"), parsed) &&
          parsed == Tab::World,
          "properties_tabs: tabFromName is case-insensitive");
    CHECK(!SceneNodePropertiesWidget::tabFromName(QStringLiteral("nonsense"), parsed),
          "properties_tabs: an unknown tab name is refused");

    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
