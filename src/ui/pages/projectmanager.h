/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QDialog>
#include <QFutureWatcher>
#include <QListWidgetItem>
#include <QPointer>
#include <QWidget>

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

class aiScene;

class Database;
class DynamicGrid;
class GridWidget;
class ItemGridWidget;
class ProgressDialog;
class QMenu;
class QAction;

namespace Ui {
	class ProjectManager;
}

struct ModelData {
	ModelData() = default;
    ModelData(QString p, QString g, const aiScene *s) : path(p), guid(g), data(s) {}
	QString			path;
	QString			guid;
    const aiScene  *data;
};

class SettingsManager;
class MainWindow;
class Project;

using AssetList = QPair<QString, QString>;

class ProjectManager : public QWidget
{
    Q_OBJECT

public:
    /// `project` is the one live Project instance, owned by the shell
    /// (Phase 4: was the Globals::project static). A constructor parameter
    /// because populateDesktop() runs during construction and reaches it
    /// through isOpenProjectTile().
    ProjectManager(Database *handle, Project *project, QWidget *parent = nullptr);
    ~ProjectManager();

	void updateTile(const QString &id, const QByteArray &arr);
	void addImportedTileToDesktop(const QString &guid);
    void populateDesktop(bool reset = false);
    bool checkForEmptyState();
    void cleanupOnClose();

	ModelData loadAiSceneFromModel(const QPair<QString, QString> asset);
	MainWindow *mainWindow = nullptr;
	/// Is this tile the project whose scene is open right now? (highlight rule)
	bool isOpenProjectTile(const QString &guid) const;

    int getCurrentDesktop() const { return currentDesktop; }

    // Desktop view mode + slider tiles, public for the scripting API
    // (desktop.viewMode / desktop.setViewMode / desktop.moveTile / desktop.tiles).
    // Mode names: "rows" | "freeform" | "sliders" (persisted per desktop).
    QString desktopViewMode() const { return currentLayoutMode; }
    bool setDesktopViewMode(const QString &name);
    bool moveTileToSliderPos(const QString &guid, int row, int index);   // 0-based row
    QVariantList sliderTilesForApi() const;

    /// Synchronous, dialog-free version of loadProjectAssets() for the scripting
    /// API (project.open): same DB sweeps and AssetManager registrations as the
    /// concurrent path, sequentially on the caller's thread, no modal progress
    /// dialog, no fileToOpen signal — the caller decides what happens next.
    void loadProjectAssetsSync();

public slots:
    // public for the scripting API (app.desktop(n))
    void switchDesktop(int desktop);

protected slots:
    void openSampleProject(QListWidgetItem*);
    void newProject();
    void importProjectFromFile(const QString& file = QString(), bool shouldOpen = false);

    void changePreviewSize(QString);

    void finalizeProjectAssetLoad();
    void finishedFutureWatcher();

    void openSampleBrowser();

    void openProjectFromWidget(ItemGridWidget*, bool playMode);
    void exportProjectFromWidget(ItemGridWidget*);
    void renameProjectFromWidget(ItemGridWidget*);
    void closeProjectFromWidget(ItemGridWidget*);
    void deleteProjectFromWidget(ItemGridWidget*);

    // desktops (DESKTOPS_SPEC.md)
    void moveProjectToDesktop(ItemGridWidget*, int desktop);
    void projectTilePositionChanged(ItemGridWidget*);
    void projectTileSliderChanged(ItemGridWidget*);     // sliders: persist {row, index}

    void searchProjects();

private:
    friend DynamicGrid;     // is this going to be a problem?
    bool openInPlayMode;

signals:
    void fileToOpen(bool playMode);
    void fileToCreate(const QString &name, const QString &path);
    void importProject();
    void exportProject();
    void closeProject();

private:
    void loadProjectAssets();

    // desktops (DESKTOPS_SPEC.md + DESKTOP_SLIDER_SPEC.md)
    void setupDesktopControls();
    void applyDesktopLayoutMode(const QString &modeName, bool persist);
    static QString desktopLayoutKey(int desktop);
    static QString normalizedLayoutMode(const QString &name);   // unknown -> "rows"

    int currentDesktop = 1;
    QString currentLayoutMode = QStringLiteral("rows");
    QMenu *desktopMenu = nullptr;
    QMenu *layoutMenu = nullptr;
    QMenu *tileSizeMenu = nullptr;
    QVector<QAction*> desktopActions;
    QAction *rowsAction = nullptr;
    QAction *freeformAction = nullptr;
    QAction *slidersAction = nullptr;

    Ui::ProjectManager *ui;
    SettingsManager* settings;

    QTimer *searchTimer;
    QString searchTerm;

    Database *db;
    Project *project;

    QPointer<QFutureWatcher<QVector<ModelData>>> futureWatcher;
	QPointer<ProgressDialog> progressDialog;

    bool isNewProject;
    bool isMainWindowActive;

    DynamicGrid *dynamicGrid;
    QDialog sampleDialog;

    QMap<QString, QString> assetGuids;
};

struct AssetWidgetConcurrentWrapper {
    ProjectManager *instance;
    typedef ModelData result_type;
    AssetWidgetConcurrentWrapper(ProjectManager *inst) : instance(inst) {}
        result_type operator()(const QPair<QString, QString> &value) {
        return instance->loadAiSceneFromModel(value);
    }
};

#endif // PROJECTMANAGER_H
