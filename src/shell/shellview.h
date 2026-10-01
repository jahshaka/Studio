/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHELLVIEW_H
#define SHELLVIEW_H

// ShellView — the shell's implementation of IShellView (ui/ishellview.h): a
// thin adapter that answers each call from the shell part that owns it, so
// the window class does not grow a forwarding method per verb and nothing
// under scripting/ or modules/ has to name the shell.

#include "ui/ishellview.h"

class MainWindow;

class ShellView : public IShellView
{
public:
    explicit ShellView(MainWindow *window) : mWindow(window) {}

    QWidget * window() const override;
    QString space() const override;
    bool setSpace(const QString &space) override;
    QString spaceRefusal() const override;
    void openProject(bool playMode) override;
    void openProjectAsync(bool playMode) override;
    void newProject(const QString &guid, const QString &name, const QString &folder, SceneTemplate kind) override;
    void newProjectAsync(const QString &guid, const QString &name, const QString &folder, SceneTemplate kind) override;
    void closeProject(bool reopenInPlace = false) override;
    bool waitForOpen() override;
    bool isOpeningProject() const override;
    unsigned openSliceBoundaries() const override;
    unsigned openSliceBoundaryFrames() const override;
    bool startProjectExport(const QString &guid, const QString &zipPath, QString *why) override;
    bool startInteractiveImport(const QStringList &files) override;
    ProjectManager * projectPage() const override;
    AssetView * assetsPage() override;
    AssetWidget * assetTray() const override;
    AssetMaterialPanel * materialTray() const override;
    SceneHierarchyWidget * hierarchyPanel() const override;
    QString trayTab() const override;
    QStringList trayTabs() const override;
    bool setTrayTab(const QString &tab, bool focusConsoleInput) override;
    bool setTrayHeight(int height) override;
    bool isTrayVisible() const override;
    bool isConsoleTabVisible() const override;
    void setConsoleTabVisible(bool visible, bool focusInput) override;
    bool isConsoleInputFocused() const override;
    QDockWidget * bottomFrontDock() const override;
    int bottomAreaTop() const override;
    QDockWidget * panelDock(const QString &name) const override;
    bool setPanelOpen(const QString &name, bool open) override;
    bool isPanelOpen(const QString &name) const override;
    bool raisePanel(const QString &name) override;
    bool isPanelInFront(const QString &name) const override;
    QVariantList dockReport() const override;
    ColumnMetrics activeColumns() const override;
    QString propertiesTab() const override;
    bool setPropertiesTab(const QString &name) override;
    bool isPropertiesTab(const QString &tabName) const override;
    QString propertiesFilter(const QString &tabName) const override;
    bool setPropertiesFilter(const QString &tabName, const QString &text) override;
    QPair<int, int> propertiesFilterCounts(const QString &tabName) const override;
    QVariantList propertyRows(const QString &tabName) const override;
    QVariantMap propertyRow(const QString &tabName, const QString &key, bool drive, const QVariant &value, QString *error) override;
    QVariantMap propertiesStats() const override;
    QVariantList toolbarActions() const override;
    QVariantMap viewOptionChecks() const override;
    void setPhysicsDebugOverlay(bool on) override;
    bool applyCameraView(const QString &name) override;
    bool applyGizmoTransformSpace(const QString &space) override;
    bool applyGizmoMode(const QString &mode) override;
    void useFreeCamera() override;
    void useArcballCamera() override;
    bool isImmersiveFullscreen() const override;
    void setImmersiveFullscreen(bool on) override;
    void refreshGameplayShortcutRows() override;
    void updateSceneIssues() override;
    QVariantMap sceneIssueBarState() const override;
    QStringList dialogNames() const override;
    QWidget * openDialog(const QString &name, const QVariantMap &options, QVariantMap *extra) override;
    bool closeDialog(const QString &name) override;
    bool isDialogOpen(const QString &name) const override;
    void showViewportToast(const QString &title, const QString &text) override;
    void showNotice(const QString &title, const QString &text) override;
    void showPlayerVrState(bool available, bool active) override;

private:
    MainWindow *mWindow = nullptr;
};

#endif // SHELLVIEW_H
