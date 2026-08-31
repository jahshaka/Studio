/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/pages/projectmanager.h"
#include "ui_projectmanager.h"

#include <chrono>
#include <memory>

#include <QtConcurrent/QtConcurrent>
#include <QActionGroup>
#include <QDebug>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGraphicsDropShadowEffect>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QThread>
#include <QTreeWidgetItem>
#include <QStyledItemDelegate>
#include <QOffscreenSurface>

#include "irisgl/thirdparty/assimp/include/assimp/Importer.hpp"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/materials/custommaterial.h"
#include "zip.h"

#include "data/constants.h"
#include "ui/controls/dynamicgrid.h"
#include "ui/controls/itemgridwidget.h"
#include "shell/mainwindow.h"
#include "services/services.h"
#include "services/projectservice.h"

#include "data/database/database.h"
#include "data/guidmanager.h"
#include "services/thumbnailmanager.h"
#include "data/project.h"
#include "data/settingsmanager.h"
#include "ui/dialogs/newprojectdialog.h"
#include "ui/dialogs/progressdialog.h"
#include "io/assetmanager.h"
#include "io/materialreader.h"
#include "ui/dialogs/customdialog.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"

ProjectManager::ProjectManager(Database *handle, Project *project, QWidget *parent)
    : QWidget(parent), ui(new Ui::ProjectManager)
{
    ui->setupUi(this);

    if (!ThemeManager::classicActive()) {
        // The .ui root stylesheet is the classic desktop-page skin (flat #444
        // square buttons, combo/lineedit/QMessageBox rules). Under Qlementine
        // it fights the theme — buttons render square and flat instead of the
        // themed rounded look — so drop it and let the style own the page.
        // The deliberate accents survive below (New Scene keeps the primary
        // blue). Classic keeps the sheet bit-for-bit.
        setStyleSheet(QString());
        ui->newProject->setStyleSheet(ThemeManager::chromeAccentButtonSheet());
        // one footer button spec (owner direction): every grey button matches
        // the blue New Scene geometry exactly — same padding, same rounded
        // corners, grey — regardless of what ancestor sheets interpose. The
        // spec is shared with the editor toolbar (ThemeManager).
        for (QPushButton *btn : { ui->importWorld, ui->downloadWorlds,
                                  ui->tileSizeBtn, ui->desktopSwitcher,
                                  ui->layoutToggle, ui->browseProjects })
            btn->setStyleSheet(ThemeManager::chromeButtonSheet());
    }

    db = handle;
    this->project = project;

#ifdef Q_OS_WIN32
	// setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NativeWindow, true);
#endif

	futureWatcher = QPointer<QFutureWatcher<QVector<ModelData>>>(new QFutureWatcher<QVector<ModelData>>());
	progressDialog = QPointer<ProgressDialog>(new ProgressDialog());

	QObject::connect(futureWatcher, &QFutureWatcher<QVector<ModelData>>::finished, [this]() {
		// [this], never [&]: this connect fires long after the constructor returns,
		// so the ctor parameters are gone - only members are safe to touch here.
		Project *project = this->project;
		progressDialog->setRange(0, 100);
		progressDialog->setLabelText(tr("Caching assets..."));

		// Meshes
		// Note - this would be the perfect place to attach materials as well but we can't access the opengl context
		for (const auto &item : futureWatcher->result()) {
			AssetObject *model = new AssetObject(
				new AssimpObject(item.data, item.path), item.path, QFileInfo(item.path).fileName()
			);
			model->assetGuid = item.guid;
			AssetManager::addAsset(model);
		}

		progressDialog->setValue(40);

        for (const auto &asset : db->fetchAssetsByType(static_cast<int>(ModelTypes::File), project->getProjectGuid())) {
            auto assetFile = new AssetFile;
            assetFile->fileName = asset.name;
            assetFile->assetGuid = asset.guid;
            assetFile->path = IrisUtils::join(project->getProjectFolder(), asset.name);
            AssetManager::addAsset(assetFile);
        }

        //for (const auto &asset : db->fetchAssetsByType(static_cast<int>(ModelTypes::CubeMap), project->getProjectGuid())) {
        //    QJsonDocument mapDefinition = QJsonDocument::fromBinaryData(db->fetchAssetData(asset.guid));
        //    QJsonObject mapObject = mapDefinition.object();

        //    auto assetCubeMap = new AssetCubeMap;
        //    assetCubeMap->fileName = asset.name;
        //    assetCubeMap->assetGuid = asset.guid;
        //    // assetFile->path = IrisUtils::join(project->getProjectFolder(), asset.name);
        //    assetCubeMap->setValue(mapObject);
        //    AssetManager::addAsset(assetCubeMap);
        //}

        for (const auto &asset : db->fetchAssetsByType(static_cast<int>(ModelTypes::Texture), project->getProjectGuid())) {
            auto assetTexture = new AssetTexture;
            assetTexture->fileName = asset.name;
            assetTexture->assetGuid = asset.guid;
            assetTexture->path = IrisUtils::join(project->getProjectFolder(), asset.name);
            AssetManager::addAsset(assetTexture);
        }

		progressDialog->setValue(60);

        for (const auto &asset : db->fetchAssetsByType(static_cast<int>(ModelTypes::Shader), project->getProjectGuid())) {
            QJsonDocument shaderDefinition = QJsonDocument::fromJson(db->fetchAssetData(asset.guid));
            QJsonObject shaderObject = shaderDefinition.object();

            auto assetShader = new AssetShader;
            assetShader->assetGuid = asset.guid;
            assetShader->fileName = QFileInfo(asset.name).baseName();
            assetShader->setValue(QVariant::fromValue(shaderObject));
            AssetManager::addAsset(assetShader);
        }

		progressDialog->setValue(70);

        for (const auto &asset : db->fetchAssetsByType(static_cast<int>(ModelTypes::ParticleSystem), project->getProjectGuid())) {
            QJsonDocument particleDefinition = QJsonDocument::fromJson(db->fetchAssetData(asset.guid));
            QJsonObject particleObject = particleDefinition.object();

            auto assetPS = new AssetParticleSystem;
            assetPS->assetGuid = asset.guid;
            assetPS->fileName = QFileInfo(asset.name).baseName();
            assetPS->setValue(QVariant::fromValue(particleObject));
            AssetManager::addAsset(assetPS);
        }

		progressDialog->setValue(80);

		// Materials
		for (const auto &asset :
			db->fetchFilteredAssets(project->getProjectGuid(), static_cast<int>(ModelTypes::Material)))
		{
            QJsonDocument matDoc = QJsonDocument::fromJson(db->fetchAssetData(asset.guid));
			QJsonObject matObject = matDoc.object();

			MaterialReader reader;
			reader.setProject(project);
			// Typed parse: a saved PBR material hydrates as a PbrMaterial. The
			// untyped parseMaterial forced everything through the shader-guid
			// CustomMaterial path and PBR assets came back broken (grey).
			iris::MaterialPtr material = reader.parseMaterialTyped(matObject, db);

			auto assetMat = new AssetMaterial;
			assetMat->assetGuid = asset.guid;
			assetMat->setValue(QVariant::fromValue(material));
			AssetManager::addAsset(assetMat);
		}

		progressDialog->setLabelText(tr("Opening scene..."));
		progressDialog->setValue(100);
		emit fileToOpen(openInPlayMode);
		progressDialog->close();
	});


	//QObject::connect(futureWatcher, &QFutureWatcher<QVector<ModelData>>::progressRangeChanged,
	//	progressDialog.data(), &ProgressDialog::setRange);
	//QObject::connect(futureWatcher, &QFutureWatcher<QVector<ModelData>>::progressValueChanged,
	//	progressDialog.data(), &ProgressDialog::setValue);

    dynamicGrid = new DynamicGrid(this);

    settings = SettingsManager::getDefaultManager();

    ui->lineEdit->setAttribute(Qt::WA_MacShowFocusRect, false);

    // Tile Size ▾ — same grey popup-button pattern as Desktops/Layouts: static
    // label, the checked entry in the popup is where current state shows.
    tileSizeMenu = new QMenu(this);
    tileSizeMenu->setStyleSheet(StyleSheet::QMenuDarkDesktop());
    auto tileSizeGroup = new QActionGroup(tileSizeMenu);
    tileSizeGroup->setExclusive(true);
    const QString currentTileSize = settings->getValue("tileSize", "Normal").toString();
    for (const QString &sizeName :
         { QStringLiteral("Small"), QStringLiteral("Normal"),
           QStringLiteral("Large"), QStringLiteral("Huge") }) {
        QAction *action = tileSizeMenu->addAction(sizeName);
        action->setCheckable(true);
        action->setChecked(sizeName == currentTileSize);
        tileSizeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, sizeName]() {
            settings->setValue("tileSize", sizeName);
            changePreviewSize(sizeName);
        });
    }
    ui->tileSizeBtn->setCursor(Qt::PointingHandCursor);
    connect(ui->tileSizeBtn, &QPushButton::pressed, this, [this]() {
        const QPoint corner = ui->tileSizeBtn->mapToGlobal(QPoint(0, 0));
        tileSizeMenu->exec(corner - QPoint(0, tileSizeMenu->sizeHint().height()));
    });

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

	connect(ui->downloadWorlds, &QPushButton::pressed, []() {
		QDesktopServices::openUrl(QUrl("https://www.jahshaka.com/get/scenes/"));
	});

    setupDesktopControls();

    populateDesktop();

    QGridLayout *layout = new QGridLayout();
    layout->addWidget(dynamicGrid);
    layout->setContentsMargins(0, 0, 0, 0);

    ui->pmContainer->setStyleSheet(StyleSheet::ProjectManagerCanvas());
    ui->pmContainer->setLayout(layout);
}

