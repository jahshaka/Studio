/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.components — THE COMPONENTS SECTION (owner review R14, COMPONENTS-1).
//
// "we need to add the ability to view individual components of a grouped asset
// like the objects we imported in the right column."
//
// An imported model is ONE row in the outliner with its meshes hidden beneath
// it (they are `attached`, and the outliner skips them deliberately). This
// section is where they become reachable, and this suite drives it with real
// mouse events on the real panel under the real Qlementine style:
//
//   * the section appears for a grouped node and NOT for a lone object;
//   * its rows are the parts, in document order, nested by depth, with the
//     type icon the outliner draws (one icon source — ui/controls/nodeicons.h);
//   * a CLICK selects that part; SHIFT-click adds; CTRL-click toggles;
//   * a DOUBLE-CLICK frames it (the viewport is asked to focus that node);
//   * the highlight FOLLOWS a selection made elsewhere (the hierarchy's own
//     click, a verb) — the "both ways" half of R14;
//   * THE LIST DOES NOT REBUILD when the selection moves inside one model
//     (SELECT-COST-1's law at row level: it re-lays by difference), and it DOES
//     rebuild when a part is renamed, hidden or locked;
//   * the section stays on screen while a part is selected — the subject is the
//     GROUP, so clicking a part does not empty the list under the cursor;
//   * a selection change with the section mounted costs no more than one
//     without it.
//
// Offscreen QPA, no display, no engine: the document graph like every other
// document suite.

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QScrollArea>
#include <QTest>
#include <QTreeWidget>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>
#include <cstdio>

#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "data/settingsmanager.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "ui/panels/propertywidgets/componentspropertywidget.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/style/thememanager.h"
#include "viewport/headlesseditorviewport.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("ok:   %s\n", name); } \
    else { std::printf("FAIL: %s\n", name); ++failures; } \
} while (0)

