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

    // ---- 8. the names the verb speaks -------------------------------------
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
