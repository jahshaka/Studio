/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.tree_reselect — THE ROW THIS PANEL LAST ANNOUNCED IS STILL CLICKABLE AFTER
// SOMEONE ELSE MAKES A SELECTION (SPACE-3 diagnosis, 2026-09-15; owner §311:
// "clicking an item often needs two or three clicks and the selection stays
// where it was").
//
// THE CAUSE. SceneHierarchyWidget::announceSet() deduplicates against a cache
// so that ONE click is not announced twice (Qt fires itemSelectionChanged on
// the press and itemClicked on the release, and SelectionService re-emits on
// every replace, so the second announce would rebuild the whole properties
// column for nothing). The cache used to be written ONLY by announceSet — "the
// last set WE announced" — so a selection made anywhere else (a viewport pick,
// a script verb, a service call) left it holding a set that was no longer the
// selection. Clicking that row then matched the stale cache and was swallowed:
// the click did nothing, and kept doing nothing until some other row was
// clicked first.
//
// THE FIX under test: the cache is what the panel BELIEVES the editor's
// selection is, written by the inbound legs (setSelectedNode / setSelectedSet)
// as well as by announceSet; the panel's own paint-then-announce gestures use
// paintSelection(), which does not touch it.
//
// Offscreen QPA, no display, no engine; document nodes on the headless graph.
// The shell's return leg is wired exactly as MainWindow::applySelectionToUi
// wires it, because the defect lives in that round trip.

#include <QApplication>
#include <QTest>
#include <QTreeWidget>

#include <cstdio>

#include "../support/documentgraph.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "ui/panels/scenehierarchywidget.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS %s\n", name); } \
    else { std::printf("FAIL %s\n", name); ++failures; } \
} while (0)

namespace {

QTreeWidgetItem *rowFor(QTreeWidget *tree, const QString &name)
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->text(0) == name) return *it;
    return nullptr;
}

void run()
{
    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();
    const char *names[] = { "Sphere", "Cube", "Cone" };
    for (const char *n : names) {
        auto node = iris::SceneNode::create();
        node->setName(QLatin1String(n));
        root->addChild(node);
    }
    const auto sphere = root->children().at(0);
    const auto cube   = root->children().at(1);

    SceneHierarchyWidget panel;
    panel.resize(320, 600);            // every row on screen: scrolling is not the subject
    panel.show();
    panel.setScene(scene);
    QTreeWidget *tree = panel.getWidget();
    tree->expandAll();
    QApplication::processEvents();

    // THE SHELL'S RETURN LEG (MainWindow::applySelectionToUi): whatever the
    // panel announces is handed to the service and comes straight back in.
    iris::SceneNodePtr announced;
    int announceCount = 0;
    QObject::connect(&panel, &SceneHierarchyWidget::sceneNodeSelected,
                     [&panel, &announced, &announceCount](iris::SceneNodePtr node) {
        announced = node;
        ++announceCount;
        panel.setSelectedNode(node);
    });
    QObject::connect(&panel, &SceneHierarchyWidget::sceneNodeSetSelected,
                     [&panel, &announced, &announceCount](const QList<iris::SceneNodePtr> &set) {
        announced = set.isEmpty() ? iris::SceneNodePtr() : set.first();
        ++announceCount;
        panel.setSelectedSet(set);
    });

    auto clickRow = [tree](const QString &name) {
        QTreeWidgetItem *row = rowFor(tree, name);
        if (!row) return false;
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                          tree->visualItemRect(row).center());
        QApplication::processEvents();
        return true;
    };

    // ---- 0. the guard the cache exists for: ONE click, ONE announce --------
    // (If this ever fails, the deduplication is gone and every tree click
    // rebuilds the properties column twice.)
    announceCount = 0;
    CHECK(clickRow(QStringLiteral("Cone")), "tree_reselect: the Cone row exists");
    CHECK(announceCount == 1,
          QStringLiteral("tree_reselect: one click announces exactly once (got %1)")
              .arg(announceCount).toUtf8().constData());

    // ---- 1. THE DEFECT: a pick made elsewhere, then the announced row ------
    // Click Sphere in the tree (the panel announces it and records it), then
    // let something else — a viewport pick, editor.select(cube), any service —
    // select Cube through the inbound leg, then click the Sphere row again.
    CHECK(clickRow(QStringLiteral("Sphere")), "tree_reselect: the Sphere row exists");
    CHECK(announced == sphere, "tree_reselect: the tree click selects Sphere");

    panel.setSelectedNode(cube);                 // the viewport / verb / service leg
    QApplication::processEvents();
    announced = iris::SceneNodePtr();
    announceCount = 0;

    CHECK(clickRow(QStringLiteral("Sphere")), "tree_reselect: the Sphere row is still there");
    CHECK(announced == sphere,
          "tree_reselect: ONE click on the last-announced row after an outside pick selects it");
    CHECK(announceCount == 1,
          QStringLiteral("tree_reselect: ...and announces exactly once (got %1)")
              .arg(announceCount).toUtf8().constData());

    // ---- 2. the same through the SET leg (a multi from elsewhere) ----------
    panel.setSelectedSet({ cube });
    QApplication::processEvents();
    announced = iris::SceneNodePtr();
    CHECK(clickRow(QStringLiteral("Sphere")), "tree_reselect: Sphere row (set leg)");
    CHECK(announced == sphere,
          "tree_reselect: one click works after a SET selection made elsewhere");

    // ---- 3. editor.select(null), then click the CURRENT row ---------------
    // The deselect verb comes in as an empty set; the row Qt still holds as its
    // cursor must be clickable straight away.
    CHECK(clickRow(QStringLiteral("Cube")), "tree_reselect: the Cube row exists");
    CHECK(announced == cube, "tree_reselect: Cube is selected before the deselect");
    panel.setSelectedSet({});                    // editor.select(null)
    QApplication::processEvents();
    announced = iris::SceneNodePtr();
    CHECK(clickRow(QStringLiteral("Cube")),
          "tree_reselect: the Cube row survives the deselect");
    CHECK(announced == cube,
          "tree_reselect: one click on the current row after editor.select(null) selects it");

    // ---- 4. the panel's own gestures still announce -----------------------
    // paintSelection() must NOT record: a click on empty space paints the clear
    // and then announces it, and the Shift range paints its set and announces
    // it. Both would be swallowed by a cache written in the paint.
    announced = cube;
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(tree->viewport()->width() / 2,
                             tree->viewport()->height() - 4));   // below the last row
    QApplication::processEvents();
    CHECK(!announced,
          "tree_reselect: a click on empty space still announces the empty selection");

    CHECK(clickRow(QStringLiteral("Sphere")), "tree_reselect: Sphere row (shift range)");
    QTreeWidgetItem *cone = rowFor(tree, QStringLiteral("Cone"));
    if (cone) {
        QList<iris::SceneNodePtr> set;
        QObject::connect(&panel, &SceneHierarchyWidget::sceneNodeSetSelected,
                         [&set](const QList<iris::SceneNodePtr> &s) { set = s; });
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::ShiftModifier,
                          tree->visualItemRect(cone).center());
        QApplication::processEvents();
        CHECK(set.size() == 3,
              QStringLiteral("tree_reselect: the Shift range still announces its set (%1 members)")
                  .arg(set.size()).toUtf8().constData());
    }
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    enginetest::DocumentGraph graph("tree-reselect-ogre.log");
    QApplication app(argc, argv);
    run();
    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
