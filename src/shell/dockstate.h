/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef DOCKSTATE_H
#define DOCKSTATE_H

// THE EDITOR'S DOCK LAYOUT IS A NESTED QMainWindow, and it was never saved.
//
// MainWindow::closeEvent writes `geometry` and `windowState` and the
// constructor restores them — but `windowState` is the OUTER window's, and
// every editor dock (Hierarchy, Properties, Presets, Tray, Timeline)
// belongs to the INNER `viewPort` QMainWindow that hosts them. Its saveState()
// was never called at all, so every dock the user moved, resized, floated,
// tabbed or closed came back to the compiled-in layout on the next launch.
//
// The helper is a free function pair rather than two lines inside MainWindow
// for one reason: it is the only part of the layout story that can be driven
// with no display, no engine and no project, so ui.dock_state can assert the
// round trip (blob out, layout changed, blob back in, layout restored) instead
// of the tree taking a QSettings write on trust.
//
// KEYS. `<key>` holds the blob; the docks are matched by objectName, which is
// why every dock in mainwindow.cpp sets one. restoreState refuses a blob it
// does not recognise (a version bump, a dock that no longer exists) and says
// so by returning false — the caller then applies the default layout, which is
// exactly what a first run does.

#include <QByteArray>
#include <QDockWidget>
#include <QList>
#include <QMainWindow>
#include <QSettings>
#include <QString>

namespace DockState {

/// The layout version. Bump when the DEFAULT layout changes in a way that
/// should override what users already have; Qt refuses to restore a blob whose
/// version does not match, which turns the bump into "everyone gets the new
/// default once".
///
/// 2 (smoke S1, 2026-09-11): the script console stopped being a dock of the
/// bottom area and became a TAB of the asset tray, so `scriptConsoleDock` no
/// longer exists. A version-1 blob names it, and a blob that names a dock the
/// window does not have leaves Qt guessing at the bottom area — the bump makes
/// every existing layout fall back to the new default exactly once.
///
/// 3 (lane SPACE-1, 2026-09-14): the Hierarchy dock's objectName was
/// `sceneHierarchyWidget` — a leftover second setObjectName() that overwrote
/// `sceneHierarchyDock` one line after it was set, so every saved layout named
/// the left column after the widget inside it. The name is the dock's now, and
/// the bump is what stops a version-2 blob from restoring a left column Qt can
/// no longer match. It also retires every layout written by the builds that
/// saved the PLAYER's hidden docks as the editor's (the defect this lane
/// fixed), which is worth one free fall-back to the default.
///
/// 4 (lane SPACE-2, 2026-09-14): the bottom area changed shape — the script
/// console is a DOCK again (`scriptConsoleDock`, the third tab beside Assets
/// and the Timeline), the tray's nested QTabWidget is gone, and the group's tab
/// bar moved to the top of the area. A version-3 blob knows nothing about the
/// console dock, and a blob that leaves a dock unplaced leaves Qt guessing at
/// the whole area; the bump hands every existing layout the new default once.
constexpr int kVersion = 4;

/// The editor toolbar's objectName. It lives in the same nested window as the
/// docks, and saveState()/restoreState() match TOOLBARS by objectName exactly
/// as they match docks: an unnamed toolbar makes Qt warn on every snapshot and
/// its position and visibility never come back (it was "Tool Bar", unnamed,
/// until SMALL-FIXES-1). One constant so the shell and ui.dock_state agree.
inline constexpr char kEditorToolBarName[] = "MainToolBar";

/// `window`'s dock layout, at this layout version. The one place saveState's
/// version argument is supplied, so a snapshot taken to be kept in memory (the
/// editor's last layout, held across a space switch) cannot drift from the one
/// written to settings.
inline QByteArray snapshot(const QMainWindow *window)
{
    return window ? window->saveState(kVersion) : QByteArray();
}

/// Writes a layout blob under `key`. An empty blob writes nothing: a session
/// with nothing to say about the layout must leave the stored one alone.
inline void store(QSettings *settings, const QString &key, const QByteArray &blob)
{
    if (!settings || blob.isEmpty()) return;
    settings->setValue(key, blob);
}

/// Writes `window`'s dock layout under `key`. Cheap and unconditional — the
/// caller decides when (on close, for the editor).
inline void save(const QMainWindow *window, QSettings *settings, const QString &key)
{
    store(settings, key, snapshot(window));
}

/// Is any dock of `window` asking to be on screen? Deliberately isHidden() and
/// not isVisible(): these docks live on a page of a stacked widget, so while
/// another space is showing, every one of them is invisible without any of them
/// being closed. isHidden() is the dock's OWN state — what the editor page will
/// show when it comes back — which is the only thing a saved layout can carry.
inline bool hasVisibleDock(const QMainWindow *window)
{
    if (!window) return false;
    const QList<QDockWidget *> docks = window->findChildren<QDockWidget *>();
    for (const QDockWidget *d : docks)
        if (!d->isHidden()) return true;
    return docks.isEmpty();   // no docks at all: nothing to lose, nothing to refuse
}

/// Restores the layout stored under `key`. Returns false — leaving `window`
/// exactly as it was — when there is nothing stored, when Qt refuses the blob
/// (wrong version, unknown docks), or when the blob describes a window with
/// NO PANELS AT ALL. "False" is the caller's signal to apply the DEFAULT
/// layout, so a first run, a rejected blob and an empty one behave the same way.
///
/// THE EMPTY-LAYOUT RULE (lane SPACE-1, 2026-09-14, owner report). The editor's
/// docks are hidden whenever another space is showing, and the exit path saved
/// whatever the docks were doing at that moment — so quitting from the Player
/// (or from immersive fullscreen) stored a layout in which every panel is
/// closed. Restoring it produced an editor with nothing but the 3D view and no
/// way to ask for the panels back except by switching space. A layout that
/// records no panels is not a layout: it is refused here, the window is put
/// back exactly as it was, and the caller applies the default.
inline bool restore(QMainWindow *window, QSettings *settings, const QString &key)
{
    if (!window || !settings) return false;
    const QByteArray blob = settings->value(key).toByteArray();
    if (blob.isEmpty()) return false;
    // The window as it stands — the default layout on the first restore, the
    // user's on the second (applyColumnWidthsOnce re-applies the blob once the
    // window has its real size). Either way it is the layout we keep if the
    // stored one turns out to be empty.
    const QByteArray lastGood = window->saveState(kVersion);
    if (!window->restoreState(blob, kVersion)) return false;
    if (hasVisibleDock(window)) return true;
    window->restoreState(lastGood, kVersion);
    return false;
}

}   // namespace DockState

#endif // DOCKSTATE_H
