/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.tree_multiselect — THE OUTLINER'S MODIFIER SEMANTICS, ON THE REAL WIDGET
// (EDITOR_MULTISELECT_SPEC §2.2 / D1(b), gate table §5).
//
// The one claim nothing else can make. The tree runs
// QAbstractItemView::ExtendedSelection, so Qt applies ITS OWN Shift range —
// anchored on the CURRENT item — inside mousePressEvent, before any of our
// code sees the click. The owner's rule anchors on the TOPMOST SELECTED row
// instead, and the two disagree in exactly one case: after a Ctrl+click that
// left the current row BELOW an earlier-selected one. Rows 2 and 4 selected,
// Shift-click row 6:
//
//     Qt / Unreal / Blender  ->  {4, 5, 6}
//     the owner's rule       ->  {2, 3, 4, 5, 6}
//
// So this suite clicks REAL rows with REAL modifiers through QTest and reads
// back the set the panel announces. If the press-time override ever stops
// pre-empting Qt — the risk the spec flagged as unverified (§7) — this is the
// suite that says so, and it says so by NUMBER: five rows, not three.
//
// It also pins the two other tree contracts the set brought with it: the World
// root is never a member of a multi (D6), and the set SURVIVES a repopulate
// (the rebuild does clear(), and until this program nothing put the selection
// back — a Shift-range followed by any folder edit lost it).
//
// Offscreen QPA, no display, no engine rendering; the document nodes need the
// headless graph like every other document suite.

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

QStringList names(const QList<iris::SceneNodePtr> &nodes)
{
    QStringList out;
    for (const auto &n : nodes) out.append(n ? n->getName() : QStringLiteral("<null>"));
    return out;
}

