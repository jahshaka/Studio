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

#include "src/irisgl/src/zip/zip.h"

#include "dynamicgrid.h"

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

ProjectManager::ProjectManager(Database *handle, QWidget *parent) : QWidget(parent), ui(new Ui::ProjectManager)
{
    ui->setupUi(this);
    db = handle;

    setWindowTitle("Jahshaka Desktop");

    dynamicGrid = new DynamicGrid(this);

    settings = SettingsManager::getDefaultManager();

    ui->tilePreview->setCurrentText(settings->getValue("tileSize", "Normal").toString());

    connect(ui->tilePreview,    SIGNAL(currentTextChanged(QString)), SLOT(changePreviewSize(QString)));
    connect(ui->newProject,     SIGNAL(pressed()), SLOT(newProject()));
    connect(ui->importWorld,    SIGNAL(pressed()), SLOT(importProjectFromFile()));
    connect(ui->browseProjects, SIGNAL(pressed()), SLOT(openSampleBrowser()));
    ui->browseProjects->setCursor(Qt::PointingHandCursor);

    searchTimer = new QTimer(this);
    searchTimer->setSingleShot(true);   // timer can only fire once after started

    connect(searchTimer, &QTimer::timeout, this, [this]() {
        dynamicGrid->searchTiles(searchTerm.toLower());
    });

    connect(ui->lineEdit, &QLineEdit::textChanged, this, [this](const QString &searchTerm) {
        this->searchTerm = searchTerm;
        searchTimer->start(100);
    });

    connect(ui->tilePreview, &QComboBox::currentTextChanged, [this](const QString &changedText) {
        settings->setValue("tileSize", changedText);
    });

    populateDesktop();

    QGridLayout *layout = new QGridLayout();
    layout->addWidget(dynamicGrid);
    layout->setMargin(0);

    ui->pmContainer->setStyleSheet("border: none");
    ui->pmContainer->setLayout(layout);
}

void ProjectManager::openProjectFromWidget(ItemGridWidget *widget, bool playMode)
{
    auto spath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
    auto projectFolder = SettingsManager::getDefaultManager()->getValue("default_directory", spath).toString();

    Globals::project->setProjectPath(QDir(projectFolder).filePath(widget->tileData.name));
    Globals::project->setProjectGuid(widget->tileData.guid);

    this->openInPlayMode = playMode;

    loadProjectAssets();
}

QString importProjectName;
int on_extract_entry(const char *filename, void *arg) {
    QFileInfo fInfo(filename);
    if (fInfo.suffix() == "db") importProjectName = fInfo.baseName();
    return 0;
}

void ProjectManager::importProjectFromFile()
{
    auto filename = QFileDialog::getOpenFileName(this,      "Import World",
                                                 nullptr,   "Jahshaka Project (*.zip)");

    if (filename.isEmpty() || filename.isNull()) return;

    // get the current project working directory
    auto pFldr = IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                 Constants::PROJECT_FOLDER);
    auto defaultProjectDirectory = settings->getValue("default_directory", pFldr).toString();

    // create a temporary directory and extract our project into it
    // we need a sure way to get the project name, so we have to extract it first and check the blob
    QTemporaryDir temporaryDir;
    if (temporaryDir.isValid()) {
        zip_extract(filename.toStdString().c_str(),
                    temporaryDir.path().toStdString().c_str(),
                    on_extract_entry,
                    Q_NULLPTR);
    }

    // now extract the project to the default projects directory with the name
    if (!importProjectName.isEmpty() || !importProjectName.isNull()) {
        auto pDir = QDir(defaultProjectDirectory).filePath(importProjectName);

        zip_extract(filename.toStdString().c_str(), pDir.toStdString().c_str(), Q_NULLPTR, Q_NULLPTR);
        QDir dir;
        if (!dir.remove(QDir(pDir).filePath(importProjectName + ".db"))) {
            // let's try again shall we...
            remove(QDir(pDir).filePath(importProjectName + ".db").toStdString().c_str());
        }

        auto open = db->importProject(QDir(temporaryDir.path()).filePath(importProjectName));
        if (open) {
            Globals::project->setProjectPath(pDir);
            loadProjectAssets();
        }
    }

    temporaryDir.remove();
}

void ProjectManager::exportProjectFromWidget(ItemGridWidget *widget)
{
    Globals::project->setProjectGuid(widget->tileData.guid);
    emit exportProject();
}