ProjectManager::~ProjectManager()
{
	delete ui;
}

void ProjectManager::openProjectFromWidget(ItemGridWidget *widget, bool playMode)
{
	if (project->getProjectGuid() == widget->tileData.guid) {
		    mainWindow->switchSpace(WindowSpaces::EDITOR);

		return;
	}

    // If we're opening a new scene, close the old one first
    if (mainWindow->studioServices()->project->isSceneOpen()) mainWindow->closeProject();

	auto spath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
	auto projectFolder = SettingsManager::getDefaultManager()->getValue("default_directory", spath).toString();

	project->setProjectPath(
        QDir(QDir(projectFolder).filePath("Projects")).filePath(widget->tileData.guid),
        widget->tileData.name
    );
	project->setProjectGuid(widget->tileData.guid);

	this->openInPlayMode = playMode;

    assetGuids.clear();
	loadProjectAssets();
}

QString projectBlobGuid;
int on_extract_entry(const char *filename, void *arg) {
    QFileInfo fInfo(filename);
    if (fInfo.suffix() == "db") projectBlobGuid = fInfo.baseName();
    return 0;
}

void ProjectManager::importProjectFromFile(const QString& file, bool shouldOpen)
{
    QString fileName;
    if (file.isEmpty()) {
        fileName = QFileDialog::getOpenFileName(this,       "Import Scene",
                                                nullptr,    "Jahshaka Project (*.zip)");

        if (fileName.isEmpty() || fileName.isNull()) return;
    } else {
        fileName = file;
    }

    progressDialog->setLabelText("Importing scene....");
    progressDialog->setValue(0);
    progressDialog->show();

    // get the current project working directory
    auto pFldr = IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                 Constants::PROJECT_FOLDER);
    auto defaultProjectDirectory = settings->getValue("default_directory", pFldr).toString();

    // create a temporary directory and extract our project into it
    // we need a sure way to get the project name, so we have to extract it first and check the blob
    QTemporaryDir temporaryDir;
    //temporaryDir.setAutoRemove(false);
    if (temporaryDir.isValid()) {
        zip_extract(fileName.toStdString().c_str(),
                    temporaryDir.path().toStdString().c_str(),
                    on_extract_entry,
                    Q_NULLPTR);
    }

    progressDialog->setValue(20);

    // iterate
    QDirIterator projectDirIterator(temporaryDir.path(), QDir::Files | QDir::Hidden);
    QStringList fileNames;
    while (projectDirIterator.hasNext()) fileNames << projectDirIterator.next();

    bool allowLoading = false;
    for (const auto &name : fileNames) {
        QFileInfo info(name);
        if (info.fileName() == ".manifest") {
            allowLoading = true;
            break;
        }
    }

    if (!db->checkIfProjectVersionSupported(QDir(temporaryDir.path()).filePath(projectBlobGuid)+".db")) {
        allowLoading = false;
    }

    if (!allowLoading) {
        QMessageBox::warning(
            this,
            "Incompatible Scene format",
            "This Scene was made with a deprecated version of Jahshaka\n"
            "You can extract the contents manually and recreate the scene.",
            QMessageBox::Ok
        );

        progressDialog->close();
        return;
    }

    // now extract the project to the default projects directory with the name
    auto importGuid = GUIDManager::generateGUID();
    auto pDir = QDir(QDir(defaultProjectDirectory).filePath("Projects")).filePath(importGuid);
    zip_extract(fileName.toStdString().c_str(), pDir.toStdString().c_str(), Q_NULLPTR, Q_NULLPTR);

    progressDialog->setValue(40);

    QDir dir;
    if (!dir.remove(QDir(pDir).filePath(projectBlobGuid + ".db"))) {
        // let's try again shall we...
        remove(QDir(pDir).filePath(projectBlobGuid + ".db").toStdString().c_str());
    }

    progressDialog->setValue(80);

    QString worldName;
    auto canOpen = db->importProject(
        QDir(temporaryDir.path()).filePath(projectBlobGuid),
        importGuid,
        worldName,
        assetGuids
    );

    // imported projects land on the desktop the user is looking at
    db->updateProjectDesktop(importGuid, currentDesktop);

    // Update files that reference guids

    if (shouldOpen) {
        project->setProjectPath(pDir, worldName);
        project->setProjectGuid(importGuid);
        loadProjectAssets();
	}
	else {
		// This is in the else since any other time the user would get redirected
		// and this function of similar would get delegated...
		addImportedTileToDesktop(importGuid);
	}

    progressDialog->setValue(100);
    temporaryDir.remove();
    progressDialog->close();
}

