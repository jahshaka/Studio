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
#include "../dialogs/newprojectdialog.h"
#include "../globals.h"
#include "../constants.h"

#include "../core/thumbnailmanager.h"

#include "../core/guidmanager.h"
#include "../io/assetmanager.h"

#include "dynamicgrid.h"
#include "gridwidget.h"

#include <QDebug>
#include <QGraphicsDropShadowEffect>
#include <QLineEdit>
#include <QFontDatabase>
#include <QMenu>

#include "itemgridwidget.hpp"

void reducer(QVector<ModelData> &accum, const QVector<ModelData> &interm)
{
    accum.append(interm);
}

ProjectManager::ProjectManager(QWidget *parent) : QWidget(parent), ui(new Ui::ProjectManager)
{
    ui->setupUi(this);

//    int id = QFontDatabase::addApplicationFont(":/fonts/Roboto-Medium.ttf");
////    QMessageBox::information(NULL,"Message",QString::number(id));  // this shows id is 0.

//        QFile fontFile(IrisUtils::getAbsoluteAssetPath("app/fonts/OpenSans-Bold.ttf"));
//        if (fontFile.exists()) {
//            fontFile.open(QIODevice::ReadOnly);
//            QFontDatabase::addApplicationFontFromData(fontFile.readAll());
//            QApplication::setFont(QFont("Open Sans", 9));
//        }

//    QFont font;
//    font.setFamily("Roboto");
//    font.setPointSize(30);
//    ui->commandLinkButton->setFont(font);
//    ui->commandLinkButton->setText("\uf021"); // this shows the Refresh icon.

//    setFont(font);

    setWindowTitle("Jahshaka Desktop");

//    ui->controls->setVisible(false);

    settings = SettingsManager::getDefaultManager();

    ui->comboBox->setCurrentText(settings->getValue("tileSize", "Normal").toString());

    connect(ui->comboBox, SIGNAL(currentTextChanged(QString)), SLOT(scaleTile(QString)));

    connect(ui->newProject, SIGNAL(pressed()), SLOT(newProject()));

    searchTimer = new QTimer(this);
    searchTimer->setSingleShot(true);   // timer can only fire once after started

    connect(searchTimer, &QTimer::timeout, this, [this]() {
        dynamicGrid->searchTiles(searchTerm.toLower());
    });

//    connect(ui->lineEdit, SIGNAL(textChanged(QString)), SLOT(searchProjects(QString)));
    connect(ui->lineEdit, &QLineEdit::textChanged, this, [this](const QString &searchTerm) {
        this->searchTerm = searchTerm;
        searchTimer->start(100);
    });

    connect(ui->comboBox, &QComboBox::currentTextChanged, [this](const QString &changedText) {
        settings->setValue("tileSize", changedText);
    });

    dynamicGrid = new DynamicGrid(this);

    ui->browseProjects->setCursor(Qt::PointingHandCursor);
    connect(ui->browseProjects, SIGNAL(pressed()), SLOT(openSampleBrowser()));

    update();

    QGridLayout *layout = new QGridLayout();
    layout->addWidget(dynamicGrid);
    layout->setMargin(0);



    ui->pmcont->setStyleSheet("border: none");
    ui->pmcont->setLayout(layout);
}

void ProjectManager::openProjectFromWidget(ItemGridWidget *widget)
{
    auto projectFile = QFileInfo(widget->projectName);
    auto projectPath = projectFile.absolutePath();
    Globals::project->setProjectPath(projectPath);

    prepareStore(projectFile.absoluteFilePath());

    this->close();
}

void ProjectManager::playProjectFromWidget(ItemGridWidget *widget)
{
    auto projectFile = QFileInfo(widget->projectName);
    auto projectPath = projectFile.absolutePath();
    Globals::project->setProjectPath(projectPath);

    prepareStore(projectFile.absoluteFilePath(), true);

    this->close();
}

void ProjectManager::renameProjectFromWidget(ItemGridWidget *widget)
{
    auto finfo = QFileInfo(widget->projectName);
    auto originalFile = widget->projectName;
    auto projectPath = finfo.absolutePath();

    QDir baseDir(projectPath);
    baseDir.cdUp();


    QDir dir;
    auto fileRename = dir.rename(originalFile, projectPath + "/" + widget->labelText + Constants::PROJ_EXT);
    auto dirRename = dir.rename(projectPath, baseDir.absolutePath() + "/" + widget->labelText);
//    QDir nf;
//    auto proRename = nf.rename(originalFile, widget->labelText);
    if (dirRename && fileRename) {
        widget->updateLabel(widget->labelText);
    } else {
        QMessageBox::StandardButton err;
        err = QMessageBox::warning(this,
                                   "Rename failed",
                                   "Failed to rename project, please try again or rename manually",
                                   QMessageBox::Ok);
    }
}

void ProjectManager::closeProjectFromWidget(ItemGridWidget *widget)
{

}