void ProjectManager::renameProjectFromWidget(ItemGridWidget *widget)
{
    auto spath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
    auto projectFolder = SettingsManager::getDefaultManager()->getValue("default_directory", spath).toString();

    QDir dir;
    auto dirRename = dir.rename(QDir(projectFolder).filePath(widget->tileData.name),
                                QDir(projectFolder).filePath(widget->labelText));
    if (dirRename) {
        widget->updateLabel(widget->labelText);
        Globals::project->setProjectGuid(widget->tileData.guid);
        db->renameProject(widget->labelText);
    }
    else {
        QMessageBox::warning(this,
                             "Rename failed",
                             "Failed to rename project, please try again or rename manually",
                             QMessageBox::Ok);
    }
}

void ProjectManager::deleteProjectFromWidget(ItemGridWidget *widget)
{
    auto spath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
    auto projectFolder = SettingsManager::getDefaultManager()->getValue("default_directory", spath).toString();

    auto option = QMessageBox::question(this,
                                        "Deleting Project",
                                        "Are you sure you want to delete this project?",
                                        QMessageBox::Yes | QMessageBox::Cancel);

    if (option == QMessageBox::Yes) {
        QDir dirToRemove(QDir(projectFolder).filePath(widget->tileData.name));
        if (dirToRemove.removeRecursively()) {
            dynamicGrid->deleteTile(widget);
            Globals::project->setProjectGuid(widget->tileData.guid);
            db->deleteProject();
        } else {
            QMessageBox::warning(this,
                                 "Delete Failed!",
                                 "Failed to remove entire project folder, please try again!",
                                 QMessageBox::Ok);
        }
    }
}

void ProjectManager::searchProjects()
{
    dynamicGrid->searchTiles(ui->lineEdit->text());
}

void ProjectManager::populateDesktop(bool reset)
{
    if (reset) dynamicGrid->resetView();

    int i = 0;
    foreach (const ProjectTileData &record, db->fetchProjects()) {
        dynamicGrid->addToGridView(record, i);
        i++;
    }
}

void ProjectManager::cleanupOnClose()
{
    AssetManager::assets.clear();
}

void ProjectManager::openSampleProject(QListWidgetItem *item)
{
//    auto path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
//                    + Constants::PROJECT_FOLDER;
//    auto projectFolder = settings->getValue("default_directory", path).toString();

//    QDir targetDir(projectFolder);
//    if(!targetDir.exists()){    /* if directory don't exists, build it */
//        targetDir.mkdir(targetDir.absolutePath());
//    }

//    if (QDir(projectFolder + "/" + item->data(Qt::DisplayRole).toString()).exists()) {
//        QMessageBox::StandardButton reply;
//        reply = QMessageBox::question(this, "Project Path not Empty", "Project already Exists! Overwrite?",
//                                        QMessageBox::Yes|QMessageBox::No);
//        if (reply == QMessageBox::Yes) {
//            if (!projectFolder.isEmpty()) {
//                auto projectFile = QFileInfo(item->data(Qt::UserRole).toString());

//                QString dest = QDir(projectFolder).filePath(projectFile.baseName());
//                if (this->copyDirectoryFiles(projectFile.absolutePath(), dest, true)) {

//                    auto newProjectFile = QFileInfo(dest);
//                    auto projectPath = QDir(newProjectFile.absolutePath()).filePath(projectFile.baseName());
//                    Globals::project->setProjectPath(projectPath);

//                    auto sln = QDir(projectPath).filePath(projectFile.fileName());

//                    prepareStore(sln);

////                    settings->addRecentlyOpenedScene(sln);
//                }

////                emit accepted();
////                this->close();

//                sampleDialog.close();
//            }
//        }
//    } else {
//        if (!projectFolder.isEmpty()) {
//            auto projectFile = QFileInfo(item->data(Qt::UserRole).toString());

//            QString dest = QDir(projectFolder).filePath(projectFile.baseName());
//            if (this->copyDirectoryFiles(projectFile.absolutePath(), dest, true)) {

//                auto newProjectFile = QFileInfo(dest);
//                auto projectPath = QDir(newProjectFile.absolutePath()).filePath(projectFile.baseName());
//                Globals::project->setProjectPath(projectPath);

//                auto sln = QDir(projectPath).filePath(projectFile.fileName());

//                prepareStore(sln);

////                settings->addRecentlyOpenedScene(sln);
//            }

////            emit accepted();
////            this->close();
//            sampleDialog.close();
//        }
//    }
}

void ProjectManager::newProject()
{
    NewProjectDialog dialog;

    dialog.exec();

    auto projectName = dialog.getProjectInfo().projectName;
    auto projectPath = dialog.getProjectInfo().projectPath;

    if (!projectName.isEmpty() || !projectName.isNull()) {
        auto fullProjectPath = QDir(projectPath).filePath(projectName);

        Globals::project->setProjectPath(fullProjectPath);

        // make a dir and the default subfolders
        QDir projectDir(fullProjectPath);
        if (!projectDir.exists()) projectDir.mkpath(".");

        for (auto folder : Constants::PROJECT_DIRS) {
            QDir dir(QDir(fullProjectPath).filePath(folder));
            dir.mkpath(".");
        }

        emit fileToCreate(projectName, fullProjectPath);

        this->hide();
    }
}

