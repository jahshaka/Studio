/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.dock_state — THE EDITOR'S DOCK LAYOUT SURVIVES A RESTART (hygiene lane,
// 2026-09-09).
//
// MainWindow saved `geometry` and `windowState`, and restored both — but the
// editor's docks (Hierarchy, Properties, Presets, Tray, Timeline) live
// in a NESTED QMainWindow (`viewPort`), whose saveState() was never called at
// all. Every dock the user moved, resized, floated, tabbed or closed came back
// to the compiled-in layout on the next launch, and the loss was invisible
// because the OUTER window's geometry did come back.
//
// The round trip is what this asserts, on the real helper (shell/dockstate.h)
// against a real QSettings file and a real nested QMainWindow built the way the
// shell builds one: docks with objectNames, a Presets dock split under
// Properties in the right column. No engine, no project, no display.
//
// It also pins the two rules the shell depends on:
//   * restore() returns FALSE when there is nothing stored — that "false" is
//     what tells MainWindow to apply the compiled-in default width instead
//     (applyColumnWidthsOnce), so a first run and a restored run differ;
//   * a blob written at a different layout VERSION is refused rather than
//     half-applied, which is how a future default layout reaches users who
//     already have a saved one;
//   * A LAYOUT WITH NO PANELS AT ALL IS REFUSED (lane SPACE-1, 2026-09-14).
//     The editor's docks are hidden whenever another space is showing, and the
//     exit path stored whatever they were doing — so quitting from the Player
//     saved "every panel closed" as the editor's layout, and the next launch
//     restored an editor with nothing but the 3D view. The helper now puts the
//     window back exactly as it was and answers false, which is the caller's
//     existing signal to use the default layout. The two shell rules that go
//     with it are modelled at the end of this file: the snapshot is taken while
//     the docks are UP (on the way out of the editor space), and an empty blob
//     stores nothing rather than overwriting a good one.

#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QAction>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <cstdio>

#include "shell/dockstate.h"
#include "ui/style/panelmetrics.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } \
} while (0)

namespace {

/// The editor's dock arrangement, as shell/mainwindow.cpp builds it: Hierarchy
/// on the left, Properties on the right with Presets split under it, and the
/// bottom area's THREE — Assets, the Timeline and the script console — in one
/// tab group whose bar sits at the TOP (lane SPACE-2), the console closed until
/// Ctrl+` asks for it.
struct Shell {
    QMainWindow window;
    QDockWidget *hierarchy;
    QDockWidget *properties;
    QDockWidget *presets;
    QDockWidget *assets;
    QDockWidget *timeline;
    QDockWidget *console;

    QDockWidget *makeDock(const char *name, const char *title)
    {
        auto *d = new QDockWidget(QString::fromLatin1(title), &window);
        d->setObjectName(QString::fromLatin1(name));   // restoreState matches on this
        d->setWidget(new QWidget(d));
        return d;
    }

    Shell()
    {
        window.setWindowFlags(Qt::Widget);
        window.setCentralWidget(new QWidget(&window));
        hierarchy  = makeDock("sceneHierarchyDock", "Hierarchy");
        properties = makeDock("sceneNodePropertiesDock", "Properties");
        presets    = makeDock("presetsDock", "Presets");
        assets     = makeDock("assetDock", "Assets");
        timeline   = makeDock("animationDock", "Timeline");
        console    = makeDock("scriptConsoleDock", "Console");
        window.addDockWidget(Qt::LeftDockWidgetArea, hierarchy);
        window.addDockWidget(Qt::RightDockWidgetArea, properties);
        window.splitDockWidget(properties, presets, Qt::Vertical);
        window.addDockWidget(Qt::BottomDockWidgetArea, assets);
        window.addDockWidget(Qt::BottomDockWidgetArea, timeline);
        window.addDockWidget(Qt::BottomDockWidgetArea, console);
        window.tabifyDockWidget(assets, timeline);
        window.tabifyDockWidget(timeline, console);
        window.setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
        console->hide();
        assets->raise();
        window.resize(1600, 900);
    }
};

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QTemporaryDir scratch;
    const QString iniPath = scratch.filePath("dockstate.ini");