void ProjectManager::exportProjectFromWidget(ItemGridWidget *widget)
{
    auto spath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
    auto projectFolder = SettingsManager::getDefaultManager()->getValue("default_directory", spath).toString();

    project->setProjectPath(
        QDir(QDir(projectFolder).filePath("Projects")).filePath(widget->tileData.guid),
        widget->tileData.name
    );
    project->setProjectGuid(widget->tileData.guid);

    emit exportProject();
}

void ProjectManager::renameProjectFromWidget(ItemGridWidget *widget)
{
    if (db->renameProject(widget->tileData.guid, widget->labelText)) {
        widget->updateLabel(widget->labelText);
    }
    else {
        QMessageBox::warning(this,
                             "Rename failed",
                             "Failed to rename project, please try again!",
                             QMessageBox::Ok);
    }
}

void ProjectManager::closeProjectFromWidget(ItemGridWidget *widget)
{
    Q_UNUSED(widget);
    emit closeProject();
    project->setProjectGuid(QString());
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
        QDir dirToRemove(QDir(projectFolder + "/Projects").filePath(widget->tileData.guid));
        if (dirToRemove.removeRecursively()) {
            dynamicGrid->deleteTile(widget);
            project->setProjectGuid(widget->tileData.guid);
            db->deleteProject(project->getProjectGuid());

			// Delete folder and contents
			for (const auto &files : db->deleteFolderAndDependencies(project->getProjectGuid())) {
				auto file = QFileInfo(QDir(project->getProjectFolder()).filePath(files));
				if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
			}

			// Delete asset and dependencies
			for (const auto &files : db->deleteAssetAndDependencies(project->getProjectGuid())) {
				auto file = QFileInfo(QDir(project->getProjectFolder()).filePath(files));
				if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
			}

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

// ===== Desktops (DESKTOPS_SPEC.md): switcher, per-desktop layout mode, move-to =====

QString ProjectManager::desktopLayoutKey(int desktop)
{
    return QString("desktop_%1_layout").arg(desktop);
}

void ProjectManager::setupDesktopControls()
{
    currentDesktop = qBound(1, settings->getValue("current_desktop", 1).toInt(), 4);

    const QString menuStyle = StyleSheet::QMenuDarkDesktop();

    // switcher popup: Desktop 1..4, current one checked
    desktopMenu = new QMenu(this);
    desktopMenu->setStyleSheet(menuStyle);
    auto desktopGroup = new QActionGroup(desktopMenu);
    desktopGroup->setExclusive(true);
    for (int i = 1; i <= 4; ++i) {
        QAction *action = desktopMenu->addAction(QString("Desktop %1").arg(i));
        action->setCheckable(true);
        action->setChecked(i == currentDesktop);
        desktopGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, i]() { switchDesktop(i); });
        desktopActions.push_back(action);
    }

    // static "Desktops ▾" label; the checked popup entry shows the current one
    ui->desktopSwitcher->setCursor(Qt::PointingHandCursor);
    connect(ui->desktopSwitcher, &QPushButton::pressed, this, [this]() {
        // the footer sits at the bottom of the window; pop the menu up, not down
        const QPoint corner = ui->desktopSwitcher->mapToGlobal(QPoint(0, 0));
        desktopMenu->exec(corner - QPoint(0, desktopMenu->sizeHint().height()));
    });

    // per-desktop layout mode: Rows (sequential grid), Freeform (drag anywhere),
    // Sliders (N filmstrip rows — DESKTOP_SLIDER_SPEC.md)
    layoutMenu = new QMenu(this);
    layoutMenu->setStyleSheet(menuStyle);
    auto layoutGroup = new QActionGroup(layoutMenu);
    layoutGroup->setExclusive(true);
    rowsAction = layoutMenu->addAction("Rows");
    rowsAction->setCheckable(true);
    freeformAction = layoutMenu->addAction("Freeform");
    freeformAction->setCheckable(true);
    slidersAction = layoutMenu->addAction("Sliders");
    slidersAction->setCheckable(true);
    layoutGroup->addAction(rowsAction);
    layoutGroup->addAction(freeformAction);
    layoutGroup->addAction(slidersAction);
    connect(rowsAction, &QAction::triggered, this, [this]() { applyDesktopLayoutMode("rows", true); });
    connect(freeformAction, &QAction::triggered, this, [this]() { applyDesktopLayoutMode("freeform", true); });
    connect(slidersAction, &QAction::triggered, this, [this]() { applyDesktopLayoutMode("sliders", true); });

    ui->layoutToggle->setCursor(Qt::PointingHandCursor);
    connect(ui->layoutToggle, &QPushButton::pressed, this, [this]() {
        const QPoint corner = ui->layoutToggle->mapToGlobal(QPoint(0, 0));
        layoutMenu->exec(corner - QPoint(0, layoutMenu->sizeHint().height()));
    });

    // freeform drags (and first-show cascade placements) persist to the library DB
    connect(dynamicGrid, &DynamicGrid::tilePositionChanged,
            this, &ProjectManager::projectTilePositionChanged);

    // slider drops / move-to-row / first-show seeding persist the same way
    connect(dynamicGrid, &DynamicGrid::tileSliderPositionChanged,
            this, &ProjectManager::projectTileSliderChanged);

    dynamicGrid->setCurrentDesktop(currentDesktop);
    applyDesktopLayoutMode(
        settings->getValue(desktopLayoutKey(currentDesktop), "rows").toString(), false);
}

