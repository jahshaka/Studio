/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.hierarchy_root — THE WORLD ROW IS GONE FROM THE OUTLINER (lane
// RIGHT-TABS-1, PROPERTY_FILTER_SPEC §4).
//
// The tree's top level used to be ONE row — the scene root, with a globe icon —
// and everything in the scene hung off it. That row existed because selecting
// it was the only way to see the World settings; those are a TAB of the
// properties column now (ui.properties_tabs), so the row is deleted and the
// tree's top level IS the scene's root level.
//
// What it carried and where each duty went:
//   * "show the World settings"   -> the World tab (not this suite)
//   * drop ONTO it = reparent to the root  -> a drop on the tree's EMPTY AREA
//   * drop onto it = leave the folder      -> empty area too (unchanged law)
//   * the anchor of folderItemFor("")      -> the tree's invisible root item
//   * a lock-column click on it unlocked EVERY node in the scene (an accident
//     of an unset flag) -> deleted with the row, and nothing replaces it
//   * D6's five "the root is never in a multi" filters -> deleted; no row
//     carries the root, so no set built from rows can contain it
//
// Offscreen QPA, no display, no engine; document nodes on the headless graph.

#include <QApplication>
#include <QKeyEvent>
#include <QTest>
#include <QTreeWidget>

#include <cstdio>

#include "../support/documentgraph.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/scenefolders.h"
#include "ui/panels/scenehierarchywidget.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS %s\n", name); } \
    else { std::printf("FAIL %s\n", name); ++failures; } \
} while (0)

namespace {

using Hint = SceneTreeWidget::DropHint;

QTreeWidgetItem *rowFor(QTreeWidget *tree, const QString &name)
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->text(0) == name) return *it;
    return nullptr;
}

iris::SceneNodePtr addChild(const iris::SceneNodePtr &parent, const QString &name)
{
    auto n = iris::SceneNode::create();
    n->setName(name);
    parent->addChild(n);
    return n;
}