void ProjectManager::changePreviewSize(QString scale)
{
    dynamicGrid->scaleTile(scale);
}

void ProjectManager::finalizeProjectAssetLoad()
{
    progressDialog->setRange(0, 0);
    progressDialog->setLabelText(QString("Populating scene..."));

    // update the static list of assets with the now loaded assimp data in memory
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

    progressDialog->setLabelText(QString("Initializing panels..."));
}

void ProjectManager::finishedFutureWatcher()
{
    emit fileToOpen(openInPlayMode);
    progressDialog->close();
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

void ProjectManager::loadProjectAssets()
{
    // iterate through the project directory and pick out files we want
    walkProjectFolder(Globals::project->getProjectFolder());

    // the whole point of the function is to concurrently load models when opening a project
    // as the project scope expands and projects get larger, it will be expanded for more assets
    QStringList assetsToLoad;

    // TODO - only load files that are in the scene instead of everything! (iKlsR)
    // of the files we detect, get a separate list of objects so we can load into memory
    for (auto asset : AssetManager::assets) {
        if (asset->type == AssetType::Object) {
            assetsToLoad.append(asset->path);
        }
    }

    progressDialog = QSharedPointer<ProgressDialog>(new ProgressDialog);
    progressDialog->setLabelText("Loading assets...");

    futureWatcher = new QFutureWatcher<QVector<ModelData>>();

    QObject::connect(futureWatcher, SIGNAL(finished()), SLOT(finalizeProjectAssetLoad()));
    QObject::connect(futureWatcher, SIGNAL(finished()), SLOT(finishedFutureWatcher()));
    QObject::connect(futureWatcher, SIGNAL(finished()), futureWatcher, SLOT(deleteLater()));
    QObject::connect(futureWatcher, SIGNAL(progressRangeChanged(int, int)),
                     progressDialog.data(), SLOT(setRange(int, int)));
    QObject::connect(futureWatcher, SIGNAL(progressValueChanged(int)),
                     progressDialog.data(), SLOT(setValue(int)));

    AssetWidgetConcurrentWrapper loadWrapper(this);
    // pass our list of objects to load concurrently
    auto future = QtConcurrent::mappedReduced(assetsToLoad,
                                              loadWrapper,
                                              reducer,
                                              QtConcurrent::SequentialReduce);
    futureWatcher->setFuture(future);

    progressDialog->exec();

    futureWatcher->waitForFinished();
}

void ProjectManager::walkProjectFolder(const QString &projectPath)
{
    QDir dir(projectPath);
    foreach (auto &file, dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs)) {
        if (file.isFile()) {
            AssetType type;
            QPixmap pixmap;

            if (Constants::IMAGE_EXTS.contains(file.suffix())) {
                auto thumb = ThumbnailManager::createThumbnail(file.absoluteFilePath(), 256, 256);
                pixmap = QPixmap::fromImage(*thumb->thumb);
                type = AssetType::Texture;
            }
            else if (Constants::MODEL_EXTS.contains(file.suffix())) {
                auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                type = AssetType::Object;
                pixmap = QPixmap::fromImage(*thumb->thumb);
            }
            else if (file.suffix() == "shader") {
                auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                pixmap = QPixmap::fromImage(*thumb->thumb);
                type = AssetType::Shader;
            }
            else {
                auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                type = AssetType::File;
                pixmap = QPixmap::fromImage(*thumb->thumb);
            }

            auto asset = new AssetVariant;
            asset->type         = type;
            asset->fileName     = file.fileName();
            asset->path         = file.absoluteFilePath();
            asset->thumbnail    = pixmap;

            AssetManager::assets.append(asset);
        }
        else {
            auto thumb = ThumbnailManager::createThumbnail(":/app/icons/folder-symbol.svg", 128, 128);
            auto asset = new AssetFolder;
            asset->fileName     = file.fileName();
            asset->path         = file.absoluteFilePath();
            asset->thumbnail    = QPixmap::fromImage(*thumb->thumb);

            AssetManager::assets.append(asset);
        }

        if (file.isDir()) {
            walkProjectFolder(file.absoluteFilePath());
        }
    }
}

QVector<ModelData> ProjectManager::loadModel(const QString &filePath)
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

ProjectManager::~ProjectManager()
{
    delete ui;
}