    // ---- 1. the DEFAULT layout is the one the shell describes ---------------
    {
        Shell shell;
        CHECK(shell.window.dockWidgetArea(shell.presets) == Qt::RightDockWidgetArea,
              "default: Presets opens in the RIGHT column, not along the bottom");
        CHECK(shell.window.dockWidgetArea(shell.properties) == Qt::RightDockWidgetArea,
              "default: ...the same column as Properties");
        CHECK(shell.window.dockWidgetArea(shell.hierarchy) == Qt::LeftDockWidgetArea,
              "default: Hierarchy is on the left");
        CHECK(shell.window.dockWidgetArea(shell.assets) == Qt::BottomDockWidgetArea &&
                  shell.window.dockWidgetArea(shell.timeline) == Qt::BottomDockWidgetArea &&
                  shell.window.dockWidgetArea(shell.console) == Qt::BottomDockWidgetArea,
              "default: Assets, the Timeline and the Console share the bottom area");
        CHECK(shell.window.tabifiedDockWidgets(shell.assets).contains(shell.timeline),
              "default: ...as TABS of one group, not as a split (lane SPACE-2)");
        CHECK(shell.window.tabPosition(Qt::BottomDockWidgetArea) == QTabWidget::North,
              "default: ...whose tab bar is at the TOP of the area, not Qt's bottom edge");
        CHECK(shell.console->isHidden(),
              "default: the console is CLOSED — a hidden dock has no tab");
    }

    // ---- 2. nothing stored: restore refuses, and says so --------------------
    {
        QSettings settings(iniPath, QSettings::IniFormat);
        Shell shell;
        CHECK(!DockState::restore(&shell.window, &settings, "viewportDockState"),
              "empty settings: restore() is false (the caller then applies the default)");
    }

    // ---- 3. the ROUND TRIP --------------------------------------------------
    // Move Presets somewhere the default layout never puts it, close a dock and
    // widen the right column; save; build a FRESH shell (a new "launch"); the
    // layout must come back, and it must not be the default any more.
    {
        QSettings settings(iniPath, QSettings::IniFormat);
        Shell shell;
        shell.window.addDockWidget(Qt::LeftDockWidgetArea, shell.presets);
        shell.window.splitDockWidget(shell.hierarchy, shell.presets, Qt::Vertical);
        shell.timeline->setVisible(false);
        shell.window.resizeDocks({ shell.properties },
                                 { PanelMetrics::rightColumnWidth + 140 }, Qt::Horizontal);
        shell.window.show();
        app.processEvents();
        const int savedWidth = shell.properties->width();
        DockState::save(&shell.window, &settings, "viewportDockState");
        settings.sync();
        CHECK(!settings.value("viewportDockState").toByteArray().isEmpty(),
              "round trip: a blob reaches the settings file");

        Shell relaunched;
        CHECK(DockState::restore(&relaunched.window, &settings, "viewportDockState"),
              "round trip: restore() accepts the blob");
        relaunched.window.show();
        app.processEvents();
        CHECK(relaunched.window.dockWidgetArea(relaunched.presets) == Qt::LeftDockWidgetArea,
              "round trip: the moved Presets dock comes back on the LEFT");
        CHECK(!relaunched.timeline->isVisible(),
              "round trip: a dock the user closed stays closed");
        CHECK(relaunched.hierarchy->isVisible() && relaunched.properties->isVisible(),
              "round trip: the docks that were open are open");
        // The width is the reason applyColumnWidthsOnce stands down for a
        // restored layout: a restored column is WIDER than the compiled-in
        // default, and re-applying the default would undo the user's drag.
        CHECK(relaunched.properties->width() > PanelMetrics::rightColumnWidth,
              "round trip: the widened right column comes back wider than the default");
        std::printf("    saved width %d, restored width %d, default %d\n",
                    savedWidth, relaunched.properties->width(),
                    PanelMetrics::rightColumnWidth);
    }