void run()
{
    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();
    root->setName(QStringLiteral("World"));
    auto cube   = addChild(root, QStringLiteral("Cube"));
    auto sphere = addChild(root, QStringLiteral("Sphere"));
    auto childOfCube = addChild(cube, QStringLiteral("Nested"));

    SceneHierarchyWidget panel;
    panel.resize(320, 600);
    panel.show();
    panel.setScene(scene);
    QTreeWidget *tree = panel.getWidget();
    tree->expandAll();
    QApplication::processEvents();

    // ---- 1. no row carries the root --------------------------------------
    CHECK(rowFor(tree, QStringLiteral("World")) == nullptr,
          "hierarchy_root: the World row is gone");
    bool rootIsARow = false;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toLongLong() == root->getNodeId()) rootIsARow = true;
    CHECK(!rootIsARow, "hierarchy_root: no row carries the root's id");

    // The scene's objects are TOP-LEVEL rows now.
    CHECK(tree->topLevelItemCount() == 2,
          QStringLiteral("hierarchy_root: the scene's two objects are top-level rows (got %1)")
              .arg(tree->topLevelItemCount()).toUtf8().constData());
    QTreeWidgetItem *cubeRow = rowFor(tree, QStringLiteral("Cube"));
    CHECK(cubeRow && cubeRow->parent() == nullptr,
          "hierarchy_root: a root-level object is a TOP-LEVEL item (no World parent)");
    QTreeWidgetItem *nestedRow = rowFor(tree, QStringLiteral("Nested"));
    CHECK(nestedRow && nestedRow->parent() == cubeRow,
          "hierarchy_root: a nested object is still under its own parent");
    bool rootInRows = false;
    for (const auto &n : panel.visibleNodeRows()) if (n && n == root) rootInRows = true;
    CHECK(!rootInRows, "hierarchy_root: visibleNodeRows excludes the root");

    // ---- 2. a drop on EMPTY SPACE reparents a nested node to the root -----
    // The gesture that used to be "drop onto the World row". The hint is the
    // decision the tree paints and acts on (dropHintAt): the Drop handler
    // resolves a Reparent with no row to the scene root.
    const QPoint emptySpace(tree->viewport()->width() / 2, tree->viewport()->height() - 4);
    CHECK(tree->itemAt(emptySpace) == nullptr, "hierarchy_root: that point really is empty");
    QTreeWidgetItem *hintRow = nullptr;
    QString folder;
    Hint hint = panel.dropHintAt({ childOfCube }, emptySpace, &hintRow, &folder);
    CHECK(hint == Hint::Reparent && hintRow == nullptr,
          "hierarchy_root: dropping a NESTED node on empty space reparents it to the root");

    // A root-level node that is FILED leaves its folder — metadata only, the
    // law empty space always had.
    scenefolders::create(scene, QStringLiteral("Props"));
    sphere->folderPath = QStringLiteral("Props");
    panel.repopulateTree();
    QApplication::processEvents();
    hint = panel.dropHintAt({ sphere }, emptySpace, &hintRow, &folder);
    CHECK(hint == Hint::ToRoot && folder.isEmpty(),
          "hierarchy_root: dropping a FILED root-level node on empty space leaves its folder");

    // A root-level node that is not filed has nothing to do there.
    hint = panel.dropHintAt({ cube }, emptySpace, &hintRow, &folder);
    CHECK(hint == Hint::None,
          "hierarchy_root: dropping an unfiled root-level node on empty space does nothing");

    // The folder row is a top-level row, anchored on the invisible root.
    QTreeWidgetItem *folderRow = rowFor(tree, QStringLiteral("Props"));
    CHECK(folderRow && folderRow->parent() == nullptr,
          "hierarchy_root: a folder row is a top-level row (folderItemFor anchors on the "
          "invisible root)");
    CHECK(rowFor(tree, QStringLiteral("Sphere")) &&
          rowFor(tree, QStringLiteral("Sphere"))->parent() == folderRow,
          "hierarchy_root: the filed object is under its folder row");

    // ---- 3. selecting the root is a no-op in the tree ---------------------
    QTreeWidgetItem *sphereRow = rowFor(tree, QStringLiteral("Sphere"));
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualItemRect(sphereRow).center());
    QApplication::processEvents();
    CHECK(tree->currentItem() == sphereRow, "hierarchy_root: the Sphere row is current");
    panel.setSelectedNode(root);              // editor.select(rootId) / the scene open
    QApplication::processEvents();
    CHECK(tree->currentItem() == sphereRow,
          "hierarchy_root: selecting the root highlights nothing (and does not crash)");

    // ---- 4. the lock cascade is unreachable ------------------------------
    // The lock column of the World row read an unset flag as false and called
    // setItemLocked(root, false) — every node in the scene unlocked, in one
    // stray click. With no row there is nothing to click; the per-node lock is
    // untouched.
    cube->setPickable(false);
    sphere->setPickable(false);
    panel.repopulateTree();
    QApplication::processEvents();
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, emptySpace);
    QApplication::processEvents();
    CHECK(!cube->isPickable() && !sphere->isPickable(),
          "hierarchy_root: a click on the tree's empty space unlocks nothing");
    cube->setPickable(true);
    sphere->setPickable(true);

    // ---- 5. keyboard Up from the first row stays on it --------------------
    panel.repopulateTree();
    QApplication::processEvents();
    QTreeWidgetItem *first = tree->topLevelItem(0);
    tree->setCurrentItem(first);
    QApplication::processEvents();
    QTest::keyClick(tree, Qt::Key_Up);
    QApplication::processEvents();
    CHECK(tree->currentItem() == first,
          "hierarchy_root: Up from the first row stays on it (there is no World row above)");
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    enginetest::DocumentGraph graph("hierarchy-root-ogre.log");
    QApplication app(argc, argv);
    run();
    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
