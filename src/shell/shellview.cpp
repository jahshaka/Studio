/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/shellview.h"

#include "shell/mainwindow.h"
#include "shell/spaces.h"

QWidget *ShellView::window() const
{
    return mWindow;
}

QString ShellView::space() const
{
    return spaces::id(mWindow->getWindowSpace());
}

bool ShellView::setSpace(const QString &space)
{
    WindowSpaces target;
    if (!spaces::fromId(space, &target)) return false;
    mWindow->switchSpace(target);
    return true;
}

QString ShellView::spaceRefusal() const
{
    return mWindow->lastSpaceRefusal();
}

void ShellView::openProject(bool playMode)
{
    mWindow->openProject(playMode);
}

void ShellView::openProjectAsync(bool playMode)
{
    mWindow->openProjectAsync(playMode);
}

void ShellView::newProject(const QString &guid, const QString &name, const QString &folder, SceneTemplate kind)
{
    mWindow->newProject(guid, name, folder, kind);
}

void ShellView::newProjectAsync(const QString &guid, const QString &name, const QString &folder, SceneTemplate kind)
{
    mWindow->newProjectAsync(guid, name, folder, kind);
}

void ShellView::closeProject(bool reopenInPlace)
{
    mWindow->closeProject(reopenInPlace ? MainWindow::CloseIntent::ReopenInPlace
                                         : MainWindow::CloseIntent::ToDesktop);
}

bool ShellView::waitForOpen()
{
    return mWindow->waitForOpen();
}

bool ShellView::isOpeningProject() const
{
    return mWindow->isOpeningProject();
}

unsigned ShellView::openSliceBoundaries() const
{
    return mWindow->openSliceBoundaries();
}

unsigned ShellView::openSliceBoundaryFrames() const
{
    return mWindow->openSliceBoundaryFrames();
}

bool ShellView::startProjectExport(const QString &guid, const QString &zipPath, QString *why)
{
    return mWindow->startProjectExport(guid, zipPath, why);
}

bool ShellView::startInteractiveImport(const QStringList &files)
{
    return mWindow->startInteractiveImport(files);
}

ProjectManager *ShellView::projectPage() const
{
    return mWindow->projectPage();
}

AssetView *ShellView::assetsPage()
{
    return mWindow->assetsPage();
}

AssetWidget *ShellView::assetTray() const
{
    return mWindow->assetTray();
}

AssetMaterialPanel *ShellView::materialTray() const
{
    return mWindow->materialTray();
}

SceneHierarchyWidget *ShellView::hierarchyPanel() const
{
    return mWindow->hierarchyPanel();
}

QString ShellView::trayTab() const
{
    return mWindow->trayTab();
}

QStringList ShellView::trayTabs() const
{
    return mWindow->trayTabs();
}

bool ShellView::setTrayTab(const QString &tab, bool focusConsoleInput)
{
    return mWindow->setTrayTab(tab, focusConsoleInput);
}

bool ShellView::setTrayHeight(int height)
{
    return mWindow->setTrayHeight(height);
}

bool ShellView::isTrayVisible() const
{
    return mWindow->isTrayVisible();
}

bool ShellView::isConsoleTabVisible() const
{
    return mWindow->isConsoleTabVisible();
}

void ShellView::setConsoleTabVisible(bool visible, bool focusInput)
{
    mWindow->setConsoleTabVisible(visible, focusInput);
}

bool ShellView::isConsoleInputFocused() const
{
    return mWindow->isConsoleInputFocused();
}

QDockWidget *ShellView::bottomFrontDock() const
{
    return mWindow->bottomFrontDock();
}

int ShellView::bottomAreaTop() const
{
    return mWindow->bottomAreaTop();
}

QDockWidget *ShellView::panelDock(const QString &name) const
{
    return mWindow->panelDock(name);
}

bool ShellView::setPanelOpen(const QString &name, bool open)
{
    return mWindow->setPanelOpen(name, open);
}

