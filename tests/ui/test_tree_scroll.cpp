/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.tree_scroll — A CLICK IN THE OUTLINER NEVER MOVES THE OUTLINER (owner
// report, 2026-09-14: "when the left-column list grows beyond the widget the
// clicks become broken — it jumps items; when there are fewer items than fit I
// can click them all, no issues").
//
// THE ROUND TRIP IS THE SUBJECT. A click in this tree does not stay in it: it
// is announced (treeSelectionChanged -> sceneNodeSelected), the shell hands it
// to SelectionService, and the service hands it back to the panel through
// MainWindow::applySelectionToUi -> SceneHierarchyWidget::setSelectedNode. That
// return leg used to scrollTo(PositionAtCenter), which SCROLLS THE LIST UNDER
// THE CURSOR on every click — the row just picked jumps to the middle of the
// viewport, so the next click lands on a different node. With fewer rows than
// fit there is nothing to scroll, which is why the defect only appeared in
// scenes big enough to scroll the dock.
//
// So this suite wires the return leg exactly as the shell does, clicks real
// rows with QTest in a tree that REALLY scrolls, and asserts the viewport did
// not move — and that a node selected from somewhere else (the viewport pick)
// is still revealed when it is off screen.
//
// It also pins the invariant the whole class of defect is measured against:
// once scrolled, `indexAt(point)` must name the row `visualRect` paints at that
// point, at the top, the middle and the bottom of the viewport.
//
// Offscreen QPA, no display, no engine; document nodes on the headless graph.

#include <QApplication>
#include <QScrollBar>
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
    for (int i = 1; i <= 60; ++i) {
        auto n = iris::SceneNode::create();
        n->setName(QStringLiteral("row%1").arg(i, 2, 10, QLatin1Char('0')));
        root->addChild(n);
    }

    SceneHierarchyWidget panel;
    // Short on purpose: 60 rows in a dock this tall is the owner's scene — a
    // list that scrolls. (The same suite with a tall panel is the control: see
    // the last case.)
    panel.resize(320, 260);
    panel.show();
    panel.setScene(scene);
    QTreeWidget *tree = panel.getWidget();
    tree->expandAll();
    QApplication::processEvents();

    // THE SHELL'S RETURN LEG, wired the way MainWindow::applySelectionToUi
    // wires it: whatever the panel announces comes straight back in.
    iris::SceneNodePtr announced;
    QObject::connect(&panel, &SceneHierarchyWidget::sceneNodeSelected,
                     [&panel, &announced](iris::SceneNodePtr node) {
        announced = node;
        panel.setSelectedNode(node);            // <- the leg that used to scroll
    });
    QObject::connect(&panel, &SceneHierarchyWidget::sceneNodeSetSelected,
                     [&panel](const QList<iris::SceneNodePtr> &set) {
        panel.setSelectedSet(set);
    });

    QScrollBar *bar = tree->verticalScrollBar();
    CHECK(bar->maximum() > 0, "tree_scroll: the list really does scroll (more rows than fit)");
    if (bar->maximum() <= 0) return;

    // ---- 1. a click on a visible row leaves the list where it is ----------
    // Scrolled into the middle of the list, so a re-centre would be visible in
    // BOTH directions (a row above the centre scrolls the list up, one below
    // scrolls it down) — and so the test cannot pass by accident at an end
    // stop, where PositionAtCenter is clamped and moves nothing.
    bar->setValue(bar->maximum() / 2);
    QApplication::processEvents();

    const int rowsVisible = tree->viewport()->height() / qMax(1, tree->visualItemRect(
                                tree->itemAt(QPoint(4, 4))).height());
    std::printf("    %d rows visible, scroll %d/%d\n", rowsVisible, bar->value(), bar->maximum());

    struct Probe { const char *name; QPoint point; };
    const int h = tree->viewport()->height();
    const QPoint probes[3] = { QPoint(40, 4), QPoint(40, h / 2), QPoint(40, h - 6) };
    const char *where[3] = { "top", "middle", "bottom" };

    for (int i = 0; i < 3; ++i) {
        const int before = bar->value();
        QTreeWidgetItem *aimed = tree->itemAt(probes[i]);
        CHECK(aimed != nullptr,
              QStringLiteral("tree_scroll: there is a row at the %1 of the scrolled viewport")
                  .arg(QLatin1String(where[i])).toUtf8().constData());
        if (!aimed) continue;
        const QString wanted = aimed->text(0);

        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, probes[i]);
        QApplication::processEvents();

        CHECK(bar->value() == before,
              QStringLiteral("tree_scroll: clicking the %1 row does not scroll the list "
                             "(%2 -> %3)")
                  .arg(QLatin1String(where[i])).arg(before).arg(bar->value())
                  .toUtf8().constData());
        CHECK(announced && announced->getName() == wanted,
              QStringLiteral("tree_scroll: the %1 row selects the node under the cursor (%2, got %3)")
                  .arg(QLatin1String(where[i]), wanted,
                       announced ? announced->getName() : QStringLiteral("<none>"))
                  .toUtf8().constData());
        // ...and the row the click landed on is still the row painted there.
        QTreeWidgetItem *nowThere = tree->itemAt(probes[i]);
        CHECK(nowThere == aimed,
              QStringLiteral("tree_scroll: the %1 row is still the row at that point after the click")
                  .arg(QLatin1String(where[i])).toUtf8().constData());
    }

    // ---- 2. indexAt == the row visualRect paints, once scrolled ------------
    // The invariant every "clicks land on the wrong row" defect is measured
    // against: hit testing and painting must agree about the scroll offset.
    bar->setValue(bar->maximum() / 3);
    QApplication::processEvents();
    for (int i = 0; i < 3; ++i) {
        QTreeWidgetItem *item = tree->itemAt(probes[i]);
        if (!item) continue;
        const QRect painted = tree->visualItemRect(item);
        CHECK(painted.contains(probes[i]),
              QStringLiteral("tree_scroll: indexAt(%1) is the row painted there (rect %2..%3)")
                  .arg(probes[i].y()).arg(painted.top()).arg(painted.bottom())
                  .toUtf8().constData());
    }

    // ---- 3. a node selected from ELSEWHERE is still revealed ---------------
    // The reason the scroll call exists at all: picking a node in the 3D
    // viewport has to bring its row on screen. EnsureVisible does that; what it
    // does not do is move a row that is already visible.
    bar->setValue(0);
    QApplication::processEvents();
    QTreeWidgetItem *far = rowFor(tree, QStringLiteral("row55"));
    CHECK(far != nullptr, "tree_scroll: row55 exists");
    if (far) {
        CHECK(!tree->viewport()->rect().intersects(tree->visualItemRect(far)),
              "tree_scroll: row55 starts off screen");
        const auto node = scene->getRootNode()->children().at(54);
        panel.setSelectedNode(node);            // the viewport-pick path
        QApplication::processEvents();
        CHECK(tree->viewport()->rect().intersects(tree->visualItemRect(far)),
              "tree_scroll: selecting an off-screen node scrolls its row into view");
    }

    // ---- 4. the control: a list that FITS never scrolls at all ------------
    // The owner's own isolation, as an assertion ("when the number of items is
    // less than the widget I can click them all, no issues").
    panel.resize(320, 1400);
    QApplication::processEvents();
    if (bar->maximum() == 0) {
        const int before = bar->value();
        QTreeWidgetItem *third = rowFor(tree, QStringLiteral("row03"));
        if (third) {
            QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                              tree->visualItemRect(third).center());
            QApplication::processEvents();
            CHECK(bar->value() == before && announced && announced->getName() == "row03",
                  "tree_scroll: with every row on screen the click still selects its own row");
        }
    }
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    enginetest::DocumentGraph graph("tree-scroll-ogre.log");
    QApplication app(argc, argv);
    run();
    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
