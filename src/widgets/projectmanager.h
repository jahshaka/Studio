#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QListWidgetItem>
#include <QFutureWatcher>
#include <QProgressDialog>

#include "../dialogs/progressdialog.h"

namespace Ui {
    class ProjectManager;
}

class aiScene;

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

class MainWindow;

class ProjectManager : public QWidget
{
    Q_OBJECT

public:
    ProjectManager(MainWindow *window, QWidget *parent = nullptr);
    ~ProjectManager();

    void prepareStore(QString path);
    void walkFileSystem(QString folder, QString path);
    QVector<ModelData> fetchModel(const QString &path);

protected slots:
    void listWidgetCustomContextMenu(const QPoint&);
    void removeFromList();
    void deleteProject();
    void openProject();
    void openRecentProject(QListWidgetItem*);

    void handleDone();
    void handleDoneFuture();

signals:
    void fileToOpen(const QString& str);

private:
    Ui::ProjectManager *ui;
    SettingsManager* settings;

    MainWindow *window;

//    Database *db;

    QListWidgetItem *currentItem;
//    QProgressDialog *dialog;
    QString pathToOpen;
    QFutureWatcher<QVector<ModelData>> *futureWatcher;

    QSharedPointer<ProgressDialog> progressDialog;
    bool isNewProject;
    bool isMainWindowActive;
};

struct AssetWidgetConcurrentWrapper {
    ProjectManager *instance;
    typedef QVector<ModelData> result_type;
    AssetWidgetConcurrentWrapper(ProjectManager *inst) : instance(inst) {}
        result_type operator()(const QString &data) {
        return instance->fetchModel(data);
    }
};

#endif // PROJECTMANAGER_H
