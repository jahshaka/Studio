#include <QFile>
#include <QFontDatabase>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMessageBox>

#include "src/irisgl/src/core/irisutils.h"
#include "../mainwindow.h"
#include "../core/settingsmanager.h"

#include "newprojectdialog.h"

#include "projectdialog.h"
#include "ui_projectdialog.h"

#include "../core/project.h"
#include "../core/database/database.h"
#include "../globals.h"
#include "../constants.h"

#include <QDebug>
#include <QFileDialog>
#include <QMenu>

#include "../core/thumbnailmanager.h"

#include "../core/guidmanager.h"
#include "../io/assetmanager.h"

#include <QFutureWatcher>
#include <QProgressDialog>
#include <QThread>
#include <QtConcurrent/QtConcurrent>
#include <chrono>

void reducer(QVector<ModelData> &accum, const QVector<ModelData> &interm)
{
    accum.append(interm);
}

ProjectDialog::ProjectDialog(bool mainWindowActive, QDialog *parent) : QDialog(parent), ui(new Ui::ProjectDialog)
{
    ui->setupUi(this);

    this->setWindowTitle("Jahshaka");

//    QFile fontFile(IrisUtils::getAbsoluteAssetPath("app/fonts/OpenSans-Bold.ttf"));
//    if (fontFile.exists()) {
//        fontFile.open(QIODevice::ReadOnly);
//        QFontDatabase::addApplicationFontFromData(fontFile.readAll());
//        QApplication::setFont(QFont("Open Sans", 9));
//    }

    isMainWindowActive = mainWindowActive;

    ui->listWidget->setViewMode(QListWidget::IconMode);
    ui->listWidget->setIconSize(QSize(256, 256));
    ui->listWidget->setResizeMode(QListWidget::Adjust);
    ui->listWidget->setMovement(QListView::Static);
    ui->listWidget->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->listWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    ui->listWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->demoList->setViewMode(QListWidget::IconMode);
    ui->demoList->setIconSize(QSize(256, 256));
    ui->demoList->setResizeMode(QListWidget::Adjust);
    ui->demoList->setMovement(QListView::Static);
    ui->demoList->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->demoList->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(ui->newProject,     SIGNAL(pressed()), SLOT(newScene()));
    connect(ui->openProject,    SIGNAL(pressed()), SLOT(openProject()));
    connect(ui->listWidget,     SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,               SLOT(openRecentProject(QListWidgetItem*)));
    connect(ui->listWidget,     SIGNAL(customContextMenuRequested(const QPoint&)),
            this,               SLOT(listWidgetCustomContextMenu(const QPoint&)));
    connect(ui->demoList,       SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,               SLOT(openSampleProject(QListWidgetItem*)));

    settings = SettingsManager::getDefaultManager();

    ui->label->hide();

    if (settings->getRecentlyOpenedScenes().count()) {
        for (auto recentItem : settings->getRecentlyOpenedScenes()) {
            auto item = new QListWidgetItem();
            item->setData(Qt::UserRole, recentItem);
            item->setToolTip(recentItem);
            auto fn = QFileInfo(recentItem);
            if (QFile::exists(fn.absolutePath() + "/Metadata/preview.png")) {
                item->setIcon(QIcon(fn.absolutePath() + "/Metadata/preview.png"));
            } else {
                item->setIcon(QIcon(":/images/no_preview.png"));
            }
            item->setText(fn.baseName());
            ui->listWidget->addItem(item);
        }
    } else {
        ui->listWidget->hide();
        ui->label->show();
    }

    QDir dir(IrisUtils::getAbsoluteAssetPath(Constants::SAMPLES_FOLDER));
    QFileInfoList files = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs);
    foreach (const QFileInfo &file, files) {
        auto item = new QListWidgetItem();
        item->setToolTip(file.absoluteFilePath());
        item->setData(Qt::DisplayRole, file.baseName());
        item->setData(Qt::UserRole, file.absoluteFilePath() + "/" + file.baseName() + Constants::PROJ_EXT);
        if (QFile::exists(file.absoluteFilePath() + "/Metadata/preview.png")) {
            item->setIcon(QIcon(file.absoluteFilePath() + "/Metadata/preview.png"));
        } else {
            item->setIcon(QIcon(":/images/no_preview.png"));
        }
        ui->demoList->addItem(item);
    }

    isNewProject = false;
}