QString ProjectManager::normalizedLayoutMode(const QString &name)
{
    const QString mode = name.trimmed().toLower();
    if (mode == "freeform" || mode == "sliders") return mode;
    return QStringLiteral("rows");
}

void ProjectManager::applyDesktopLayoutMode(const QString &modeName, bool persist)
{
    const QString modeStr = normalizedLayoutMode(modeName);
    currentLayoutMode = modeStr;

    if (persist) settings->setValue(desktopLayoutKey(currentDesktop), modeStr);

    rowsAction->setChecked(modeStr == "rows");
    freeformAction->setChecked(modeStr == "freeform");
    slidersAction->setChecked(modeStr == "sliders");

    // static "Layouts ▾" label; the checked popup entry shows the current mode
    DynamicGrid::LayoutMode gridMode = DynamicGrid::LayoutMode::Rows;
    if (modeStr == "freeform") gridMode = DynamicGrid::LayoutMode::Freeform;
    else if (modeStr == "sliders") gridMode = DynamicGrid::LayoutMode::Sliders;

    dynamicGrid->setLayoutMode(gridMode);
}

bool ProjectManager::setDesktopViewMode(const QString &name)
{
    const QString mode = name.trimmed().toLower();
    if (mode != "rows" && mode != "freeform" && mode != "sliders") return false;
    applyDesktopLayoutMode(mode, true);
    return true;
}