void ProjectManager::deleteProjectFromWidget(ItemGridWidget *widget)
{
    auto finfo = QFileInfo(widget->projectName);
    auto projectPath = finfo.absolutePath();

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this,
                                  "Deleting Project",
                                  "Are you sure you want to delete this project?",
                                  QMessageBox::Yes | QMessageBox::Cancel);
    if (reply == QMessageBox::Yes) {
        QDir dir(projectPath);
        if (dir.removeRecursively()) {
            dynamicGrid->deleteTile(widget);
            delete widget;
        } else {
            QMessageBox::StandardButton err;
            err = QMessageBox::warning(this,
                                       "Delete failed",
                                       "Failed to remove project, please try again or delete manually",
                                       QMessageBox::Ok);
        }
    }
}

void ProjectManager::searchProjects()
{
    dynamicGrid->searchTiles(ui->lineEdit->text());
}

void ProjectManager::update()
{
    auto path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
    auto projectFolder = settings->getValue("default_directory", path).toString();

    QDir dir(projectFolder);
    QFileInfoList files = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs);

    int i = 0;
    foreach (const QFileInfo &file, files) {
        dynamicGrid->addToGridView(new GridWidget(file.absoluteFilePath()), i);
        i++;
    }
}

void ProjectManager::resizeEvent(QResizeEvent *event)
{
//    qDebug() << event->size().width();

//    if (event->size().width() <= 874) {
//        ui->listWidget->setIconSize(QSize(312, 256));
//        ui->listWidget->setGridSize(QSize(336, 256));
//    } else {
//        ui->listWidget->setIconSize(QSize(256, 256));
////        ui->listWidget->setGridSize(QSize(event->size().width() / 3, 256));
    //    }
}

void ProjectManager::closeEvent(QCloseEvent *event)
{
//    qDebug() << "nice";
}

void ProjectManager::listWidgetCustomContextMenu(const QPoint &pos)
{
//    QModelIndex index = ui->listWidget->indexAt(pos);

//    QMenu menu;
//    QAction *action;

//    if (index.isValid()) {
//        currentItem = ui->listWidget->itemAt(pos);

//        folder = currentItem->data(Qt::UserRole).toString();
//        auto f = QFileInfo(folder);
//        folder = f.absolutePath();

//        auto icon = currentItem->icon();
////        ui->name->setText(currentItem->text());
////        ui->preview->setPixmap(icon.pixmap(QSize(212, 212)));

////        assetItem.selectedPath = item->data(Qt::UserRole).toString();

//        // check if current project is open first
//        if (currentItem->data(Qt::DisplayRole).toString() != Globals::project->getProjectName()) {
//            action = new QAction(QIcon(), "Delete", this);
//            connect(action, SIGNAL(triggered()), this, SLOT(deleteProject()));
//            menu.addAction(action);
//        }

//        action = new QAction(QIcon(), "Rename", this);
//        connect(action, SIGNAL(triggered()), this, SLOT(renameProject()));
//        menu.addAction(action);
//    }

//    menu.exec(ui->listWidget->mapToGlobal(pos));
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
//    if (!folder.isEmpty()) {
//        QDir dir(folder);

//        if (dir.exists()) {
//            if (dir.removeRecursively()) {
//                if (currentItem) {
//                    delete ui->listWidget->takeItem(ui->listWidget->row(currentItem));
//                }
//            }
//        }
//    }
}

void ProjectManager::openProject()
{
    auto projectFile = QFileInfo(loadProjectDelegate());
    auto projectPath = projectFile.absolutePath();

    if (!projectPath.isEmpty()) {
//        Globals::project->setProjectPath(projectPath);
        Globals::project->setProjectPath(projectPath);

        prepareStore(projectFile.absoluteFilePath());

//        prepareStore(projectFile.absoluteFilePath());
//        window = new MainWindow;
//        window->showMaximized();
//        window->openProject(projectFile.absoluteFilePath());

//        settings->addRecentlyOpenedScene(projectFile.absoluteFilePath());
//        this->close();
//        emit accepted();

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


bool ProjectManager::copyDirectoryFiles(const QString &fromDir, const QString &toDir, bool coverFileIfExist)
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

void ProjectManager::openSampleProject(QListWidgetItem *item)
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

//                    settings->addRecentlyOpenedScene(sln);
                }

//                emit accepted();
//                this->close();

                sampleDialog.close();
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

//                settings->addRecentlyOpenedScene(sln);
            }

//            emit accepted();
//            this->close();
            sampleDialog.close();
        }
    }
}

void ProjectManager::renameItem(QListWidgetItem *item)
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

QString ProjectManager::loadProjectDelegate()
{
    auto projectFileName = QFileDialog::getOpenFileName(this, "Select Project File",
                                                        nullptr, "Jahshaka Project File (*.jah)");
    return projectFileName;
}

void ProjectManager::newProject()
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

        emit fileToCreate(projectName, fullProjectPath);

