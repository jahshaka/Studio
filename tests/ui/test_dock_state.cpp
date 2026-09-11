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
// editor's docks (Hierarchy, Properties, Presets, Asset Browser, Timeline) live
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
//     already have a saved one.

#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QSettings>
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
/// on the left, Properties on the right with Presets split under it, the Asset
/// Browser and the Timeline tabbed along the bottom.
struct Shell {
    QMainWindow window;
    QDockWidget *hierarchy;
    QDockWidget *properties;
    QDockWidget *presets;
    QDockWidget *assets;
    QDockWidget *timeline;

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
        assets     = makeDock("assetDock", "Asset Browser");
        timeline   = makeDock("animationDock", "Timeline");
        window.addDockWidget(Qt::LeftDockWidgetArea, hierarchy);
        window.addDockWidget(Qt::RightDockWidgetArea, properties);
        window.splitDockWidget(properties, presets, Qt::Vertical);
        window.addDockWidget(Qt::BottomDockWidgetArea, assets);
        window.addDockWidget(Qt::BottomDockWidgetArea, timeline);
        window.tabifyDockWidget(timeline, assets);
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
                  shell.window.dockWidgetArea(shell.timeline) == Qt::BottomDockWidgetArea,
              "default: the Asset Browser and the Timeline share the bottom area");
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

    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
