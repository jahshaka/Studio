/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ISHELLVIEW_H
#define ISHELLVIEW_H

// IShellView — WHAT THE SHELL SHOWS, AS AN INTERFACE (D10-SHELL-MODULES; audit S3).
//
// The verbs and the modules need the window: which space is up, a toast over
// the viewport, the editor's panels and tray, the dialogs by name, the open and
// the create. They used to get it by including shell/mainwindow.h — the API
// layer compiled against the whole shell (13 ApiModule TUs, 69 reaches from
// editorapi alone). This is the surface they actually use, in the shape
// IEditorViewport gives the viewport: the shell implements it
// (shell/shellview.cpp, answering each call from the part that owns it),
// headless hosts leave it null, and nothing under scripting/ or modules/ names
// the shell (tests/hygiene guards it).
//
// Spaces are named as app.space() names them: "desktop", "player", "editor",
// "materials", "assets", "publish", "avatar".

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "services/scenetemplate.h"

class AssetMaterialPanel;
class AssetView;
class AssetWidget;
class ProjectManager;
class QDockWidget;
class QWidget;
class SceneHierarchyWidget;

class IShellView
{
public:
    virtual ~IShellView() = default;

    /// The top-level window (dialog parent, findChild root, app.quit's close).
    virtual QWidget *window() const = 0;

    // ---- spaces --------------------------------------------------------
    /// The space on screen.
    virtual QString space() const = 0;
    /// Switches to `space` through the product's own switch (the buttons'
    /// path). False for an unknown name; a refused switch leaves the space
    /// where it was and spaceRefusal() says why.
    virtual bool setSpace(const QString &space) = 0;
    /// Why the last switch did not happen, in the user's words ("" unless the
    /// most recent attempt was refused).
    virtual QString spaceRefusal() const = 0;

    // ---- the project: open, create, close, export -----------------------
    /// The blocking open of the CURRENT project (it returns with the world open).
    virtual void openProject(bool playMode) = 0;
    /// The threaded open; poll isOpeningProject().
    virtual void openProjectAsync(bool playMode) = 0;
    virtual void newProject(const QString &guid, const QString &name, const QString &folder,
                            SceneTemplate kind) = 0;
    virtual void newProjectAsync(const QString &guid, const QString &name, const QString &folder,
                                 SceneTemplate kind) = 0;
    /// `reopenInPlace` = the first half of an open: the teardown without
    /// leaving the page the open will reveal into.
    virtual void closeProject(bool reopenInPlace = false) = 0;
    /// Drains an open in flight; true when nothing is (or is still) in flight.
    virtual bool waitForOpen() = 0;
    virtual bool isOpeningProject() const = 0;
    virtual unsigned openSliceBoundaries() const = 0;
    virtual unsigned openSliceBoundaryFrames() const = 0;
    /// The threaded export of project `guid` (the tile's Export, minus the dialog).
    virtual bool startProjectExport(const QString &guid, const QString &zipPath, QString *why) = 0;
    /// The interactive threaded import (the tray's batch + its progress UI).
    virtual bool startInteractiveImport(const QStringList &files) = 0;
    /// The Desktop page and the Assets page (built on first use), or null.
    virtual ProjectManager *projectPage() const = 0;
    virtual AssetView *assetsPage() = 0;

