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
// every editor dock (Hierarchy, Properties, Presets, Asset Browser, Timeline)
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
#include <QMainWindow>
#include <QSettings>
#include <QString>

namespace DockState {

/// The layout version. Bump when the DEFAULT layout changes in a way that
/// should override what users already have; Qt refuses to restore a blob whose
/// version does not match, which turns the bump into "everyone gets the new
/// default once".
constexpr int kVersion = 1;

/// Writes `window`'s dock layout under `key`. Cheap and unconditional — the
/// caller decides when (on close, for the editor).
inline void save(const QMainWindow *window, QSettings *settings, const QString &key)
{
    if (!window || !settings) return;
    settings->setValue(key, window->saveState(kVersion));
}

/// Restores the layout stored under `key`. Returns false — leaving `window`
/// exactly as it was — when there is nothing stored, or when Qt refuses the
/// blob (wrong version, unknown docks). "False" is the caller's signal to
/// apply the DEFAULT layout, so a first run and a rejected blob behave the
/// same way.
inline bool restore(QMainWindow *window, QSettings *settings, const QString &key)
{
    if (!window || !settings) return false;
    const QByteArray blob = settings->value(key).toByteArray();
    if (blob.isEmpty()) return false;
    return window->restoreState(blob, kVersion);
}

}   // namespace DockState

#endif // DOCKSTATE_H