    // ---- 4. a blob from another layout VERSION is refused -------------------
    {
        QSettings settings(scratch.filePath("versioned.ini"), QSettings::IniFormat);
        Shell shell;
        settings.setValue("viewportDockState", shell.window.saveState(DockState::kVersion + 1));
        Shell fresh;
        CHECK(!DockState::restore(&fresh.window, &settings, "viewportDockState"),
              "version: a blob from a different layout version is refused, not half-applied");
        CHECK(fresh.window.dockWidgetArea(fresh.presets) == Qt::RightDockWidgetArea,
              "version: ...and the default layout is untouched by the refusal");
    }

    // ---- 5. a layout with NO PANELS is refused ------------------------------
    // The owner's 2026-09-14 report, at the helper: an editor that opens with
    // nothing but the 3D view. The stored blob is what the shell used to write
    // when the app was quit from the Player space (every dock hidden).
    {
        QSettings settings(scratch.filePath("empty-layout.ini"), QSettings::IniFormat);
        Shell saver;
        saver.window.show();
        app.processEvents();
        for (QDockWidget *d : { saver.hierarchy, saver.properties, saver.presets,
                                saver.assets, saver.timeline })
            d->setVisible(false);               // MainWindow::hideEditorPanels()
        app.processEvents();
        DockState::save(&saver.window, &settings, "viewportDockState");
        settings.sync();
        CHECK(!DockState::hasVisibleDock(&saver.window),
              "empty layout: the saved window really has no dock on screen");

        Shell relaunched;
        relaunched.window.show();
        app.processEvents();
        CHECK(!DockState::restore(&relaunched.window, &settings, "viewportDockState"),
              "empty layout: restore() REFUSES it (the caller then applies the default)");
        CHECK(relaunched.hierarchy->isVisible() && relaunched.properties->isVisible()
                  && relaunched.presets->isVisible() && relaunched.assets->isVisible(),
              "empty layout: ...and the window keeps the layout it had — panels and all");
        CHECK(relaunched.window.dockWidgetArea(relaunched.presets) == Qt::RightDockWidgetArea,
              "empty layout: the refusal leaves the DEFAULT arrangement untouched");
    }

    // ---- 6. one closed dock is not an empty layout --------------------------
    // The rule has to be "no panels at all", not "any panel closed": a user who
    // closes the Timeline must still get their layout back.
    {
        QSettings settings(scratch.filePath("one-closed.ini"), QSettings::IniFormat);
        Shell saver;
        saver.window.show();
        app.processEvents();
        saver.timeline->setVisible(false);
        app.processEvents();
        DockState::save(&saver.window, &settings, "viewportDockState");
        settings.sync();

        Shell relaunched;
        relaunched.window.show();
        app.processEvents();
        CHECK(DockState::restore(&relaunched.window, &settings, "viewportDockState"),
              "one closed dock: the layout is still restored");
        CHECK(!relaunched.timeline->isVisible() && relaunched.hierarchy->isVisible(),
              "one closed dock: ...with that dock closed and the rest open");
    }