ProjectDialog::~ProjectDialog()
{
    delete ui;
}

void ProjectDialog::newScene()
{
    NewProjectDialog dialog;

    dialog.exec();

    auto projectName = dialog.getProjectInfo().projectName;
    auto projectPath = dialog.getProjectInfo().projectPath;

    if (!projectName.isEmpty() || !projectName.isNull()) {
        auto fullProjectPath = QDir(projectPath).filePath(projectName);
        auto slnName = QDir(fullProjectPath).filePath(projectName + Constants::PROJ_EXT);

        Globals::project->setFilePath(slnName);
        Globals::project->setProjectPath(fullProjectPath);

        // make a dir and the default subfolders
        QDir projectDir(fullProjectPath);
        if (!projectDir.exists()) projectDir.mkpath(".");

        for (auto folder : Constants::PROJECT_DIRS) {
            QDir dir(QDir(fullProjectPath).filePath(folder));
            dir.mkpath(".");
        }

        window = new MainWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->showMaximized();
        window->newProject(projectName, fullProjectPath);
        settings->addRecentlyOpenedScene(slnName);
//        settings->setValue("last_wd", projectPath);

        emit accepted();
        this->close();
    }
}

void ProjectDialog::openProject()
{
    auto projectFile = QFileInfo(loadProjectDelegate());
    auto projectPath = projectFile.absolutePath();

    if (!projectPath.isEmpty()) {
        Globals::project->setProjectPath(projectPath);


        prepareStore(projectFile.absoluteFilePath());
//        window = new MainWindow;
//        window->showMaximized();
//        window->openProject(projectFile.absoluteFilePath());

        settings->addRecentlyOpenedScene(projectFile.absoluteFilePath());
//        this->close();
        emit accepted();
    }

    else {
        // QMessageBox msgBox;
        // msgBox.setStyleSheet("QLabel { width: 128px; }");
        // msgBox.setIcon(QMessageBox::Warning);
        // msgBox.setText("Unable to locate project!");
        // msgBox.setInformativeText("You did not select a folder or the path is invalid");
        // msgBox.setStandardButtons(QMessageBox::Ok);
        // msgBox.exec();
    }
}

QString ProjectDialog::loadProjectDelegate()
{
    auto projectFileName = QFileDialog::getOpenFileName(this, "Select Project File",
                                                        nullptr, "Jahshaka Project File (*.jah)");
    return projectFileName;
}

void ProjectDialog::openRecentProject(QListWidgetItem *item)
{
    auto projectFile = QFileInfo(item->data(Qt::UserRole).toString());
    auto projectPath = projectFile.absolutePath();
    Globals::project->setProjectPath(projectPath);

    prepareStore(projectFile.absoluteFilePath());

    settings->addRecentlyOpenedScene(projectFile.absoluteFilePath());

//    window = new MainWindow;
//    window->showMaximized();

//    window->openProject(projectFile.absoluteFilePath());

//    this->close();
    emit accepted();
}

