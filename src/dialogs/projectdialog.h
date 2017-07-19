#ifndef PROJECTDIALOG_H
#define PROJECTDIALOG_H

#include <QDialog>
#include <QListWidgetItem>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QSqlDatabase>

#include "progressdialog.h"

namespace Ui {
    class ProjectDialog;
}

class MainWindow;
class SettingsManager;
class Database;

class aiScene;

 struct ModelData {
     ModelData() = default;
     ModelData(QString p, const aiScene *ai) : path(p), data(ai) {}
     QString path;
     const aiScene *data;
 };

class ProjectDialog : public QDialog
{
    Q_OBJECT

public:
    ProjectDialog(bool mainWindowActive = false, QDialog *parent = nullptr);
    ~ProjectDialog();

    MainWindow *window;

    void setSettingsManager(SettingsManager* settings);
    QString loadProjectDelegate();
    bool copyDirectoryFiles(const QString &fromDir, const QString &toDir, bool coverFileIfExist);
    SettingsManager* getSettingsManager();
    SettingsManager* settings;
    void prepareStore(QString path);
    void walkFileSystem(QString folder, QString path);
    QVector<ModelData> fetchModel(const QString &path);

    bool newInstance(Database *db);

protected:
    bool eventFilter(QObject *watched, QEvent *event);

protected slots:
    void newScene();
    void openProject();
    void openRecentProject(QListWidgetItem*);
    void openSampleProject(QListWidgetItem*);
    void listWidgetCustomContextMenu(const QPoint&);

    void removeFromList();
    void deleteProject();

    void handleDone();
    void handleDoneFuture();

    void newSceneRequested();

private:
    Ui::ProjectDialog *ui;
    Database *db;

    QListWidgetItem *currentItem;
//    QProgressDialog *dialog;
    QString pathToOpen;
    QFutureWatcher<QVector<ModelData>> *futureWatcher;

    QSharedPointer<ProgressDialog> progressDialog;
    bool isNewProject;
    bool isMainWindowActive;
};

struct AssetWidgetConcurrentWrapper {
    ProjectDialog *instance;
    typedef QVector<ModelData> result_type;
    AssetWidgetConcurrentWrapper(ProjectDialog *inst) : instance(inst) {}
        result_type operator()(const QString &data) {
        return instance->fetchModel(data);
    }
};

#endif // PROJECTDIALOG_H
