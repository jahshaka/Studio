/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

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