bool ProjectDialog::copyDirectoryFiles(const QString &fromDir, const QString &toDir, bool coverFileIfExist)
{
    QDir sourceDir(fromDir);
    QDir targetDir(toDir);
    if(!targetDir.exists()){    /* if directory don't exists, build it */
        if(!targetDir.mkdir(targetDir.absolutePath()))
            return false;
    }

    QFileInfoList fileInfoList = sourceDir.entryInfoList();
    foreach(QFileInfo fileInfo, fileInfoList){
        if(fileInfo.fileName() == "." || fileInfo.fileName() == "..")
            continue;

        if(fileInfo.isDir()){    /* if it is directory, copy recursively*/
            if(!copyDirectoryFiles(fileInfo.filePath(),
                targetDir.filePath(fileInfo.fileName()),
                coverFileIfExist))
                return false;
        }
        else{            /* if coverFileIfExist == true, remove old file first */
            if(coverFileIfExist && targetDir.exists(fileInfo.fileName())){
                targetDir.remove(fileInfo.fileName());
            }

            // files copy
            if(!QFile::copy(fileInfo.filePath(),
                targetDir.filePath(fileInfo.fileName()))){
                    return false;
            }
        }
    }
    return true;
}

void ProjectDialog::openSampleProject(QListWidgetItem *item)
{
    auto path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                + Constants::PROJECT_FOLDER;
    auto projectFolder = settings->getValue("default_directory", path).toString();

    QDir targetDir(projectFolder);
    if(!targetDir.exists()){    /* if directory don't exists, build it */
        targetDir.mkdir(targetDir.absolutePath());
    }

    if (QDir(projectFolder + "/" + item->data(Qt::DisplayRole).toString()).exists()) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Project Path not Empty", "Project already Exists! Overwrite?",
                                        QMessageBox::Yes|QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            if (!projectFolder.isEmpty()) {
                auto projectFile = QFileInfo(item->data(Qt::UserRole).toString());

                QString dest = QDir(projectFolder).filePath(projectFile.baseName());
                if (this->copyDirectoryFiles(projectFile.absolutePath(), dest, true)) {

                    auto newProjectFile = QFileInfo(dest);
                    auto projectPath = QDir(newProjectFile.absolutePath()).filePath(projectFile.baseName());
                    Globals::project->setProjectPath(projectPath);

                    auto sln = QDir(projectPath).filePath(projectFile.fileName());

                    prepareStore(sln);

                    settings->addRecentlyOpenedScene(sln);
                }

                emit accepted();
                this->close();
            }
        }
    } else {
        if (!projectFolder.isEmpty()) {
            auto projectFile = QFileInfo(item->data(Qt::UserRole).toString());

            QString dest = QDir(projectFolder).filePath(projectFile.baseName());
            if (this->copyDirectoryFiles(projectFile.absolutePath(), dest, true)) {

                auto newProjectFile = QFileInfo(dest);
                auto projectPath = QDir(newProjectFile.absolutePath()).filePath(projectFile.baseName());
                Globals::project->setProjectPath(projectPath);

                auto sln = QDir(projectPath).filePath(projectFile.fileName());

                prepareStore(sln);

                settings->addRecentlyOpenedScene(sln);
            }

            emit accepted();
            this->close();
        }
    }
}

void ProjectDialog::listWidgetCustomContextMenu(const QPoint &pos)
{
    QModelIndex index = ui->listWidget->indexAt(pos);

    QMenu menu;
    QAction *action;

    if (index.isValid()) {
        currentItem = ui->listWidget->itemAt(pos);
//        assetItem.selectedPath = item->data(Qt::UserRole).toString();

        action = new QAction(QIcon(), "Remove from recent list", this);
        connect(action, SIGNAL(triggered()), this, SLOT(removeFromList()));
        menu.addAction(action);

        if (!isMainWindowActive) {
            action = new QAction(QIcon(), "Delete project", this);
            connect(action, SIGNAL(triggered()), this, SLOT(deleteProject()));
            menu.addAction(action);
        }
    }

    menu.exec(ui->listWidget->mapToGlobal(pos));
}

void ProjectDialog::removeFromList()
{
    auto selectedInfo = QFileInfo(currentItem->data(Qt::UserRole).toString());
    delete ui->listWidget->takeItem(ui->listWidget->row(currentItem));
    settings->removeRecentlyOpenedEntry(selectedInfo.absoluteFilePath());

    if (!ui->listWidget->count()) {
        ui->label->show();
        ui->listWidget->hide();
    }
}

