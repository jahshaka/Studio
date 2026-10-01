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
#include "shell/viewcontroller.h"
#include "shell/sceneissuewatch.h"
#include "shell/editordocks.h"

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
    mWindow->projectRunner()->open(playMode);
}

void ShellView::openProjectAsync(bool playMode)
{
    mWindow->projectRunner()->openAsync(playMode);
}

void ShellView::newProject(const QString &guid, const QString &name, const QString &folder, SceneTemplate kind)
{
    mWindow->projectRunner()->create(guid, name, folder, kind);
}

void ShellView::newProjectAsync(const QString &guid, const QString &name, const QString &folder, SceneTemplate kind)
{
    mWindow->projectRunner()->createAsync(guid, name, folder, kind);
}

void ShellView::closeProject(bool reopenInPlace)
{
    mWindow->projectRunner()->close(reopenInPlace);
}

bool ShellView::waitForOpen()
{
    return mWindow->projectRunner()->waitForOpen();
}

bool ShellView::isOpeningProject() const
{
    return mWindow->projectRunner()->isOpening();
}

unsigned ShellView::openSliceBoundaries() const
{
    return mWindow->projectRunner()->sliceBoundaries();
}

unsigned ShellView::openSliceBoundaryFrames() const
{
    return mWindow->projectRunner()->sliceBoundaryFrames();
}

bool ShellView::startProjectExport(const QString &guid, const QString &zipPath, QString *why)
{
    return mWindow->projectRunner()->startExport(guid, zipPath, why);
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
    return mWindow->editorDocks()->assetTray();
}

AssetMaterialPanel *ShellView::materialTray() const
{
    return mWindow->editorDocks()->materialTray();
}

SceneHierarchyWidget *ShellView::hierarchyPanel() const
{
    return mWindow->hierarchyPanel();
}

QString ShellView::trayTab() const
{
    return mWindow->editorDocks()->trayTab();
}

QStringList ShellView::trayTabs() const
{
    return mWindow->editorDocks()->trayTabs();
}

bool ShellView::setTrayTab(const QString &tab, bool focusConsoleInput)
{
    return mWindow->editorDocks()->setTrayTab(tab, focusConsoleInput);
}

bool ShellView::setTrayHeight(int height)
{
    return mWindow->editorDocks()->setTrayHeight(height);
}

bool ShellView::isTrayVisible() const
{
    return mWindow->editorDocks()->isTrayVisible();
}

bool ShellView::isConsoleTabVisible() const
{
    return mWindow->editorDocks()->isConsoleTabVisible();
}

void ShellView::setConsoleTabVisible(bool visible, bool focusInput)
{
    mWindow->editorDocks()->setConsoleTabVisible(visible, focusInput);
}

bool ShellView::isConsoleInputFocused() const
{
    return mWindow->editorDocks()->isConsoleInputFocused();
}

QDockWidget *ShellView::bottomFrontDock() const
{
    return mWindow->editorDocks()->bottomFrontDock();
}

int ShellView::bottomAreaTop() const
{
    return mWindow->editorDocks()->bottomAreaTop();
}

QDockWidget *ShellView::panelDock(const QString &name) const
{
    return mWindow->editorDocks()->panelDock(name);
}

bool ShellView::setPanelOpen(const QString &name, bool open)
{
    return mWindow->editorDocks()->setPanelOpen(name, open);
}

bool ShellView::isPanelOpen(const QString &name) const
{
    return mWindow->editorDocks()->isPanelOpen(name);
}

bool ShellView::raisePanel(const QString &name)
{
    return mWindow->editorDocks()->raisePanel(name);
}

bool ShellView::isPanelInFront(const QString &name) const
{
    return EditorDocks::isFrontTab(mWindow->editorDocks()->panelDock(name));
}

QVariantList ShellView::dockReport() const
{
    return mWindow->editorDocks()->dockReport();
}

IShellView::ColumnMetrics ShellView::activeColumns() const
{
    return mWindow->activeColumns();
}

QString ShellView::propertiesTab() const
{
    return mWindow->editorDocks()->propertiesTab();
}

bool ShellView::setPropertiesTab(const QString &name)
{
    return mWindow->editorDocks()->setPropertiesTab(name);
}

bool ShellView::isPropertiesTab(const QString &tabName) const
{
    return mWindow->editorDocks()->isPropertiesTab(tabName);
}

QString ShellView::propertiesFilter(const QString &tabName) const
{
    return mWindow->editorDocks()->propertiesFilter(tabName);
}

bool ShellView::setPropertiesFilter(const QString &tabName, const QString &text)
{
    return mWindow->editorDocks()->setPropertiesFilter(tabName, text);
}

QPair<int, int> ShellView::propertiesFilterCounts(const QString &tabName) const
{
    return mWindow->editorDocks()->propertiesFilterCounts(tabName);
}

QVariantList ShellView::propertyRows(const QString &tabName) const
{
    return mWindow->editorDocks()->propertyRows(tabName);
}

QVariantMap ShellView::propertyRow(const QString &tabName, const QString &key, bool drive, const QVariant &value, QString *error)
{
    return mWindow->editorDocks()->propertyRow(tabName, key, drive, value, error);
}

QVariantMap ShellView::propertiesStats() const
{
    return mWindow->editorDocks()->propertiesStats();
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
    return mWindow->views()->applyCameraView(name);
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
    return mWindow->views()->isImmersiveFullscreen();
}

void ShellView::setImmersiveFullscreen(bool on)
{
    mWindow->views()->setImmersiveFullscreen(on);
}

void ShellView::refreshGameplayShortcutRows()
{
    mWindow->refreshGameplayShortcutRows();
}

void ShellView::updateSceneIssues()
{
    mWindow->sceneIssues()->update();
}

QVariantMap ShellView::sceneIssueBarState() const
{
    return mWindow->sceneIssues()->state();
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

void ShellView::showNotice(const QString &title, const QString &text)
{
    mWindow->showNotice(title, text);
}

void ShellView::showPlayerVrState(bool available, bool active)
{
    mWindow->showPlayerVrState(available, active);
}
