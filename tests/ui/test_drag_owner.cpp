/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.drag_owner — ONE DRAG OWNER PER ITEM VIEW (src/ui/panels/singledragowner.h).
//
// THE DEFECT, reported three times: an item in the preset panels or the asset
// browser sticks to the cursor with no button held. Root cause, traced
// 2026-09-07: the panels run their OWN QDrag from an event filter, but the view
// underneath was left with `dragEnabled` on (setDragDropMode(DragDrop) turns it
// on as a side effect). So QAbstractItemView::mouseMoveEvent put the view into
// DraggingState behind the filter's back, and the release that ended the
// filter's QDrag::exec() was consumed inside exec's nested event loop — the
// view never saw it and stayed in DraggingState FOREVER. The next time the
// pointer entered the panel, the first mouse move took the branch at the top of
// mouseMoveEvent (which tests the STATE, not the buttons) and started a drag
// from nothing.
//
// This suite is the RED-FIRST proof and the regression pin, on a QListWidget
// configured exactly as the panels configure theirs. The probe subclass exists
// only to read `state()`, which QAbstractItemView keeps protected.
//
// The suite is deliberately NOT the panels themselves: AssetModelPanel pulls in
// MainWindow, the Database and the whole shell. The BEHAVIOUR under test
// belongs to QAbstractItemView and to singledragowner.h, and both are here.

#include <QApplication>
#include <QListWidget>
#include <QMouseEvent>
#include <QTest>

#include <cstdio>

#include "ui/panels/singledragowner.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS %s\n", name); } \
    else { std::printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__); ++failures; } \
} while (0)

namespace {

/// The only thing this adds is public access to the two protected members that
/// ARE the bug: the view's drag state machine.
class ProbeList : public QListWidget
{
public:
    using QListWidget::state;
    using QListWidget::setState;
    // The State enum is protected too, so the values need re-exporting as well.
    using QListWidget::State;
    static constexpr State kNoState   = QListWidget::NoState;
    static constexpr State kDragging  = QListWidget::DraggingState;
};

/// The panels' configuration, verbatim (assetpanel.h + assetmodelpanel.cpp).
ProbeList *makePanelStyleView()
{
    auto *view = new ProbeList;
    view->setViewMode(QListWidget::IconMode);
    view->setIconSize(QSize(64, 64));
    view->setResizeMode(QListWidget::Adjust);
    view->setMovement(QListView::Static);
    view->setSelectionBehavior(QAbstractItemView::SelectItems);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setSpacing(4);
    view->setDragDropMode(QAbstractItemView::DragDrop);
    view->addItem("Cube");
    view->addItem("Sphere");
    view->resize(400, 200);
    view->show();
    QTest::qWaitForWindowExposed(view);
    return view;
}

void press(QWidget *target, const QPoint &pos)
{
    QMouseEvent e(QEvent::MouseButtonPress, QPointF(pos), target->mapToGlobal(QPointF(pos)),
                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &e);
}

void move(QWidget *target, const QPoint &pos, Qt::MouseButtons buttons)
{
    QMouseEvent e(QEvent::MouseMove, QPointF(pos), target->mapToGlobal(QPointF(pos)),
                  Qt::NoButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &e);
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // ---- RED: the machinery the panels were fighting -----------------------
    // With the view armed exactly as the panels used to leave it, a press on an
    // item plus one mouse move is enough to put it in DraggingState. Everything
    // after that is the view's drag, not the panel's.
    {
        ProbeList *view = makePanelStyleView();
        CHECK(view->dragEnabled(),
              "RED: setDragDropMode(DragDrop) arms the view's own drag (dragEnabled)");

        const QPoint on = view->visualItemRect(view->item(0)).center();
        press(view->viewport(), on);
        move(view->viewport(), on + QPoint(3, 0), Qt::LeftButton);
        CHECK(view->state() == ProbeList::kDragging,
              "RED: one move over an item puts the armed view into DraggingState");

        // ...and the state SURVIVES a gesture whose release the panel's
        // QDrag::exec() swallowed. This is exactly the sticky item: the view is
        // still in DraggingState with no button held.
        CHECK(view->state() == ProbeList::kDragging,
              "RED: with the release consumed, DraggingState outlives the gesture");
        move(view->viewport(), on + QPoint(60, 0), Qt::NoButton);
        std::printf("    state after a buttonless move: %d (DraggingState = %d)\n",
                    int(view->state()), int(ProbeList::kDragging));
        CHECK(view->state() == ProbeList::kDragging,
              "RED: and a buttonless move LEAVES IT ARMED — the view is still in the state "
              "whose only exit is startDrag(), which IS the item glued to the cursor");
        delete view;
    }

    // ---- GREEN part 1: disarmViewDrag ---------------------------------------
    {
        ProbeList *view = makePanelStyleView();
        singledrag::disarmViewDrag(view);
        CHECK(!view->dragEnabled(), "disarmViewDrag clears dragEnabled");
        CHECK(view->acceptDrops(),
              "and leaves DROPS alone — the asset browser still accepts file URLs");
        // dragDropMode() is DERIVED from (dragEnabled, acceptDrops) in Qt, so
        // clearing the first is exactly the statement "this view drops but does
        // not drag" — which is the contract, spelled by Qt itself.
        CHECK(view->dragDropMode() == QAbstractItemView::DropOnly,
              "the view now reads as DropOnly: drops in, no drag of its own");

        const QPoint on = view->visualItemRect(view->item(0)).center();
        press(view->viewport(), on);
        move(view->viewport(), on + QPoint(3, 0), Qt::LeftButton);
        CHECK(view->state() != ProbeList::kDragging,
              "a disarmed view NEVER enters DraggingState — the sticky state cannot exist");
        move(view->viewport(), on + QPoint(60, 0), Qt::NoButton);
        CHECK(view->state() != ProbeList::kDragging,
              "and a buttonless move over it starts nothing");
        delete view;
    }

    // ---- GREEN part 2: clearViewPressState ---------------------------------
    // The belt to the braces: even if something puts the view into a pressed
    // state, the call the panels make after QDrag::exec() returns ends it.
    {
        ProbeList *view = makePanelStyleView();
        singledrag::disarmViewDrag(view);

        const QPoint on = view->visualItemRect(view->item(0)).center();
        press(view->viewport(), on);
        view->setState(ProbeList::kDragging);   // force the worst case
        CHECK(view->state() == ProbeList::kDragging, "forced into DraggingState");

        int clicked = 0;
        QObject::connect(view, &QListWidget::itemClicked, [&clicked](QListWidgetItem *) { ++clicked; });
        singledrag::clearViewPressState(view);
        CHECK(view->state() == ProbeList::kNoState,
              "clearViewPressState returns the view to NoState");
        CHECK(clicked == 0,
              "and emits no click — the synthetic release lands outside every item, "
              "because a drag is not a click");
        delete view;
    }

    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
