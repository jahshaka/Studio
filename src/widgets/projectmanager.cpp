#include "projectmanager.h"
#include "ui_projectmanager.h"

#include <QStandardPaths>
#include "../constants.h"
#include "../irisgl/src/core/irisutils.h"
#include "../core/settingsmanager.h"

#include <QFutureWatcher>
#include <QProgressDialog>
#include <QThread>
#include <QtConcurrent/QtConcurrent>
#include <chrono>

#include "../mainwindow.h"
#include "../core/project.h"
#include "../core/database/database.h"
#include "../globals.h"
#include "../constants.h"

#include "../core/thumbnailmanager.h"

#include "../core/guidmanager.h"
#include "../io/assetmanager.h"

#include <QDebug>
#include <QMenu>

void reducer(QVector<ModelData> &accum, const QVector<ModelData> &interm)
{
    accum.append(interm);
}

ProjectManager::ProjectManager(MainWindow *window, QWidget *parent) : QWidget(parent), ui(new Ui::ProjectManager)
{
    ui->setupUi(this);

    window = window;

    ui->listWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->listWidget->setFlow(QListView::LeftToRight);
    ui->listWidget->setViewMode(QListWidget::IconMode);
//    ui->listWidget->setSpacing(12);
//    ui->listWidget->setIconSize(QSize(192, 160));
    ui->listWidget->setUniformItemSizes(true);
    ui->listWidget->setSizeAdjustPolicy(QListWidget::AdjustToContents);
    ui->listWidget->setResizeMode(QListWidget::Adjust);
    ui->listWidget->setMovement(QListView::Static);
    ui->listWidget->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->listWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    settings = SettingsManager::getDefaultManager();

    auto path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
    auto projectFolder = settings->getValue("default_directory", path).toString();

    QDir dir(projectFolder);
    QFileInfoList files = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs);

    foreach (const QFileInfo &file, files) {
        auto item = new QListWidgetItem();
        item->setToolTip(file.absoluteFilePath());
        item->setData(Qt::DisplayRole, file.baseName());
        item->setData(Qt::UserRole, file.absoluteFilePath() + "/" + file.baseName() + Constants::PROJ_EXT);
        if (QFile::exists(file.absoluteFilePath() + "/Metadata/preview.png")) {
            item->setIcon(QIcon(file.absoluteFilePath() + "/Metadata/preview.png"));
        } else {
            item->setIcon(QIcon(":/app/images/no_preview.png"));
        }
        ui->listWidget->addItem(item);
    }

    connect(ui->deleteProject,  SIGNAL(pressed()), SLOT(deleteProject()));
    connect(ui->listWidget,     SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,               SLOT(openRecentProject(QListWidgetItem*)));
    connect(ui->listWidget,     SIGNAL(customContextMenuRequested(const QPoint&)),
            this,               SLOT(listWidgetCustomContextMenu(const QPoint&)));
}

void ProjectManager::listWidgetCustomContextMenu(const QPoint &pos)
{
    QModelIndex index = ui->listWidget->indexAt(pos);

    QMenu menu;
    QAction *action;

    if (index.isValid()) {
        currentItem = ui->listWidget->itemAt(pos);
//        assetItem.selectedPath = item->data(Qt::UserRole).toString();

        // check if current project is open first
        if (true) {
            action = new QAction(QIcon(), "Delete project", this);
            connect(action, SIGNAL(triggered()), this, SLOT(deleteProject()));
            menu.addAction(action);
        }
    }

    menu.exec(ui->listWidget->mapToGlobal(pos));
}

void ProjectManager::removeFromList()
{
//    auto selectedInfo = QFileInfo(currentItem->data(Qt::UserRole).toString());
//    delete ui->listWidget->takeItem(ui->listWidget->row(currentItem));
//    settings->removeRecentlyOpenedEntry(selectedInfo.absoluteFilePath());

//    if (!ui->listWidget->count()) {
//        ui->label->show();
//        ui->listWidget->hide();
//    }
}

void ProjectManager::deleteProject()
{
    auto selectedInfo = QFileInfo(currentItem->data(Qt::UserRole).toString());

    QDir dir(selectedInfo.absolutePath());

    if (dir.removeRecursively()) {
        delete ui->listWidget->takeItem(ui->listWidget->row(currentItem));
    }
}


void ProjectManager::openProject()
{

}

void ProjectManager::openRecentProject(QListWidgetItem *item)
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
//    emit accepted();

    // switch to viewport tab
}

void ProjectManager::handleDone()
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

     progressDialog->setLabelText(QString("Initializing panels..."));

//     window = new MainWindow;
//     window->showMaximized();
//     window->setVisible(false);

     emit fileToOpen(pathToOpen);

     progressDialog->close();

//     progressDialog->reset();
//     window->openProject(pathToOpen);
 //    window->showMaximized();
//     window->setVisible(true);
}

void ProjectManager::handleDoneFuture()
{

}

//QString ProjectManager::fileToOpen(QString &str)
//{
//    return str;
//}

void ProjectManager::prepareStore(QString path)
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

void ProjectManager::walkFileSystem(QString folder, QString path)
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
                  auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                  type = AssetType::Object;
                  pixmap = QPixmap::fromImage(*thumb->thumb);
              } else if (file.suffix() == "shader") {
                  auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                  type = AssetType::Shader;
                  pixmap = QPixmap::fromImage(*thumb->thumb);
              } else {
                  auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
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
              auto thumb = ThumbnailManager::createThumbnail(":/app/icons/folder-symbol.svg", 128, 128);
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

QVector<ModelData> ProjectManager::fetchModel(const QString &filePath)
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

ProjectManager::~ProjectManager()
{
    delete ui;
}
