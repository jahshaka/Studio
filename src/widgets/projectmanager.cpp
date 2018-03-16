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

#include "irisgl/src/zip/zip.h"
#include "irisgl/src/assimp/include/assimp/Importer.hpp"

#include "dynamicgrid.h"

#include <QDebug>
#include <QDesktopServices>
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

#ifdef Q_OS_WIN32
//    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NativeWindow, true);
#endif

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

	connect(ui->downloadWorlds, &QPushButton::pressed, []() {
		QDesktopServices::openUrl(QUrl("http://www.jahfx.com/downloads/worlds/"));
	});

    populateDesktop();

    QGridLayout *layout = new QGridLayout();
    layout->addWidget(dynamicGrid);
    layout->setMargin(0);

    ui->pmContainer->setStyleSheet(
		"border: none;"
		"background-image: url(:/images/empty_canvas.png);"
		"background-attachment: fixed;"
		"background-position: center;"
		"background-origin: content;"
		"background-repeat: no-repeat;"
	);
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

void ProjectManager::importProjectFromFile(const QString& file)
{
    QString fileName;
    if (file.isEmpty()) {
        fileName = QFileDialog::getOpenFileName(this,       "Import World",
                                                nullptr,    "Jahshaka Project (*.zip)");

        if (fileName.isEmpty() || fileName.isNull()) return;
    } else {
        fileName = file;
    }

    // get the current project working directory
    auto pFldr = IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                 Constants::PROJECT_FOLDER);
    auto defaultProjectDirectory = settings->getValue("default_directory", pFldr).toString();

    // create a temporary directory and extract our project into it
    // we need a sure way to get the project name, so we have to extract it first and check the blob
    QTemporaryDir temporaryDir;
    if (temporaryDir.isValid()) {
        zip_extract(fileName.toStdString().c_str(),
                    temporaryDir.path().toStdString().c_str(),
                    on_extract_entry,
                    Q_NULLPTR);
    }

    // now extract the project to the default projects directory with the name
    if (!importProjectName.isEmpty() || !importProjectName.isNull()) {
        auto pDir = QDir(defaultProjectDirectory).filePath(importProjectName);

        zip_extract(fileName.toStdString().c_str(), pDir.toStdString().c_str(), Q_NULLPTR, Q_NULLPTR);
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
    auto spath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
    auto projectFolder = SettingsManager::getDefaultManager()->getValue("default_directory", spath).toString();

    Globals::project->setProjectPath(QDir(projectFolder).filePath(widget->tileData.name));
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

void ProjectManager::closeProjectFromWidget(ItemGridWidget *widget)
{
    Q_UNUSED(widget);
    emit closeProject();
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
            checkForEmptyState();
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

    checkForEmptyState();
}

bool ProjectManager::checkForEmptyState()
{
    if (dynamicGrid->containsTiles()) {
        ui->stackedWidget->setCurrentIndex(0);
        return false;
    }

    ui->stackedWidget->setCurrentIndex(1);
    return true;
}

void ProjectManager::cleanupOnClose()
{
    AssetManager::assets.clear();
}

void ProjectManager::openSampleProject(QListWidgetItem *item)
{
    sampleDialog.close();
    importProjectFromFile(item->data(Qt::UserRole).toString());
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

		QJsonObject assetProperty;

		// Insert an empty scene to get access to the project guid... 
		db->insertSceneGlobal(projectName, QByteArray(), QByteArray());

		for (auto folder : Constants::PROJECT_DIRS) {
			QDir dir(QDir(fullProjectPath).filePath(folder));
			dir.mkpath(".");
		}

		//const QString folderGuid = db->insertFolder("Assets", Globals::project->getProjectGuid());
        
		// if we reached this far, the project dir has already been created
		// we can copy some default assets to each project here
		QFile::copy(IrisUtils::getAbsoluteAssetPath("app/content/textures/tile.png"),
					QDir(Globals::project->getProjectFolder()).filePath("Textures/Tile.png"));

		auto thumb = ThumbnailManager::createThumbnail(
						IrisUtils::getAbsoluteAssetPath("app/content/textures/tile.png"), 72, 72);

		QByteArray bytes;
		QBuffer buffer(&bytes);
		buffer.open(QIODevice::WriteOnly);
		QPixmap::fromImage(*thumb->thumb).save(&buffer, "PNG");

		db->insertAssetGlobal("Tile.png", static_cast<int>(AssetType::Texture),
							  Globals::project->getProjectGuid(), bytes);
		
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

     //update the static list of assets with the now loaded assimp data in memory
    for (auto item : futureWatcher->result()) {
        AssimpObject *ao = new AssimpObject(item.data, item.path);
        AssetObject *model = new AssetObject(ao, item.path, QFileInfo(item.path).fileName());
		model->assetGuid = item.guid;
		AssetManager::addAsset(model);
	}

    progressDialog->setLabelText(QString("Initializing panels..."));
}

void ProjectManager::finishedFutureWatcher()
{
    emit fileToOpen(settings->getValue("open_in_player", QVariant::fromValue(false)).toBool());
    progressDialog->close();
}

void ProjectManager::openSampleBrowser()
{
    sampleDialog.setFixedSize(Constants::TILE_SIZE * 1.66);
    sampleDialog.setWindowFlags(sampleDialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    sampleDialog.setWindowTitle("Sample Worlds");

    QGridLayout *layout = new QGridLayout();
    QListWidget *sampleList = new QListWidget();
    sampleList->setObjectName("sampleList");
    sampleList->setStyleSheet("#sampleList { background-color: #1e1e1e; padding: 0 8px; border: none } " \
                              "QListWidgetItem { padding: 12px; } "\
                              "QListView::item:selected { "\
                              "    border: 1px solid #3498db; "\
                               " background: #3498db; "\
                               "  color: #CECECE; "\
                              "} "\
                              "QToolTip { padding: 2px; border: 0; background: black; opacity: 200; }");
    sampleList->setViewMode(QListWidget::IconMode);
    sampleList->setSizeAdjustPolicy(QListWidget::AdjustToContents);
    sampleList->setSpacing(4);
    sampleList->setResizeMode(QListWidget::Adjust);
    sampleList->setMovement(QListView::Static);
    sampleList->setIconSize(Constants::TILE_SIZE * 0.5);
    sampleList->setSelectionMode(QAbstractItemView::SingleSelection);

    QMap<QString, QString> samples;
    samples.insert("preview/matcaps.png",   "Matcaps");
    samples.insert("preview/particles.png", "Particles");
    samples.insert("preview/skeletal.png",  "Skeletal Animation");
    samples.insert("preview/world.png",     "World Background");

    QDir dir(IrisUtils::getAbsoluteAssetPath(Constants::SAMPLES_FOLDER));

    QMap<QString, QString>::const_iterator it;
    for (it = samples.begin(); it != samples.end(); ++it){
        auto item = new QListWidgetItem();
        item->setData(Qt::DisplayRole, it.value());
        item->setData(Qt::UserRole, QDir(dir.absolutePath()).filePath(it.value()) + ".zip");
        item->setIcon(QIcon(QDir(dir.absolutePath()).filePath(it.key())));
        sampleList->addItem(item);
    }

    connect(sampleList, SIGNAL(itemDoubleClicked(QListWidgetItem*)), SLOT(openSampleProject(QListWidgetItem*)));

    auto instructions = new QLabel("Double click on a sample world to import it in the editor");
    instructions->setObjectName("instructions");
    instructions->setStyleSheet("#instructions { border: none; background: #1e1e1e; " \
                                "padding: 10px; font-size: 12px }");
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
    // walkProjectFolder(Globals::project->getProjectFolder());

    // the whole point of the function is to concurrently load models when opening a project
    // as the project scope expands and projects get larger, it will be expanded for more assets
    QVector<QPair<QString, QString>> assetsToLoad;

	for (const auto &asset : db->fetchFilteredAssets(Globals::project->getProjectGuid(), (int)AssetType::Mesh)) {
		//QString meshGuid = db->fetchObjectMesh(asset.guid, (int)AssetType::Object);
		//qDebug() << asset.guid << meshGuid;
		assetsToLoad.append(QPair<QString, QString>(QDir(Globals::project->getProjectFolder() + "/Models").filePath(asset.name),
							db->fetchMeshObject(asset.guid, (int) AssetType::Object)));
	}

	//for (auto i : assetsToLoad) qDebug() << i.first << i.second;

    // TODO - only load files that are in the scene instead of everything! (iKlsR)
    // of the files we detect, get a separate list of objects so we can load into memory
    //for (auto asset : AssetManager::assets) {
    //    if (asset->type == AssetType::Object) {
    //        assetsToLoad.append(asset->path);
    //    }
    //}

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

ProjectManager::~ProjectManager()
{
    delete ui;
}

void ProjectManager::updateTile(const QString &id, const QByteArray & arr)
{
	dynamicGrid->updateTile(id, arr);
}