void ProjectDialog::deleteProject()
{
    auto selectedInfo = QFileInfo(currentItem->data(Qt::UserRole).toString());

    QDir dir(selectedInfo.absolutePath());
    if (dir.removeRecursively()) {
        removeFromList();
    }
}

void ProjectDialog::handleDone()
{
    progressDialog->setRange(0, 0);

    progressDialog->setLabelText(QString("Populating scene..."));

     for (auto item : futureWatcher->result()) {
         for (int i = 0; i < AssetManager::assets.count(); i++) {
             if (AssetManager::assets[i]->path == item.path) {
                AssimpObject *ao = new AssimpObject(item.data, item.path);
                AssetObject *model = new AssetObject(ao, item.path);

                QVariant v;
                v.setValue(ao);

                AssetManager::assets[i] = model;
             }
         }
     }

     this->close();

     progressDialog->setLabelText(QString("Initializing interface..."));

     window = new MainWindow;
     window->showMaximized();
     window->setVisible(false);

     progressDialog->close();

//     progressDialog->reset();
     window->openProject(pathToOpen);
 //    window->showMaximized();
     window->setVisible(true);

}

void ProjectDialog::handleDoneFuture()
{

}

void ProjectDialog::newSceneRequested()
{
   isNewProject = true;
}

SettingsManager *ProjectDialog::getSettingsManager()
{
    return settings;
}

void ProjectDialog::prepareStore(QString path)
{
    pathToOpen = path;

    // populate asset list
    QDir d(Globals::project->getProjectFolder());
    walkFileSystem("", d.absolutePath());

    // models to thread import
    // Prepare the vector.
      QStringList fileNames;

      // start the magic
      for (auto asset : AssetManager::assets) {
          if (asset->type == AssetType::Object) {
  //            QFile file(asset->path);
  //            file.open(QFile::ReadOnly);
  //            asset->data = file.readAll();
  //            file.close();

              fileNames.append(asset->path);

  //            AssetManager::modelAssets.append(static_cast<AssetModel*>(asset));
          }
      }

//      std::chrono::time_point<std::chrono::system_clock> start, end;
//      start = std::chrono::system_clock::now();

  //    for (auto asset : AssetManager::modelAssets) {
  //        qDebug() << "cracking a cold one open with " << asset->fileName;
  //        Assimp::Importer importer;
  //        const aiScene *scene = importer.ReadFileFromMemory((void*) asset->data.data(),
  //                                                           asset->data.length(),
  //                                                           aiProcessPreset_TargetRealtime_Fast);
  //    }
  //    4 seconds

  //    // Create a progress dialog.
//      dialog = new QProgressDialog;
      progressDialog = QSharedPointer<ProgressDialog>(new ProgressDialog);
  //    QProgressDialog dialog;
  //    dialog.setLabelText(QString("Progressing using %1 thread(s)...").arg(QThread::idealThreadCount()));
      progressDialog->setLabelText("Loading assets...");

      // Create a QFutureWatcher and connect signals and slots.
      futureWatcher = new QFutureWatcher<QVector<ModelData>>();

      QObject::connect(futureWatcher, SIGNAL(finished()), SLOT(handleDone()));
      QObject::connect(futureWatcher, SIGNAL(finished()), SLOT(handleDoneFuture()));
      QObject::connect(futureWatcher, SIGNAL(finished()), futureWatcher, SLOT(deleteLater()));
//      QObject::connect(progressDialog.data(), SIGNAL(canceled()), futureWatcher, SLOT(cancel()));
      QObject::connect(futureWatcher, SIGNAL(progressRangeChanged(int,int)), progressDialog.data(), SLOT(setRange(int, int)));
      QObject::connect(futureWatcher, SIGNAL(progressValueChanged(int)), progressDialog.data(), SLOT(setValue(int)));

      // Start the computation.
      AssetWidgetConcurrentWrapper loadWrapper(this);
      auto future = QtConcurrent::mappedReduced(fileNames, loadWrapper, reducer, QtConcurrent::SequentialReduce);
  //    auto future = QtConcurrent::mapped(fileNames, AssetWidgetConcurrentWrapper2(this));
      futureWatcher->setFuture(future);

      // Display the dialog and start the event loop.
//      dialog->exec();
      progressDialog->exec();

      futureWatcher->waitForFinished();

      // Query the future to check if was canceled.
  //    qDebug() << "Canceled?" << futureWatcher.future().isCanceled();

//      end = std::chrono::system_clock::now();

//      std::chrono::duration<double> elapsed_seconds = end - start;
//      std::time_t end_time = std::chrono::system_clock::to_time_t(end);

//      qDebug() << "\nfinished computation at " << std::ctime(&end_time)
//               << "elapsed time: " << elapsed_seconds.count() << "s\n";
}