    // ---- the editor's panels -------------------------------------------
    virtual AssetWidget *assetTray() const = 0;
    virtual AssetMaterialPanel *materialTray() const = 0;
    virtual SceneHierarchyWidget *hierarchyPanel() const = 0;
    /// The bottom area: "assets" | "timeline" | "console".
    virtual QString trayTab() const = 0;
    virtual QStringList trayTabs() const = 0;
    virtual bool setTrayTab(const QString &tab, bool focusConsoleInput = true) = 0;
    virtual bool setTrayHeight(int height) = 0;
    virtual bool isTrayVisible() const = 0;
    virtual bool isConsoleTabVisible() const = 0;
    virtual void setConsoleTabVisible(bool visible, bool focusInput = true) = 0;
    virtual bool isConsoleInputFocused() const = 0;
    virtual QDockWidget *bottomFrontDock() const = 0;
    virtual int bottomAreaTop() const = 0;
    /// An editor panel by script name ("hierarchy", "properties", "presets",
    /// "assets", "timeline", "console").
    virtual QDockWidget *panelDock(const QString &name) const = 0;
    virtual bool setPanelOpen(const QString &name, bool open) = 0;
    virtual bool isPanelOpen(const QString &name) const = 0;
    virtual bool raisePanel(const QString &name) = 0;
    /// Whether that panel is the tab in FRONT of its group (and not closed).
    virtual bool isPanelInFront(const QString &name) const = 0;
    /// The editor's docks, measured (app.docks).
    virtual QVariantList dockReport() const = 0;
    /// The active page's columns (app.columns): `valid` false for a space
    /// with no columns.
    struct ColumnMetrics {
        bool valid = false;
        int leftWidth = 0;
        int leftMin = 0;
        int rightWidth = 0;
        int rightMin = 0;
    };
    virtual ColumnMetrics activeColumns() const = 0;

    // ---- the properties column ------------------------------------------
    virtual QString propertiesTab() const = 0;
    virtual bool setPropertiesTab(const QString &name) = 0;
    virtual bool isPropertiesTab(const QString &tabName) const = 0;
    virtual QString propertiesFilter(const QString &tabName) const = 0;
    virtual bool setPropertiesFilter(const QString &tabName, const QString &text) = 0;
    virtual QPair<int, int> propertiesFilterCounts(const QString &tabName) const = 0;
    virtual QVariantList propertyRows(const QString &tabName) const = 0;
    virtual QVariantMap propertyRow(const QString &tabName, const QString &key, bool drive,
                                    const QVariant &value, QString *error) = 0;
    virtual QVariantMap propertiesStats() const = 0;

    // ---- the editor's view controls --------------------------------------
    virtual QVariantList toolbarActions() const = 0;
    virtual QVariantMap viewOptionChecks() const = 0;
    virtual void setPhysicsDebugOverlay(bool on) = 0;
    /// A canonical camera view ("top", ..., "perspective"); false when unknown.
    virtual bool applyCameraView(const QString &name) = 0;
    /// "local" | "global"; false for anything else.
    virtual bool applyGizmoTransformSpace(const QString &space) = 0;
    /// "translate" | "rotate" | "scale", the toolbar following; false when unknown.
    virtual bool applyGizmoMode(const QString &mode) = 0;
    virtual void useFreeCamera() = 0;
    virtual void useArcballCamera() = 0;
    virtual bool isImmersiveFullscreen() const = 0;
    virtual void setImmersiveFullscreen(bool on) = 0;
    /// Re-labels the Gameplay rows of the shortcut table from the InputMap.
    virtual void refreshGameplayShortcutRows() = 0;

    // ---- the scene-issue bar ---------------------------------------------
    virtual void updateSceneIssues() = 0;
    virtual QVariantMap sceneIssueBarState() const = 0;

    // ---- dialogs by name ---------------------------------------------------
    virtual QStringList dialogNames() const = 0;
    virtual QWidget *openDialog(const QString &name, const QVariantMap &options,
                                QVariantMap *extra) = 0;
    virtual bool closeDialog(const QString &name) = 0;
    virtual bool isDialogOpen(const QString &name) const = 0;

    // ---- feedback ------------------------------------------------------
    /// The one transient toast over the editor viewport.
    virtual void showViewportToast(const QString &title, const QString &text) = 0;
    /// The window-centre notice (a page that cannot start, VR that did not).
    virtual void showNotice(const QString &title, const QString &text) = 0;
    /// The Player page's VR button follows the session (VrModule owns it).
    virtual void showPlayerVrState(bool available, bool active) = 0;
};

#endif // ISHELLVIEW_H