namespace {

/// The shipping headless viewport with one thing added: it remembers what it
/// was asked to frame. Nothing here duplicates the 137-method interface.
class RecordingViewport : public HeadlessEditorViewport
{
public:
    void focusOnNode(iris::SceneNodePtr node) override { framed = node; ++frameCalls; }
    iris::SceneNodePtr framed;
    int frameCalls = 0;
};

iris::SceneNodePtr makeMesh(const QString &name)
{
    auto mesh = iris::MeshNode::create();
    mesh->setName(name);
    mesh->setMaterial(iris::PbrMaterial::create());
    return mesh;
}

/// The Components blade the panel built, or null when it is not mounted.
ComponentsPropertyWidget *sectionOf(SceneNodePropertiesWidget *panel)
{
    panel->flushPendingMount();
    const auto blades = panel->findChildren<ComponentsPropertyWidget *>();
    if (blades.isEmpty()) return nullptr;
    // Built once and kept; "mounted" is whether the layout holds it, which for
    // a permanent hidden child is exactly isVisibleTo(panel).
    return blades.first()->isVisibleTo(panel) ? blades.first() : nullptr;
}

double median(QVector<double> v)
{
    std::sort(v.begin(), v.end());
    return v.isEmpty() ? 0.0 : v[v.size() / 2];
}

}   // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    enginetest::DocumentGraph graph("ui-components-ogre.log");
    if (!graph.require()) return 1;

    // THE ICONS ARE REAL, which takes one line of rigging: every icon in this
    // app is resolved against applicationDirPath (IrisUtils::getAbsoluteAssetPath)
    // and a test binary lives in tests/ui, not in bin/ where the icons are
    // staged. Without this the rows draw null pixmaps and "the hidden part
    // shows a different eye" is an assertion about two empty icons.
    {
        const QDir appDir(QCoreApplication::applicationDirPath());
        if (!appDir.exists(QStringLiteral("app/icons"))) {
            const QString staged = QDir(appDir.absoluteFilePath(QStringLiteral("../../bin/app")))
                                       .absolutePath();
            if (QDir(staged).exists())
                QFile::link(staged, appDir.absoluteFilePath(QStringLiteral("app")));
        }
        if (!QFile::exists(appDir.absoluteFilePath(
                QStringLiteral("app/icons/icons8-mesh-32.png")))) {
            std::printf("FAIL: the staged icons are not reachable from %s\n",
                        qPrintable(appDir.absolutePath()));
            return 1;
        }
    }

    auto *settings = SettingsManager::getDefaultManager();
    settings->settings->remove(ThemeManager::settingsKey());
    ThemeManager::applyAtStartup(app);

    QUndoStack stack;
    UndoService undo(&stack);
    SelectionService selection;
    RecordingViewport viewport;
    StudioServices services;
    services.undo = &undo;
    services.selection = &selection;

    // ---- an imported model's shape: a wrapper with three ATTACHED parts, one
    //      of which has a part of its own; plus a lone mesh beside it. -------
    auto scene = iris::Scene::create();
    auto group = iris::SceneNode::create();
    group->setName(QStringLiteral("Robot"));
    scene->getRootNode()->addChild(group);

    QVector<iris::SceneNodePtr> parts;
    for (const QString &name : { QStringLiteral("Head"), QStringLiteral("Torso"),
                                 QStringLiteral("Leg") }) {
        auto part = makeMesh(name);
        group->addChild(part);
        part->setAttached(true);
        parts.append(part);
    }
    auto bolt = makeMesh(QStringLiteral("Bolt"));
    parts[0]->addChild(bolt);
    bolt->setAttached(true);

    auto lone = makeMesh(QStringLiteral("Crate"));
    scene->getRootNode()->addChild(lone);

    // ---- the panel, in a scroll area, the way the dock hosts it -------------
    QWidget host;
    host.setLayout(new QVBoxLayout);
    auto *scroll = new QScrollArea(&host);
    host.layout()->addWidget(scroll);
    auto *panel = new SceneNodePropertiesWidget;
    panel->setServices(&services);
    panel->setDatabase(nullptr);
    panel->setProject(nullptr);
    panel->setSceneView(&viewport);
    panel->setScene(scene);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    host.resize(420, 900);
    host.show();

    auto turn = [&]() {
        host.grab();
        QApplication::processEvents();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    for (int i = 0; i < 4; ++i) turn();

    // ---- 1. the section appears for a group, and not for a lone object ------
    panel->setSceneNode(lone);
    for (int i = 0; i < 2; ++i) turn();
    CHECK(sectionOf(panel) == nullptr, "a lone object gets NO Components section");

    panel->setSceneNode(group);
    for (int i = 0; i < 2; ++i) turn();
    auto *section = sectionOf(panel);
    CHECK(section != nullptr, "a grouped node gets a Components section");
    if (!section) { std::printf("FAIL: nothing further can be checked\n"); return 1; }
    CHECK(section->panelTitle() == QLatin1String("Components"),
          "...titled Components");

    const QStringList rows = section->rowLabels();
    CHECK(rows.join(QStringLiteral(",")) == QLatin1String("1:Head,2:Bolt,1:Torso,1:Leg"),
          "the rows are the parts in document order, nested by depth");

    QTreeWidget *tree = section->listWidget();
    CHECK(tree && tree->topLevelItemCount() == 3, "three top-level rows, one nested");
    CHECK(tree && !tree->topLevelItem(0)->icon(0).isNull(),
          "a row carries the hierarchy's own type icon (one icon source)");
    CHECK(tree && !tree->topLevelItem(0)->icon(1).isNull()
              && !tree->topLevelItem(0)->icon(2).isNull(),
          "...and its visibility and lock indicators");
    CHECK(tree && tree->selectionMode() == QAbstractItemView::ExtendedSelection,
          "the list takes Shift and Ctrl gestures");
    CHECK(tree && !tree->toolTip().isEmpty()
              && tree->toolTip().contains(QLatin1String("Hierarchy")),
          "the tooltip says the eye and the padlock are shown here, edited there");

    // A helper that clicks the row whose name is `name`, with modifiers.
    auto rowFor = [&](const QString &name) -> QTreeWidgetItem * {
        QTreeWidgetItemIterator it(tree);
        for (; *it; ++it) if ((*it)->text(0) == name) return *it;
        return nullptr;
    };
    auto clickRow = [&](const QString &name, Qt::KeyboardModifiers mods) {
        QTreeWidgetItem *item = rowFor(name);
        if (!item) { std::printf("FAIL: no row named %s\n", qPrintable(name)); ++failures; return; }
        const QRect r = tree->visualItemRect(item);
        // THE ROW MUST BE INSIDE THE VIEWPORT. The list sizes itself to its
        // rows, and it got that arithmetic wrong once (a 20 px constant against
        // the 28 px the style draws) — the last row was laid out below the
        // viewport, present and unclickable. A click that misses would
        // otherwise read as "the gesture does not work".
        if (!tree->viewport()->rect().contains(r.center())) {
            std::printf("FAIL: the row '%s' is laid out outside the list's viewport "
                        "(row %d..%d, viewport %d tall)\n",
                        qPrintable(name), r.top(), r.bottom(), tree->viewport()->height());
            ++failures;
            return;
        }
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, mods, r.center());
        turn();
    };

    // ---- 2. a click selects that part --------------------------------------
    const int rebuildsBeforeClicks = section->rebuildCount();
    clickRow(QStringLiteral("Torso"), Qt::NoModifier);
    CHECK(selection.selected() == parts[1], "clicking a row SELECTS that part");
    CHECK(selection.count() == 1, "...and only that part");

    // The section must still be there, showing the same list: the subject is
    // the GROUP, not the selection (otherwise it empties under the cursor).
    panel->setSceneNode(parts[1]);
    for (int i = 0; i < 2; ++i) turn();
    CHECK(sectionOf(panel) == section, "the section survives selecting a part");
    CHECK(section->rowLabels().join(QStringLiteral(",")) ==
              QLatin1String("1:Head,2:Bolt,1:Torso,1:Leg"),
          "...showing the same list");
    CHECK(section->rebuildCount() == rebuildsBeforeClicks,
          "...WITHOUT rebuilding a single row (it re-lays by difference)");
    CHECK(rowFor(QStringLiteral("Torso"))->isSelected(),
          "...with the clicked part highlighted");

    // ---- 3. Shift adds, Ctrl toggles ---------------------------------------
    clickRow(QStringLiteral("Leg"), Qt::ShiftModifier);
    CHECK(selection.count() == 2 && selection.isSelected(parts[1])
              && selection.isSelected(parts[2]),
          "SHIFT-click adds the range to the selection");
    clickRow(QStringLiteral("Head"), Qt::ControlModifier);
    CHECK(selection.count() == 3 && selection.isSelected(parts[0]),
          "CTRL-click adds a part without dropping the others");
    CHECK(selection.selected() == parts[0],
          "...and the row just clicked is the PRIMARY (what the column shows)");
    clickRow(QStringLiteral("Head"), Qt::ControlModifier);
    CHECK(selection.count() == 2 && !selection.isSelected(parts[0]),
          "CTRL-click again takes it back out");

    // ---- 4. a double-click frames it ---------------------------------------
    viewport.frameCalls = 0;
    {
        // A DOUBLE CLICK IS A CLICK AND THEN A DOUBLE CLICK: QTest's widget
        // overload sends only the QEvent::MouseButtonDblClick, and the view
        // drops one whose row is not the row it last saw pressed.
        QTreeWidgetItem *item = rowFor(QStringLiteral("Bolt"));
        const QRect r = tree->visualItemRect(item);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, r.center());
        turn();
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, r.center());
        turn();
    }
    CHECK(viewport.frameCalls == 1 && viewport.framed == bolt,
          "DOUBLE-CLICK frames that part in the viewport");
    CHECK(section->rowLabels().size() == 4,
          "...and does not fold the row away (a double-click is not an expander)");

    // ---- 5. the highlight follows a selection made elsewhere ---------------
    // This is the hierarchy's half of "both ways": nothing touches the list.
    selection.select(parts[2]);
    turn();
    CHECK(rowFor(QStringLiteral("Leg"))->isSelected()
              && !rowFor(QStringLiteral("Torso"))->isSelected(),
          "a selection made OUTSIDE the section moves its highlight");
    selection.select(QList<iris::SceneNodePtr>{ parts[0], bolt });
    turn();
    CHECK(rowFor(QStringLiteral("Head"))->isSelected()
              && rowFor(QStringLiteral("Bolt"))->isSelected()
              && !rowFor(QStringLiteral("Leg"))->isSelected(),
          "...a MULTI-selection made outside marks every member");
    selection.select(group);
    turn();
    CHECK(!rowFor(QStringLiteral("Head"))->isSelected(),
          "selecting the GROUP highlights no part");

    // ---- 6. the list follows the document ----------------------------------
    const int beforeEdit = section->rebuildCount();
    parts[1]->setVisible(false);
    parts[2]->setPickable(false);
    panel->setSceneNode(group);
    for (int i = 0; i < 2; ++i) turn();
    CHECK(section->rebuildCount() == beforeEdit + 1,
          "hiding and locking a part DOES rebuild the rows");
    CHECK(rowFor(QStringLiteral("Torso"))->icon(1).cacheKey()
              != rowFor(QStringLiteral("Head"))->icon(1).cacheKey(),
          "...a hidden part draws a different eye from a visible one");
    CHECK(rowFor(QStringLiteral("Leg"))->icon(2).cacheKey()
              != rowFor(QStringLiteral("Head"))->icon(2).cacheKey(),
          "...and a locked part a different padlock");
    parts[1]->setVisible(true);
    parts[2]->setPickable(true);

    bolt->setName(QStringLiteral("Rivet"));
    panel->setSceneNode(group);
    for (int i = 0; i < 2; ++i) turn();
    CHECK(section->rowLabels().join(QStringLiteral(",")) ==
              QLatin1String("1:Head,2:Rivet,1:Torso,1:Leg"),
          "a renamed part is renamed in the list");

    // ---- 7. THE COST: a selection change inside the model is not a rebuild --
    //
    // Two hundred selection changes among the group and its parts. The rows are
    // built ONCE; everything after is a repaint of the highlight. And the wall
    // time is compared with the same two hundred switches on a plain object,
    // which mounts no section at all — the section may not make a pick cost
    // measurably more.
    const int rebuildsBefore = section->rebuildCount();
    const int refreshesBefore = section->refreshCount();
    QVector<double> groupMs, plainMs;
    QElapsedTimer timer;
    const QVector<iris::SceneNodePtr> cycle = { group, parts[0], bolt, parts[1], parts[2] };
    for (int i = 0; i < 200; ++i) {
        timer.start();
        panel->setSceneNode(cycle[i % cycle.size()]);
        panel->flushPendingMount();
        groupMs.append(timer.nsecsElapsed() / 1e6);
        turn();
    }
    for (int i = 0; i < 200; ++i) {
        timer.start();
        panel->setSceneNode(lone);
        panel->flushPendingMount();
        plainMs.append(timer.nsecsElapsed() / 1e6);
        turn();
        panel->setSceneNode(iris::SceneNodePtr());
        panel->flushPendingMount();
        turn();
    }
    const int rebuilt = section->rebuildCount() - rebuildsBefore;
    const int refreshed = section->refreshCount() - refreshesBefore;
    std::printf("  200 picks inside the model: %d rebuilds, %d repaints; "
                "median pick %.3f ms (group) vs %.3f ms (plain object)\n",
                rebuilt, refreshed, median(groupMs), median(plainMs));
    CHECK(rebuilt == 0, "200 picks inside one model rebuild the list ZERO times");
    CHECK(refreshed > 0, "...they repaint the highlight instead");
    CHECK(median(groupMs) < median(plainMs) + 2.0,
          "...and a pick with the section mounted is not measurably dearer");

    std::printf(failures ? "ui.components: FAILED (%d)\n" : "ui.components: PASS\n", failures);
    return failures == 0 ? 0 : 1;
}
