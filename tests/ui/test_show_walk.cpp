/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.show_walk — SHOWING THE PROPERTIES DOCK DESTROYS NOTHING WHILE QT IS STILL
// SHOWING IT (lane CREATE-CRASH-1).
//
// THE CRASH THIS PINS (spikes/d1-scale-fixtures/library-crash/): SIGSEGV in
// QWidgetPrivate::showChildren, reading a freed child's `data`, three levels
// under `sceneNodePropertiesDock->setVisible(true)` in
// MainWindow::applyDockVisibilityForSpace — a create's reveal switching the page
// back to the editor. The three levels are the dock's body, its QScrollArea and
// the scroll area's VIEWPORT, and the viewport's children are NOT just the
// column: Qlementine gives every focusable row a QFocusFrame and
// QFocusFrame::setWidget re-parents that frame into the enclosing scroll area's
// viewport. So the viewport holds the column AND one frame per focusable row
// (67 of them on a create's World tab), as siblings.
//
// QWidgetPrivate::showChildren walks a SNAPSHOT of that child list, showing the
// column first — and the column's showEvent paid its owed mount right there,
// inline (flushPendingMount -> mountNow -> bindScene -> a blade rebuild ->
// AccordianBladeWidget::clearPanel). clearPanel frees the rows it retired
// kRetiredGenerations rebuilds ago if they are still alive, and a freed row
// takes its focus frame with it (qlementine's WidgetWithFocusFrameEventFilter
// deletes the frame it owns). That frame is a LATER sibling in the snapshot the
// walk is still holding: the next iteration dereferences it.
//
// Retired rows are alive when a blade was rebuilt kRetiredGenerations times
// with no DeferredDelete delivered in between (Qt delivers one only when
// control returns below the loop/scope level it was posted at — a nested pump
// running at the poster's own level does not). This suite builds that state
// directly (three rebinds with no loop turn) and then shows the dock: the
// interleave the create hit by timing, made deterministic. The frame stack of
// the fault is the create loop's, frame for frame (spikes/create-crash-1/).
//
// THE RULE IT PINS: the column's show-time mount is paid AFTER the show — the
// same turn, never inside Qt's child walk. Asserted without relying on a crash:
// every QFocusFrame in the viewport is watched, and none may be destroyed
// between the dock's setVisible(true) call and its return. The same arm then
// checks the product behaviour survived: the mount still happens, once, before
// the turn ends, for the scene the column was re-pointed at.
//
// The real SceneNodePropertiesWidget and blades under the real Qlementine style
// (no style, no focus frames, no crash), in a QMainWindow dock shaped the way
// MainWindow builds it. Offscreen QPA, no display.

#include <QApplication>
#include <QDockWidget>
#include <QFocusFrame>
#include <QFrame>
#include <QMainWindow>
#include <QPointer>
#include <QScrollArea>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "services/services.h"
#include "services/undoservice.h"
#include "ui/controls/accordionbladewidget.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/style/thememanager.h"
#include "data/settingsmanager.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