bool ProjectManager::moveTileToSliderPos(const QString &guid, int row, int index)
{
    if (currentLayoutMode != "sliders") return false;

    foreach (ItemGridWidget *widget, dynamicGrid->originalItems) {
        if (widget->tileData.guid == guid) {
            dynamicGrid->moveTileToRow(widget, row, index);
            return true;
        }
    }
    return false;
}

QVariantList ProjectManager::sliderTilesForApi() const
{
    QVariantList tiles;
    foreach (ItemGridWidget *widget, dynamicGrid->originalItems) {
        QVariantMap tile;
        tile["guid"]  = widget->tileData.guid;
        tile["name"]  = widget->tileData.name;
        tile["row"]   = widget->hasSliderPos ? widget->sliderRow + 1 : -1;  // API rows are 1-based
        tile["index"] = widget->hasSliderPos ? widget->sliderIndex : -1;
        tiles.push_back(tile);
    }
    return tiles;
}

void ProjectManager::switchDesktop(int desktop)
{
    desktop = qBound(1, desktop, 4);
    if (desktop == currentDesktop) return;

    currentDesktop = desktop;
    settings->setValue("current_desktop", desktop);

    for (int i = 0; i < desktopActions.size(); ++i)
        desktopActions[i]->setChecked(i + 1 == desktop);

    dynamicGrid->setCurrentDesktop(desktop);

    // this desktop's layout mode FIRST (so tiles populate under the right mode —
    // a rows desktop must not cascade-assign freeform positions), then its projects
    applyDesktopLayoutMode(
        settings->getValue(desktopLayoutKey(desktop), "rows").toString(), false);
    populateDesktop(true);
}

void ProjectManager::moveProjectToDesktop(ItemGridWidget *widget, int desktop)
{
    desktop = qBound(1, desktop, 4);
    if (desktop == currentDesktop) return;

    if (db->updateProjectDesktop(widget->tileData.guid, desktop)) {
        dynamicGrid->deleteTile(widget);
        checkForEmptyState();
    } else {
        QMessageBox::warning(this,
                             "Move failed",
                             "Failed to move project, please try again!",
                             QMessageBox::Ok);
    }
}

void ProjectManager::projectTilePositionChanged(ItemGridWidget *widget)
{
    db->updateProjectPosition(widget->tileData.guid,
                              static_cast<float>(widget->normX),
                              static_cast<float>(widget->normY));
}

