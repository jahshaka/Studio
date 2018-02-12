#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QListWidgetItem>
#include <QFutureWatcher>
#include <QProgressDialog>

#include "../dialogs/progressdialog.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

namespace Ui {
    class ProjectManager;
}

class aiScene;
class GridWidget;
class DynamicGrid;
class ItemGridWidget;
class Database;

struct ModelData {
    ModelData() = default;
    ModelData(QString p, const aiScene *ai) : path(p), data(ai) {}
    QString path;
    const aiScene *data;
};

class SettingsManager;

#include <QListWidget>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>

class MainWindow;

class ProjectManager : public QWidget
{
    Q_OBJECT

public:
    ProjectManager(Database *handle, QWidget *parent = nullptr);
    ~ProjectManager();

	void updateTile(const QString &id, const QByteArray &arr);
    void populateDesktop(bool reset = false);
    bool checkForEmptyState();
    void cleanupOnClose();
//    static QVector<ModelData> loadModel(const QString&);
    QVector<ModelData> loadModel(const QString &filePath)
    {
        QVector<ModelData> sceneVec;
        QFile file(filePath);
        file.open(QFile::ReadOnly);
        auto data = file.readAll();

        auto importer = new Assimp::Importer;
        //    const aiScene *scene = importer->ReadFile(filePath.toStdString().c_str(),
        //                                             aiProcessPreset_TargetRealtime_Fast);

        const aiScene *scene = importer->ReadFileFromMemory((void*) data.data(),
                                                            data.length(),
                                                            aiProcessPreset_TargetRealtime_Fast);
        ModelData d = { filePath, scene };
        sceneVec.append(d);
        return sceneVec;
    }


protected slots:
    void openSampleProject(QListWidgetItem*);
    void newProject();
    void importProjectFromFile(const QString& file = QString());

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
    void fileToCreate(const QString& str, const QString& str2);
    void importProject();
    void exportProject();
    void closeProject();

private:
    void loadProjectAssets();
    void walkProjectFolder(const QString&);

    Ui::ProjectManager *ui;
    SettingsManager* settings;

    QTimer *searchTimer;
    QString searchTerm;

    Database *db;
    QFutureWatcher<QVector<ModelData>> *futureWatcher;

    QSharedPointer<ProgressDialog> progressDialog;
    bool isNewProject;
    bool isMainWindowActive;

    DynamicGrid *dynamicGrid;
    QDialog sampleDialog;
};

struct AssetWidgetConcurrentWrapper {
    ProjectManager *instance;
    typedef QVector<ModelData> result_type;
    AssetWidgetConcurrentWrapper(ProjectManager *inst) : instance(inst) {}
        result_type operator()(const QString &data) {
        return instance->loadModel(data);
    }
};

#endif // PROJECTMANAGER_H