/// How many retired rows the column's blades are still holding alive.
int liveRetiredRows(QWidget *panel)
{
    int n = 0;
    for (AccordianBladeWidget *blade : panel->findChildren<AccordianBladeWidget *>())
        n += blade->retiredRowCount();
    return n;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    enginetest::DocumentGraph graph("ui-show-walk-ogre.log");
    if (!graph.require()) return 1;

    // THE STYLE IS THE PRECONDITION: the focus frames exist only under Qlementine.
    auto *settings = SettingsManager::getDefaultManager();
    settings->settings->remove(ThemeManager::settingsKey());
    ThemeManager::applyAtStartup(app);

    QUndoStack stack;
    UndoService undo(&stack);
    StudioServices services;
    services.undo = &undo;

    auto sceneA = iris::Scene::create();
    auto sceneB = iris::Scene::create();
    auto sceneC = iris::Scene::create();

    // ---- the dock, the way MainWindow::setupDockWidgets builds it ------------
    // dock -> body -> QScrollArea -> viewport -> the column: the crash's levels.
    QMainWindow win;
    win.resize(1400, 900);
    auto *dock = new QDockWidget(QStringLiteral("Properties"), &win);
    dock->setObjectName(QStringLiteral("sceneNodePropertiesDock"));
    auto *body = new QWidget(dock);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    body->setFixedWidth(400);
    auto *scroll = new QScrollArea(body);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidgetResizable(true);
    auto *panel = new SceneNodePropertiesWidget;
    panel->setServices(&services);
    panel->setDatabase(nullptr);
    panel->setProject(nullptr);
    panel->setScene(sceneA);
    scroll->setWidget(panel);
    bodyLayout->addWidget(scroll);
    dock->setWidget(body);
    win.addDockWidget(Qt::RightDockWidgetArea, dock);
    win.show();

    // A focus frame attaches one turn after its widget's FIRST PAINT, and the
    // offscreen platform never paints on its own: a turn is a grab plus a
    // drained queue.
    auto turn = [&]() {
        win.grab();
        QApplication::processEvents();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    panel->setSceneNode(sceneA->getRootNode());   // the World tab, as a create opens on
    for (int i = 0; i < 6; ++i) turn();

    QWidget *viewport = scroll->viewport();
    QList<QPointer<QFocusFrame>> frames;
    for (QObject *child : viewport->children())
        if (auto *frame = qobject_cast<QFocusFrame *>(child)) frames.append(frame);
    std::printf("  the viewport holds the column and %d focus frame(s)\n", int(frames.size()));
    CHECK(frames.size() > 10,
          "precondition: the rows' focus frames are the COLUMN'S SIBLINGS in the viewport");

    // ---- the page leaves; the column is rebound three times with no loop turn ---
    dock->hide();
    turn();
    const int mountsBefore = panel->mountCount();
    panel->setScene(sceneB);
    panel->setScene(sceneA);
    panel->setScene(sceneB);
    const int live = liveRetiredRows(panel);
    std::printf("  retired rows still alive after three rebinds in one scope: %d\n", live);
    CHECK(live > 0, "precondition: the blades' rings hold retired rows that are still alive");
    // ...and the column is re-pointed without binding: the world root of another
    // scene (a reveal's selectRoot shape). Hidden, so the mount is OWED to the show.
    panel->setSceneNode(sceneC->getRootNode());
    CHECK(panel->mountIsPending(), "precondition: the hidden column owes its mount to the show");

    // ---- THE SHOW: nothing in the viewport may die while Qt is walking it ------
    int diedInsideTheShow = 0;
    bool inShow = false;
    for (const QPointer<QFocusFrame> &frame : std::as_const(frames))
        if (frame)
            QObject::connect(frame.data(), &QObject::destroyed, [&]() { if (inShow) ++diedInsideTheShow; });
    inShow = true;
    dock->setVisible(true);        // applyDockVisibilityForSpace's call
    inShow = false;
    std::printf("  focus frames destroyed inside the dock's show: %d\n", diedInsideTheShow);
    CHECK(diedInsideTheShow == 0,
          "the show destroys NO sibling the walk still holds (the create-loop SIGSEGV)");

    // ---- ...and the product behaviour is unchanged: the column fills this turn --
    turn();
    CHECK(panel->mountCount() == mountsBefore + 1,
          QStringLiteral("the owed mount is paid ONCE by the end of the show's turn (%1)")
              .arg(panel->mountCount() - mountsBefore).toUtf8().constData());
    CHECK(!panel->mountIsPending(), "...with no debt left over");
    const auto rows = panel->propertyRows(SceneNodePropertiesWidget::Tab::World);
    CHECK(!rows.isEmpty(), "...and the World tab lists its rows");
    for (int i = 0; i < 3; ++i) turn();
    CHECK(liveRetiredRows(panel) == 0, "the retired rows are gone once the loop turns");

    std::printf(failures ? "ui.show_walk: %d failure(s)\n" : "ui.show_walk: ok\n", failures);
    return failures ? 1 : 0;
}