void ProjectManager::projectTileSliderChanged(ItemGridWidget *widget)
{
    db->updateProjectSliderPos(widget->tileData.guid,
                               widget->sliderRow,
                               widget->sliderIndex);
}

void ProjectManager::addImportedTileToDesktop(const QString &guid)
{
	int i = 0;
	ProjectTileData importedScene;
	foreach(const ProjectTileData &record, db->fetchProjects(currentDesktop)) {
		if (record.guid == guid) importedScene = record;
		i++;
	}

	dynamicGrid->addToGridView(importedScene, i - 1, isOpenProjectTile(importedScene.guid));

	checkForEmptyState();

    update();
}

void ProjectManager::populateDesktop(bool reset)
{
    if (reset) dynamicGrid->resetView();

    int i = 0;
    foreach (const ProjectTileData &record, db->fetchProjects(currentDesktop)) {
        dynamicGrid->addToGridView(record, i, isOpenProjectTile(record.guid));
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
    AssetManager::getAssets().clear();
}

void ProjectManager::openSampleProject(QListWidgetItem *item)
{
    sampleDialog.close();
    importProjectFromFile(item->data(Qt::UserRole).toString(), true);
}

void ProjectManager::newProject()
{
	NewProjectDialog dialog;
	dialog.exec();

	auto projectName = dialog.getProjectInfo().projectName;
	auto projectPath = dialog.getProjectInfo().projectPath;
	auto projectGuid = GUIDManager::generateGUID();

	if (!projectName.isEmpty() || !projectName.isNull()) {
		auto fullProjectPath = QDir(QDir(projectPath).filePath("Projects")).filePath(projectGuid);

		project->setProjectPath(fullProjectPath, projectName);
		project->setProjectGuid(projectGuid);

		// make a dir and the default subfolders
		QDir projectDir(fullProjectPath);
		if (!projectDir.exists()) projectDir.mkpath(".");

		// Insert an empty scene to get access to the project guid...
		if (!db->createProject(projectGuid, projectName)) return;

		// new projects belong to the desktop they were created on
		db->updateProjectDesktop(projectGuid, currentDesktop);

		emit fileToCreate(projectName, fullProjectPath);

		this->hide();
	}
}

void ProjectManager::changePreviewSize(QString scale)
{
    dynamicGrid->scaleTile(scale);
}

ModelData ProjectManager::loadAiSceneFromModel(const QPair<QString, QString> asset)
{
	//QFile file(asset.first);
	//file.open(QFile::ReadOnly);
	//auto data = file.readAll();

	Assimp::Importer *importer = new Assimp::Importer;
	 //const aiScene *scene = importer->ReadFile(asset.first.toStdString().c_str(), aiProcessPreset_TargetRealtime_Fast);
	//const aiScene *scene = sceneSource->importer.ReadFileFromMemory((void*)data.data(),
	//																data.length(),
	//																aiProcessPreset_TargetRealtime_Fast);
	ModelData d = { asset.first, asset.second, importer->ReadFile(asset.first.toStdString().c_str(), aiProcessPreset_TargetRealtime_Fast) };
	return d;
}

void ProjectManager::finalizeProjectAssetLoad()
{
	
}

void ProjectManager::finishedFutureWatcher()
{
    emit fileToOpen(settings->getValue("open_in_player", QVariant::fromValue(false)).toBool());
    progressDialog->close();
}

void ProjectManager::openSampleBrowser()
{
    // 3 columns x 2 rows of uniformly sized tiles: every preview center-cropped
    // to the same 16:9 thumb (the physics preview's shape, at the smaller
    // display scale), names on a black bar in white like the desktop tiles.
    const QSize sampleIconSize(192, 108);
    const QSize sampleGridSize(sampleIconSize.width() + 6,
                               sampleIconSize.height() + 30);

    sampleDialog.setWindowFlags(sampleDialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    sampleDialog.setWindowTitle("Sample Scenes");
    sampleDialog.setAttribute(Qt::WA_MacShowFocusRect, false);

    QGridLayout *layout = new QGridLayout();
    QListWidget *sampleList = new QListWidget();
    sampleList->setAttribute(Qt::WA_MacShowFocusRect, false);
    sampleList->setObjectName("sampleList");
    sampleList->setStyleSheet(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { background: black; color: white; }"
        "QListWidget::item:selected { background: #3498db; color: white; }");
    sampleList->setViewMode(QListWidget::IconMode);
    sampleList->setSpacing(4);
    sampleList->setResizeMode(QListWidget::Adjust);
    sampleList->setMovement(QListView::Static);
    sampleList->setIconSize(sampleIconSize);
    sampleList->setGridSize(sampleGridSize);
    sampleList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sampleList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sampleList->setSelectionMode(QAbstractItemView::SingleSelection);

    QMap<QString, QString> samples;
    samples.insert("preview/matcaps.png",   "Matcaps");
    samples.insert("preview/particles.png", "Particles");
    samples.insert("preview/skeletal.png",  "Skeletal Animation");
    samples.insert("preview/world.png",     "World Background");
    samples.insert("preview/physics.png",   "Physics");

    QDir dir(IrisUtils::getAbsoluteAssetPath(Constants::SAMPLES_FOLDER));

    // center-crop, never squash: scale so the thumb is fully covered, then
    // cut the overhang symmetrically
    const auto croppedThumb = [&sampleIconSize](const QString &path) {
        const QPixmap src(path);
        if (src.isNull()) return src;
        QPixmap scaled = src.scaled(sampleIconSize, Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
        const QRect cropRect(QPoint((scaled.width() - sampleIconSize.width()) / 2,
                                    (scaled.height() - sampleIconSize.height()) / 2),
                             sampleIconSize);
        return scaled.copy(cropRect);
    };

    QMap<QString, QString>::const_iterator it;
    for (it = samples.begin(); it != samples.end(); ++it){
        auto item = new QListWidgetItem();
        item->setData(Qt::DisplayRole, it.value());
        item->setData(Qt::UserRole, QDir(dir.absolutePath()).filePath(it.value()) + ".zip");
        item->setIcon(QIcon(croppedThumb(QDir(dir.absolutePath()).filePath(it.key()))));
        item->setSizeHint(sampleGridSize - QSize(4, 4));
        sampleList->addItem(item);
    }

    // fixed size that fits the 3x2 grid cleanly (title + grid + button row)
    const int sampleColumns = 3, sampleRows = 2;
    sampleList->setFixedSize(sampleColumns * sampleGridSize.width() + 16,
                             sampleRows * sampleGridSize.height() + 12);

    auto instructions = new QLabel("Double click on a sample scene to import it in the editor");
    instructions->setObjectName("instructions");
    instructions->setAlignment(Qt::AlignHCenter);
    instructions->setStyleSheet(StyleSheet::ProjectManagerInstructions());

	auto cancel = new QPushButton("Cancel");
	auto select = new QPushButton("Open");
	select->setDisabled(true);
	auto wid = new QWidget;
	auto layout1 = new QHBoxLayout;
	wid->setLayout(layout1);
	layout1->addStretch();
	layout1->addWidget(cancel);
	layout1->addWidget(select);
	// owner: no background band behind the button row; Open blue, Cancel grey.
	// Classic keeps its big-button sheets; Qlementine uses the shared chrome spec.
	if (ThemeManager::classicActive()) {
		cancel->setStyleSheet(StyleSheet::QPushButtonGreyscaleBig());
		select->setStyleSheet(StyleSheet::QPushButtonBlueBig());
	} else {
		cancel->setStyleSheet(ThemeManager::chromeButtonSheet());
		select->setStyleSheet(ThemeManager::chromeAccentButtonSheet());
	}
	layout1->setContentsMargins(0, 10, 0, 10);

	connect(sampleList, SIGNAL(itemDoubleClicked(QListWidgetItem*)), SLOT(openSampleProject(QListWidgetItem*)));
	connect(sampleList, &QListWidget::itemClicked, [=](QListWidgetItem *item) {
		select->setDisabled(false);
		});


	connect(cancel, &QPushButton::clicked, [=]() {
		sampleDialog.close();
		});
	connect(select, &QPushButton::clicked, [=]() {
		openSampleProject(sampleList->currentItem());
	});


    layout->addWidget(instructions);
    layout->addWidget(sampleList, 1, 0, Qt::AlignHCenter);
    layout->addWidget(wid);
    // a blank line's worth of air above the (centered) title; the button row
    // supplies its own padding at the bottom
    layout->setContentsMargins(12, 20, 12, 0);
    layout->setSpacing(8);

    sampleDialog.setLayout(layout);
    sampleDialog.setFixedSize(sampleDialog.sizeHint());
    sampleDialog.exec();
}

void ProjectManager::loadProjectAssets()
{
	// This shouldn't be needed but just in case a scene doesn't get cleaned up due 
	// to some future change this will prevent any subtle bugs regarding invalid data
	AssetManager::clearAssetList();

    // The whole point of the function is to concurrently load models when opening a project
    // As the project scope expands and projects get larger, it will be expanded for more (large) assets
    QVector<AssetList> assetsToLoad;

	progressDialog->setLabelText(tr("Collecting assets..."));

	// TODO - if we are only loading a couple assets, just do it sequentially
	for (const auto &asset : db->fetchFilteredAssets(project->getProjectGuid(), static_cast<int>(ModelTypes::Mesh))) {
		assetsToLoad.append(
			AssetList(QDir(project->getProjectFolder()).filePath(asset.name),
			db->fetchMeshObject(asset.guid, static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh)))
		);
	}

    progressDialog->setLabelText(tr("Loading assets..."));

    AssetWidgetConcurrentWrapper aiSceneFromModelMapper(this);
    auto aiSceneFromModelReducer = [](QVector<ModelData> &accum, const ModelData &interm) {
		accum.append(interm);
	};
    auto future = QtConcurrent::mappedReduced<QVector<ModelData>>(assetsToLoad.constBegin(),
																  assetsToLoad.constEnd(),
																  aiSceneFromModelMapper,
																  aiSceneFromModelReducer,
																  QtConcurrent::OrderedReduce);
    futureWatcher->setFuture(future);
    progressDialog->exec();
    futureWatcher->waitForFinished();
}

void ProjectManager::loadProjectAssetsSync()
{
	// Mirrors loadProjectAssets() + its futureWatcher-finished lambda exactly —
	// same DB sweeps, same AssetManager registrations, same order — minus the
	// QtConcurrent map, the modal progress dialog and the fileToOpen signal.
	AssetManager::clearAssetList();


	// Meshes
	for (const auto &asset : db->fetchFilteredAssets(project->getProjectGuid(), static_cast<int>(ModelTypes::Mesh))) {
		auto item = loadAiSceneFromModel(
			AssetList(QDir(project->getProjectFolder()).filePath(asset.name),
			db->fetchMeshObject(asset.guid, static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh))));
		AssetObject *model = new AssetObject(
			new AssimpObject(item.data, item.path), item.path, QFileInfo(item.path).fileName()
		);
		model->assetGuid = item.guid;
		AssetManager::addAsset(model);
	}

	for (const auto &asset : db->fetchAssetsByType(static_cast<int>(ModelTypes::File), project->getProjectGuid())) {
		auto assetFile = new AssetFile;
		assetFile->fileName = asset.name;
		assetFile->assetGuid = asset.guid;
		assetFile->path = IrisUtils::join(project->getProjectFolder(), asset.name);
		AssetManager::addAsset(assetFile);
	}

	for (const auto &asset : db->fetchAssetsByType(static_cast<int>(ModelTypes::Texture), project->getProjectGuid())) {
		auto assetTexture = new AssetTexture;
		assetTexture->fileName = asset.name;
		assetTexture->assetGuid = asset.guid;
		assetTexture->path = IrisUtils::join(project->getProjectFolder(), asset.name);
		AssetManager::addAsset(assetTexture);
	}

	for (const auto &asset : db->fetchAssetsByType(static_cast<int>(ModelTypes::Shader), project->getProjectGuid())) {
		QJsonDocument shaderDefinition = QJsonDocument::fromJson(db->fetchAssetData(asset.guid));
		QJsonObject shaderObject = shaderDefinition.object();

		auto assetShader = new AssetShader;
		assetShader->assetGuid = asset.guid;
		assetShader->fileName = QFileInfo(asset.name).baseName();
		assetShader->setValue(QVariant::fromValue(shaderObject));
		AssetManager::addAsset(assetShader);
	}

	for (const auto &asset : db->fetchAssetsByType(static_cast<int>(ModelTypes::ParticleSystem), project->getProjectGuid())) {
		QJsonDocument particleDefinition = QJsonDocument::fromJson(db->fetchAssetData(asset.guid));
		QJsonObject particleObject = particleDefinition.object();

		auto assetPS = new AssetParticleSystem;
		assetPS->assetGuid = asset.guid;
		assetPS->fileName = QFileInfo(asset.name).baseName();
		assetPS->setValue(QVariant::fromValue(particleObject));
		AssetManager::addAsset(assetPS);
	}

	// Materials
	for (const auto &asset :
		db->fetchFilteredAssets(project->getProjectGuid(), static_cast<int>(ModelTypes::Material)))
	{
		QJsonDocument matDoc = QJsonDocument::fromJson(db->fetchAssetData(asset.guid));
		QJsonObject matObject = matDoc.object();

		MaterialReader reader;
		reader.setProject(project);
		// Typed parse — see loadProjectAssets() above.
		iris::MaterialPtr material = reader.parseMaterialTyped(matObject, db);

		auto assetMat = new AssetMaterial;
		assetMat->assetGuid = asset.guid;
		assetMat->setValue(QVariant::fromValue(material));
		AssetManager::addAsset(assetMat);
	}
}

void ProjectManager::updateTile(const QString &id, const QByteArray & arr)
{
	dynamicGrid->updateTile(id, arr);
}

bool ProjectManager::isOpenProjectTile(const QString &guid) const
{
    return mainWindow && mainWindow->studioServices() && mainWindow->studioServices()->project
        && mainWindow->studioServices()->project->isSceneOpen()
        && guid == project->getProjectGuid();
}