bool ShellView::isPanelOpen(const QString &name) const
{
    return mWindow->isPanelOpen(name);
}

bool ShellView::raisePanel(const QString &name)
{
    return mWindow->raisePanel(name);
}

bool ShellView::isPanelInFront(const QString &name) const
{
    return MainWindow::isFrontTab(mWindow->panelDock(name));
}

QVariantList ShellView::dockReport() const
{
    return mWindow->dockReport();
}

IShellView::ColumnMetrics ShellView::activeColumns() const
{
    return mWindow->activeColumns();
}

QString ShellView::propertiesTab() const
{
    return mWindow->propertiesTab();
}

bool ShellView::setPropertiesTab(const QString &name)
{
    return mWindow->setPropertiesTab(name);
}

bool ShellView::isPropertiesTab(const QString &tabName) const
{
    return mWindow->isPropertiesTab(tabName);
}

QString ShellView::propertiesFilter(const QString &tabName) const
{
    return mWindow->propertiesFilter(tabName);
}

bool ShellView::setPropertiesFilter(const QString &tabName, const QString &text)
{
    return mWindow->setPropertiesFilter(tabName, text);
}

QPair<int, int> ShellView::propertiesFilterCounts(const QString &tabName) const
{
    return mWindow->propertiesFilterCounts(tabName);
}

QVariantList ShellView::propertyRows(const QString &tabName) const
{
    return mWindow->propertyRows(tabName);
}

QVariantMap ShellView::propertyRow(const QString &tabName, const QString &key, bool drive, const QVariant &value, QString *error)
{
    return mWindow->propertyRow(tabName, key, drive, value, error);
}

QVariantMap ShellView::propertiesStats() const
{
    return mWindow->propertiesStats();
}

QVariantList ShellView::toolbarActions() const
{
    return mWindow->toolbarActions();
}

QVariantMap ShellView::viewOptionChecks() const
{
    return mWindow->viewOptionChecks();
}

void ShellView::setPhysicsDebugOverlay(bool on)
{
    mWindow->setPhysicsDebugOverlay(on);
}

bool ShellView::applyCameraView(const QString &name)
{
    return mWindow->applyCameraView(name);
}

bool ShellView::applyGizmoTransformSpace(const QString &space)
{
    return mWindow->applyGizmoTransformSpace(space);
}

bool ShellView::applyGizmoMode(const QString &mode)
{
    if (mode == QLatin1String("rotate"))         mWindow->rotateGizmo();
    else if (mode == QLatin1String("scale"))     mWindow->scaleGizmo();
    else if (mode == QLatin1String("translate")) mWindow->translateGizmo();
    else return false;
    return true;
}

void ShellView::useFreeCamera()
{
    mWindow->useFreeCamera();
}

void ShellView::useArcballCamera()
{
    mWindow->useArcballCam();
}

bool ShellView::isImmersiveFullscreen() const
{
    return mWindow->isImmersiveFullscreen();
}

void ShellView::setImmersiveFullscreen(bool on)
{
    mWindow->setImmersiveFullscreen(on);
}

void ShellView::refreshGameplayShortcutRows()
{
    mWindow->refreshGameplayShortcutRows();
}

void ShellView::updateSceneIssues()
{
    mWindow->updateSceneIssues();
}

QVariantMap ShellView::sceneIssueBarState() const
{
    return mWindow->sceneIssueBarState();
}

QStringList ShellView::dialogNames() const
{
    return mWindow->dialogNames();
}

QWidget *ShellView::openDialog(const QString &name, const QVariantMap &options, QVariantMap *extra)
{
    return mWindow->openDialog(name, options, extra);
}

bool ShellView::closeDialog(const QString &name)
{
    return mWindow->closeDialog(name);
}

bool ShellView::isDialogOpen(const QString &name) const
{
    return mWindow->isDialogOpen(name);
}

void ShellView::showViewportToast(const QString &title, const QString &text)
{
    mWindow->showViewportToast(title, text);
}