    // ---- 7. the shell's two save-side rules ---------------------------------
    // MainWindow::captureEditorDockState() snapshots while the docks are UP —
    // on the way out of the editor space and before immersive fullscreen hides
    // them — and closeEvent stores THAT, not the live state of whatever page
    // the user quit from. store() of an empty blob leaves the stored layout
    // alone, which is what a session that never opened the editor must do.
    {
        QSettings settings(scratch.filePath("quit-from-player.ini"), QSettings::IniFormat);
        Shell shell;
        shell.window.show();
        app.processEvents();
        const QByteArray captured = DockState::snapshot(&shell.window);   // leaving the editor
        for (QDockWidget *d : { shell.hierarchy, shell.properties, shell.presets,
                                shell.assets, shell.timeline })
            d->setVisible(false);                                         // the player space
        app.processEvents();
        DockState::store(&settings, "viewportDockState", captured);       // closeEvent
        settings.sync();

        Shell relaunched;
        relaunched.window.show();
        app.processEvents();
        CHECK(DockState::restore(&relaunched.window, &settings, "viewportDockState"),
              "quit from the player: what was stored is the EDITOR's layout, and it restores");
        CHECK(relaunched.hierarchy->isVisible() && relaunched.assets->isVisible(),
              "quit from the player: ...with the panels the editor had");

        const QByteArray before = settings.value("viewportDockState").toByteArray();
        DockState::store(&settings, "viewportDockState", QByteArray());
        settings.sync();
        CHECK(settings.value("viewportDockState").toByteArray() == before,
              "a session with nothing to say (an empty snapshot) leaves the stored layout alone");
    }

    // ---- 8. A TAB THAT IS NOT IN FRONT IS NOT A CLOSED PANEL ---------------
    // The owner's 2026-09-14 report ("the timeline widget … is gone") and the
    // reading the shell's seed depends on, pinned at the Qt level so a Qt
    // upgrade that changes it fails HERE and not in the editor.
    //
    // MainWindow seeds `widgetStates` — which panels are open — from
    // `!dock->isHidden()` after restoring the saved layout. For a TABIFIED dock
    // that is not the front tab, Qt keeps the dock SHOWN and parks it
    // off-screen (a negative geometry) rather than hiding it, so isHidden() is
    // the right question and the off-screen x is what "is it the front tab"
    // means (QDockWidget emits visibilityChanged(geometry().right() >= 0) for
    // exactly this reason). The two obvious alternatives are asserted WRONG
    // below, because both were proposed: isVisible() and the toggleViewAction.
    {
        QSettings settings(scratch.filePath("tabbed.ini"), QSettings::IniFormat);
        Shell saver;
        saver.window.show();
        app.processEvents();
        saver.assets->raise();                       // Assets is the front tab
        app.processEvents();
        CHECK(!saver.timeline->isHidden(),
              "tabs: the Timeline behind the Assets tab is NOT hidden");
        CHECK(saver.timeline->geometry().right() < 0 && saver.assets->geometry().right() >= 0,
              "tabs: ...it is parked off-screen, which is how the front tab is told apart");
        DockState::save(&saver.window, &settings, "viewportDockState");
        settings.sync();

        Shell relaunched;
        CHECK(!relaunched.timeline->isHidden() && !relaunched.assets->isHidden(),
              "tabs: BEFORE the window is shown, neither tab reads as closed (the seed runs "
              "in the constructor, where isVisible() is false for every dock)");
        CHECK(!relaunched.assets->toggleViewAction()->isChecked(),
              "tabs: ...and toggleViewAction() is false for an OPEN dock there — not a "
              "substitute for isHidden()");
        CHECK(DockState::restore(&relaunched.window, &settings, "viewportDockState"),
              "tabs: the layout restores");
        CHECK(!relaunched.timeline->isHidden(),
              "tabs: and the Timeline is still OPEN after the restore — the seed reads it as "
              "a panel the user never closed (the owner's missing Timeline)");
        relaunched.window.show();
        app.processEvents();
        CHECK(!relaunched.timeline->isHidden() && !relaunched.assets->isHidden(),
              "tabs: ...on screen too — one of them is in front, neither is closed");
        // …and a REAL close still reads as closed, through the same predicate.
        relaunched.timeline->close();
        app.processEvents();
        CHECK(relaunched.timeline->isHidden(),
              "tabs: a tab closed from its X IS hidden — the predicate tells the two apart");
    }

    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