void ProjectDialog::walkFileSystem(QString folder, QString path)
{
  QDir dir(path);
  QFileInfoList files = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);
  foreach (const QFileInfo &file, files) {
      // TODO - maybe add some OS centric code to check for hidden folder
      if (file.suffix() != Constants::PROJ_EXT) {
          // TODO -- get type from extension if is a file
          if (file.isFile()) {
              AssetType type;
              QPixmap pixmap;

              if (file.suffix() == "jpg" || file.suffix() == "png" || file.suffix() == "bmp") {
                  auto thumb = ThumbnailManager::createThumbnail(file.absoluteFilePath(), 256, 256);
                  pixmap = QPixmap::fromImage(*thumb->thumb);
                  type = AssetType::Texture;
              } else if (file.suffix() == "obj" || file.suffix() == "fbx" || file.suffix() == "dae") {
                  auto thumb = ThumbnailManager::createThumbnail(":/icons/google-drive-file.svg", 128, 128);
                  type = AssetType::Object;
                  pixmap = QPixmap::fromImage(*thumb->thumb);
              } else if (file.suffix() == "shader") {
                  auto thumb = ThumbnailManager::createThumbnail(":/icons/google-drive-file.svg", 128, 128);
                  type = AssetType::Shader;
                  pixmap = QPixmap::fromImage(*thumb->thumb);
              } else {
                  auto thumb = ThumbnailManager::createThumbnail(":/icons/google-drive-file.svg", 128, 128);
                  type = AssetType::File;
                  pixmap = QPixmap::fromImage(*thumb->thumb);
              }

              auto asset = new AssetVariant;
              asset->type = type;
              asset->fileName = file.fileName();
              asset->path = file.absoluteFilePath();
              asset->thumbnail = pixmap;

              AssetManager::assets.append(asset);
          } else {
              auto thumb = ThumbnailManager::createThumbnail(":/icons/folder-symbol.svg", 128, 128);
              QPixmap pixmap = QPixmap::fromImage(*thumb->thumb);

              auto asset = new AssetFolder;
              asset->fileName = file.fileName();
              asset->path = file.absoluteFilePath();
              asset->thumbnail = pixmap;

              AssetManager::assets.append(asset);
          }

          if (file.isDir()) {
              walkFileSystem("", file.absoluteFilePath());
          }
      }
  }
}

QVector<ModelData> ProjectDialog::fetchModel(const QString &filePath)
{
  QVector<ModelData> sceneVec;
  QFile file(filePath);
  file.open(QFile::ReadOnly);
  auto data = file.readAll();
//    qDebug() << "called???";

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

bool ProjectDialog::newInstance(Database *db)
{
    connect(this, SIGNAL(accepted()), SLOT(newSceneRequested()));
    ProjectDialog::exec();
    return isNewProject;
}

bool ProjectDialog::eventFilter(QObject *watched, QEvent *event)
{
    return QObject::eventFilter(watched, event);
}