//        window = new MainWindow;
//        window->setAttribute(Qt::WA_DeleteOnClose);
//        window->showMaximized();
//        window->newProject(projectName, fullProjectPath);
//        settings->addRecentlyOpenedScene(slnName);
//        settings->setValue("last_wd", projectPath);

//        emit accepted();
//        this->close();

        this->hide();
    }
}

void ProjectManager::renameProject()
{
//    ui->listWidget->editItem(currentItem);
}

void ProjectManager::updateCurrentItem(QListWidgetItem *item)
{
    folder = item->data(Qt::UserRole).toString();
    auto f = QFileInfo(folder);
    folder = f.absolutePath();

//    auto icon = item->icon();
//    ui->name->setText(item->text());
//    ui->preview->setPixmap(icon.pixmap(QSize(212, 212)));
}

void ProjectManager::myProjects()
{
//    ui->projects->setStyleSheet("background: #4998ff");
//    ui->samples->setStyleSheet("background: #444");
//    ui->stackedWidget->setCurrentIndex(0);
}

void ProjectManager::sampleProjects()
{
//    ui->samples->setStyleSheet("background: #4998ff");
//    ui->projects->setStyleSheet("background: #444");
    //    ui->stackedWidget->setCurrentIndex(1);
}

void ProjectManager::scaleTile(QString scale)
{
    dynamicGrid->scaleTile(scale);
}

void ProjectManager::searchTiles(QString search)
{
//    auto srch = search.toLower();
//    dynamicGrid->searchTiles(srch);
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

//     this->close();

     progressDialog->setLabelText(QString("Initializing panels..."));

     emit fileToOpen(pathToOpen);

     progressDialog->close();

     this->hide();
}

void ProjectManager::handleDonePlay()
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

//     this->close();

     progressDialog->setLabelText(QString("Initializing panels..."));

     emit fileToPlay(pathToOpen);

     progressDialog->close();

     this->hide();
}

void ProjectManager::handleDoneFuture()
{

}

void ProjectManager::OnLstItemsCommitData(QWidget *listItem)
{
    QString folderName = reinterpret_cast<QLineEdit*>(listItem)->text();
//    qDebug() << folderName;

//    QDir dir(assetItem.selectedPath + '/' + folderName);
//    if (!dir.exists()) {
//        dir.mkpath(".");
//    }

//    auto child = ui->assetTree->currentItem();

//    if (child) {    // should always be set but just in case
//        auto branch = new QTreeWidgetItem();
//        branch->setIcon(0, QIcon(":/app/icons/folder-symbol.svg"));
//        branch->setText(0, folderName);
//        branch->setData(0, Qt::UserRole, assetItem.selectedPath + '/' + folderName);
//        child->addChild(branch);

//        ui->assetTree->clearSelection();
//        branch->setSelected(true);
    //    }
}

void ProjectManager::openSampleBrowser()
{
    sampleDialog.setFixedSize(Constants::TILE_SIZE * 1.66);
    sampleDialog.setWindowFlags(sampleDialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    sampleDialog.setWindowTitle("Jahshaka Sample Browser");

    QGridLayout *layout = new QGridLayout();
    QListWidget *sampleList = new QListWidget();
    sampleList->setObjectName("sampleList");
    sampleList->setStyleSheet("#sampleList { background-color: #1e1e1e; padding: 8px; border: none } QListWidgetItem { padding: 12px; }");
    sampleList->setViewMode(QListWidget::IconMode);
    sampleList->setSizeAdjustPolicy(QListWidget::AdjustToContents);
    sampleList->setSpacing(4);
    sampleList->setResizeMode(QListWidget::Adjust);
    sampleList->setMovement(QListView::Static);
    sampleList->setIconSize(Constants::TILE_SIZE * 0.5);
    sampleList->setSelectionMode(QAbstractItemView::SingleSelection);

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
            item->setIcon(QIcon(":/app/images/preview.png"));
        }

        sampleList->addItem(item);
    }

    connect(sampleList, SIGNAL(itemDoubleClicked(QListWidgetItem*)), SLOT(openSampleProject(QListWidgetItem*)));

    auto instructions = new QLabel("Double click on a sample project to open it in the editor");
    instructions->setObjectName("instructions");
    instructions->setStyleSheet("#instructions { border: none; background: #1e1e1e; padding: 10px; font-size: 12px }");
    layout->addWidget(instructions);
    layout->addWidget(sampleList);
    layout->setMargin(0);
    layout->setSpacing(0);

    sampleDialog.setLayout(layout);
    sampleDialog.exec();
}

void ProjectManager::test()
{
//    qDebug() << "what";
//    ui->listWidget->setVisible(true);
//    this->repaint();
}

void ProjectManager::prepareStore(QString path, bool playMode)
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

      if (playMode) {
          QObject::connect(futureWatcher, SIGNAL(finished()), SLOT(handleDonePlay()));
      } else {
          QObject::connect(futureWatcher, SIGNAL(finished()), SLOT(handleDone()));
      }

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