/// Clicks the row for `node`, with modifiers, the way a user does.
void clickRow(QTreeWidget *tree, QTreeWidgetItem *item, Qt::KeyboardModifiers mods)
{
    tree->scrollToItem(item);
    const QRect r = tree->visualItemRect(item);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, mods, r.center());
}

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
    QList<iris::SceneNodePtr> nodes;
    for (int i = 1; i <= 6; ++i) {
        auto n = iris::SceneNode::create();
        n->setName(QStringLiteral("row%1").arg(i));
        root->addChild(n);
        nodes.append(n);
    }

    SceneHierarchyWidget panel;
    panel.resize(320, 600);
    panel.show();
    panel.setScene(scene);
    QTreeWidget *tree = panel.getWidget();
    tree->expandAll();
    QApplication::processEvents();

    QList<iris::SceneNodePtr> announced;
    QObject::connect(&panel, &SceneHierarchyWidget::sceneNodeSetSelected,
                     [&announced](const QList<iris::SceneNodePtr> &set) { announced = set; });
    QObject::connect(&panel, &SceneHierarchyWidget::sceneNodeSelected,
                     [&announced](iris::SceneNodePtr node) {
        announced.clear();
        if (node) announced.append(node);
    });

    QTreeWidgetItem *r2 = rowFor(tree, "row2");
    QTreeWidgetItem *r4 = rowFor(tree, "row4");
    QTreeWidgetItem *r6 = rowFor(tree, "row6");
    CHECK(r2 && r4 && r6, "tree_multiselect: the rows are there");
    if (!r2 || !r4 || !r6) return;

    // ---- plain click -----------------------------------------------------
    clickRow(tree, r2, Qt::NoModifier);
    QApplication::processEvents();
    CHECK(names(announced) == QStringList({ "row2" }),
          "tree_multiselect: a plain click selects one row");

    // ---- Ctrl+click adds -------------------------------------------------
    clickRow(tree, r4, Qt::ControlModifier);
    QApplication::processEvents();
    CHECK(announced.size() == 2 && announced.first()->getName() == QStringLiteral("row4"),
          QStringLiteral("tree_multiselect: Ctrl+click adds a row and makes it the primary (%1)")
              .arg(names(announced).join(',')).toUtf8().constData());

    // ---- Shift+click: THE OWNER'S RULE ----------------------------------
    clickRow(tree, r6, Qt::ShiftModifier);
    QApplication::processEvents();
    CHECK(announced.size() == 5,
          QStringLiteral("tree_multiselect: Shift ranges from the TOPMOST SELECTED row "
                         "(rows 2..6 = 5, not Qt's 4..6 = 3) — got %1: %2")
              .arg(announced.size()).arg(names(announced).join(',')).toUtf8().constData());
    CHECK(!announced.isEmpty() && announced.first()->getName() == QStringLiteral("row6"),
          "tree_multiselect: the CLICKED row is the primary after a range");
    {
        QStringList got = names(announced);
        got.sort();
        CHECK(got == QStringList({ "row2", "row3", "row4", "row5", "row6" }),
              "tree_multiselect: the range is exactly rows 2..6, inclusive on both ends");
    }

    // ---- Shift UPWARDS: [min..max], the case every system agrees on ------
    clickRow(tree, r4, Qt::NoModifier);
    QApplication::processEvents();
    clickRow(tree, r2, Qt::ShiftModifier);
    QApplication::processEvents();
    CHECK(announced.size() == 3, "tree_multiselect: an upward Shift range is [min..max] too");

    // ---- the World root is never in a multi (D6) -------------------------
    QTreeWidgetItem *rootRow = rowFor(tree, root->getName());
    CHECK(rootRow != nullptr, "tree_multiselect: the World row exists");
    if (rootRow) {
        clickRow(tree, r4, Qt::NoModifier);
        QApplication::processEvents();
        clickRow(tree, rootRow, Qt::ControlModifier);
        QApplication::processEvents();
        bool hasRoot = false;
        for (const auto &n : announced) if (n && n->isRootNode()) hasRoot = true;
        CHECK(!hasRoot, "tree_multiselect: Ctrl+clicking the World root never puts it in a set");
    }

    // ---- the set survives a rebuild --------------------------------------
    clickRow(tree, r2, Qt::NoModifier);
    QApplication::processEvents();
    clickRow(tree, r6, Qt::ShiftModifier);
    QApplication::processEvents();
    const int before = tree->selectedItems().size();
    panel.repopulateTree();
    QApplication::processEvents();
    CHECK(tree->selectedItems().size() == before,
          QStringLiteral("tree_multiselect: the selection SURVIVES a repopulate (%1 rows before, "
                         "%2 after)").arg(before).arg(tree->selectedItems().size())
              .toUtf8().constData());

    // ---- S9: AN ASSET IS ONE ROW, on the real widget ---------------------
    //
    // The document half of this rule is asserted by editor.outlinerRows in
    // scripting.e2e.one_asset; THIS is the widget, where it has to be true by
    // NOT CREATING the rows — a hidden row would still be a member of a Shift
    // range (EDITOR_MULTISELECT_SPEC §2.2), which is exactly the interaction
    // the spec flagged.
    {
        auto assetRoot = iris::SceneNode::create();
        assetRoot->setName(QStringLiteral("character"));
        root->addChild(assetRoot);
        for (int i = 1; i <= 3; ++i) {
            auto part = iris::SceneNode::create();
            part->setName(QStringLiteral("part%1").arg(i));
            part->setAttached(true);              // what an import marks its insides
            assetRoot->addChild(part);
            auto deep = iris::SceneNode::create();
            deep->setName(QStringLiteral("deep%1").arg(i));
            deep->setAttached(true);
            part->addChild(deep);
        }
        panel.repopulateTree();
        QApplication::processEvents();

        CHECK(rowFor(tree, "character") != nullptr, "one_asset: the asset root has a row");
        CHECK(rowFor(tree, "part1") == nullptr && rowFor(tree, "deep1") == nullptr,
              "one_asset: its attached parts (and their children) have NO rows");
        const int rows = panel.visibleNodeRows().size();
        CHECK(rows == 8,
              QStringLiteral("one_asset: 6 loose rows + the World root + ONE asset row = 8 "
                             "(got %1) — the document has %2 nodes under the asset")
                  .arg(rows).arg(6).toUtf8().constData());

        // A part arriving after the tree exists (the incremental insert the add
        // command uses) gets no row either.
        auto late = iris::SceneNode::create();
        late->setName(QStringLiteral("latePart"));
        late->setAttached(true);
        assetRoot->addChild(late);
        panel.insertChild(late);
        QApplication::processEvents();
        CHECK(rowFor(tree, "latePart") == nullptr,
              "one_asset: an attached node inserted incrementally gets no row either");

        // Detaching is how a user takes a part OUT of its asset: the row appears.
        late->setAttached(false);
        panel.repopulateTree();
        QApplication::processEvents();
        CHECK(rowFor(tree, "latePart") != nullptr,
              "one_asset: detaching a part gives it a row of its own");
    }

    scene.reset();
}

} // namespace

int main(int argc, char *argv[])
{
    enginetest::DocumentGraph graph("tree-multiselect-ogre.log");
    QApplication app(argc, argv);
    run();
    std::printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
