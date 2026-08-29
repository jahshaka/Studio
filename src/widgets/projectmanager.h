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

using AssetList = QPair<QString, QString>;

class ProjectManager : public QWidget
{
    Q_OBJECT

public:
    ProjectManager(Database *handle, QWidget *parent = nullptr);
    ~ProjectManager();

	void updateTile(const QString &id, const QByteArray &arr);
	void addImportedTileToDesktop(const QString &guid);
    void populateDesktop(bool reset = false);
    bool checkForEmptyState();
    void cleanupOnClose();

	ModelData loadAiSceneFromModel(const QPair<QString, QString> asset);
	MainWindow *mainWindow;

    int getCurrentDesktop() const { return currentDesktop; }

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
    void switchDesktop(int desktop);
    void projectTilePositionChanged(ItemGridWidget*);

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

    // desktops (DESKTOPS_SPEC.md)
    void setupDesktopControls();
    void applyDesktopLayoutMode(bool freeform, bool persist);
    static QString desktopLayoutKey(int desktop);

    int currentDesktop = 1;
    QMenu *desktopMenu = nullptr;
    QMenu *layoutMenu = nullptr;
    QVector<QAction*> desktopActions;
    QAction *rowsAction = nullptr;
    QAction *freeformAction = nullptr;

    Ui::ProjectManager *ui;
    SettingsManager* settings;

    QTimer *searchTimer;
    QString searchTerm;

    Database *db;

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
