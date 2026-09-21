/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "shaderassetwidget.h"
#include "data/project.h"
#include <QSqlDatabase>
#include "services/assetcas.h"
#include "services/assetdelete.h"
#include "services/assettray.h"
#include "services/projectassets.h"
#include "services/assetstorepaths.h"
#include <QMenu>
#include <QEvent>
#include <QMouseEvent>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDebug>


#include "irisgl/core/irisutils.h"
#include "../core/materialhelper.h"
#include "../models/properties.h"
#include <QStandardPaths>
#include <QDirIterator>

#include "../effectspage.h"
#if(EFFECT_BUILD_AS_LIB)
#include "io/assetmanager.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "ui/style/stylesheet.h"
#endif

ShaderAssetWidget::ShaderAssetWidget(Database *handle) : QWidget()
{

	layout = new QVBoxLayout;
	setLayout(layout);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	breadCrumbs = new QHBoxLayout;
	auto breadCrumbsWidget = new QWidget;
	breadCrumbsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	breadCrumbsWidget->setLayout(breadCrumbs);

	assetViewWidget = new ShaderListWidget;
    assetViewWidget->shaderContextMenuAllowed = true;
	assetViewWidget->setGridSize({ 95,95 });
	connect(assetViewWidget, &ShaderListWidget::itemDropped, [=](QListWidgetItem *item) {
		addDroppedToProject(item);
	});
	connect(assetViewWidget, &ShaderListWidget::itemDoubleClicked, [=](QListWidgetItem *item) {
		emit loadToGraph(item);
	});

    if(handle) setUpDatabase(handle);

	stackWidget = new QStackedWidget;

	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(breadCrumbsWidget);
	layout->addWidget(stackWidget);

	noWidget = new QWidget;
	auto noWidgetLayout = new QVBoxLayout;
	auto lableIcon = new QLabel;
	auto lableText = new QLabel;
	noWidget->setLayout(noWidgetLayout);
	noWidgetLayout->addWidget(lableIcon);
	noWidgetLayout->addWidget(lableText);
	lableText->setText("No Open projects");
	lableIcon->setPixmap(QPixmap(":/icons/icons8-empty-box-50.png"));
	lableIcon->setAlignment(Qt::AlignCenter);
	lableText->setAlignment(Qt::AlignCenter);
	noWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	closeBtn = new QPushButton("Hide Panel");
	noWidgetLayout->addWidget(closeBtn);

	stackWidget->addWidget(assetViewWidget);
	stackWidget->addWidget(noWidget);
	this->setWidgetToBeShown();
	configureConnections();
}


ShaderAssetWidget::~ShaderAssetWidget()
{
	
}

void ShaderAssetWidget::updateAssetView(const QString & path)
{
	assetViewWidget->clear();

	// No library or no project = nothing to list, and the stacked widget's
	// "no scene open" page is what the user sees (setWidgetToBeShown decides
	// which page that is). The truthful empty state, not a skipped refresh.
	// THE PROJECT DRAWER *IS* THE EDITOR'S ASSET TRAY, filtered to materials
	// (the four-drawer rule, OWNER_REVIEW 9: "the project asset tray should
	// mirror the materials project drawer" — ONE LIST, TWO WINDOWS).
	//
	// So it calls the function the tray calls — `assettray::list`, which is
	// the single implementation the tray panel, its search and
	// `assets.list({tray:true})` already share (services/assettray.h). A
	// second query here was a second RULE: it listed pinned rows plus the
	// folder's rows and knew nothing of the tray's collapsing (an import
	// member, a row the editor minted rather than the user, a companion
	// whose image is its tile), so the two windows disagreed by
	// construction. `path` is the folder the user has navigated into — the
	// project's own guid at the root — and the tray listing takes exactly
	// that.
	if (db && project && !project->getProjectGuid().isEmpty()) {
		const QString folder = path.isEmpty() ? project->getProjectGuid() : path;
		for (const auto &asset : assettray::list(db, project->getProjectGuid(), folder,
		                                         static_cast<int>(ModelTypes::Material)))
			addItem(asset);
	}

	setWidgetToBeShown();
}

void ShaderAssetWidget::addItem(const FolderRecord & folderData)
{
	if (!folderData.visible) return;

	QListWidgetItem *item = new QListWidgetItem;
	item->setData(Qt::DisplayRole, folderData.name);
	item->setData(MODEL_ITEM_TYPE, MODEL_FOLDER);
	item->setData(MODEL_GUID_ROLE, folderData.guid);
	item->setData(MODEL_PARENT_ROLE, folderData.parent);

	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setFlags(item->flags() | Qt::ItemIsEditable);
	item->setIcon(QIcon(":/icons/icons8-folder-72.png"));
	 
	assetViewWidget->addItem(item);
}

void ShaderAssetWidget::addItem(const AssetRecord & assetData)
{
    auto prop = QJsonDocument::fromJson(assetData.properties).object();
	if (!prop["type"].toString().isEmpty()) {
		// No need to check further, this is a builtin asset
		return;
	}

	// dont show item if its not a shader asset

    auto doc = QJsonDocument::fromJson(assetData.asset);
	auto obj = doc.object();
	auto name = obj["name"].toString();

	QListWidgetItem *item = new QListWidgetItem;
	item->setData(Qt::DisplayRole, QFileInfo(assetData.name).baseName());
	item->setData(Qt::UserRole, assetData.name);
	item->setData(MODEL_TYPE_ROLE, assetData.type);
	item->setData(MODEL_ITEM_TYPE, MODEL_ASSET);
	item->setData(MODEL_GUID_ROLE, assetData.guid);
	item->setData(MODEL_PARENT_ROLE, assetData.parent);
	ListWidget::updateThumbnailImage(assetData.thumbnail, item);

	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setFlags(item->flags() | Qt::ItemIsEditable);

	// Hide meshes for now, we work with objects which are parents for meshes, materials etc
	assetViewWidget->addItem(item);
}

void ShaderAssetWidget::setUpDatabase(Database * db)
{
	// THE HANDLE IS STORED UNCONDITIONALLY (lane DBPTR-1). It used to be kept
	// only when a scene was open at this moment — and the one caller is
	// EffectsPage's CONSTRUCTOR (MaterialsModule builds the page, THEN hands it
	// the scene-open probe), so `sceneOpenProbe` was still empty here every
	// time and `db` was never assigned at all. The member was uninitialised,
	// so every later `db->fetchChildAssets(...)` — one per switch into the
	// Materials space, through refresh() — ran on a wild pointer; it survived
	// only because that query touches no member of Database (the same accident
	// that hid AssetPanel::handle, CLOSE-2).
	this->db = db;
	//remove noWidget if preset and add assetViewWidget
	if (sceneOpenProbe && sceneOpenProbe() && project) {
		updateAssetView(project->getProjectGuid());
		assetItemShader.selectedGuid = project->getProjectGuid();
	}
}

void ShaderAssetWidget::refresh()
{
	updateAssetView(project ? project->getProjectGuid() : QString());
}

void ShaderAssetWidget::setWidgetToBeShown()
{
	if (sceneOpenProbe && sceneOpenProbe()) {
		stackWidget->setCurrentIndex(0);
	}
	else {
		stackWidget->setCurrentIndex(1);

	}
}

void ShaderAssetWidget::configureConnections()
{
	connect(assetViewWidget, &ListWidget::renameShader, [=](QString guid) {
		assetViewWidget->editItem(assetViewWidget->currentItem());
	});
	connect(assetViewWidget, &ListWidget::exportShader, [=](QString guid) {

	});
	connect(assetViewWidget, &ListWidget::editShader, [=](QString guid) {
		emit loadToGraph(assetViewWidget->currentItem());
	});
	connect(assetViewWidget, &ListWidget::deleteShader, [=](QString guid) {
		deleteShader(guid);
	});
	connect(assetViewWidget->itemDelegate(), &QAbstractItemDelegate::commitData, [=]() {
		//item finished editing
		editingFinishedOnListItem(assetViewWidget->currentItem());
	});
}

void ShaderAssetWidget::deleteShader(QString guid)
{
	// DELETE FROM THE PROJECT DRAWER MEANS "TAKE IT OUT OF THIS PROJECT"
	// (MATERIAL_BUNDLE_SPEC phase 2's Deletes column — the module's private
	// asset code). What was here was 130 lines of the module's own delete: a
	// hand-rolled dependency dialog over `deleteAssetAndDependencies`, plus a
	// sweep that removed FILES from the project folder by display name. Every
	// premise of it is gone — a project holds no asset files (they are pins on
	// store objects), and the library-delete law says a row a project pins is
	// unlisted, never deleted. It also answered the wrong question: this is the
	// PROJECT drawer, and removing something from a project must not touch the
	// library at all.
	//
	// `assetdelete::removeFromProject` is the one implementation the editor's
	// tray Delete uses: it drops this project's pin and the pins of the members
	// only this bundle uses (a texture two materials share keeps its pin), and
	// reaps a row that was already unlisted and has just lost its last pin.
	if (!db || !project || project->getProjectGuid().isEmpty()) return;
	auto *item = assetViewWidget->currentItem();
	if (!item) return;

	// A FOLDER, TOO, IS A PROJECT-SIDE REMOVE (fix round F16). This called
	// `deleteFolderAndDependencies`, which deletes the folder AND the library
	// rows of everything filed in it — from the PROJECT drawer, which
	// contradicts the one rule this function exists to keep. Each asset in
	// the folder leaves the project by the same door a single tile takes, and
	// then the (now empty) folder row goes.
	if (item->data(MODEL_ITEM_TYPE).toInt() == MODEL_FOLDER) {
		const QString folder = item->data(MODEL_GUID_ROLE).toString();
		for (const auto &asset : db->fetchChildAssets(folder, project->getProjectGuid())) {
			assetdelete::removeFromProject(db, asset.guid, project->getProjectGuid());
			emit assetRemoved(asset.guid);
		}
		db->deleteFolder(folder);
		refresh();
		return;
	}
	const QString target = guid.isEmpty() ? item->data(MODEL_GUID_ROLE).toString() : guid;
	if (target.isEmpty()) return;
	assetdelete::removeFromProject(db, target, project->getProjectGuid());
	// AND THE PAGE IS TOLD (fix round F1): a '(project)' tab is editing this
	// project's copy, and the pin it reads and writes through has just gone.
	emit assetRemoved(target);
	refresh();
}

void ShaderAssetWidget::editingFinishedOnListItem(QListWidgetItem *item)
{
	// THE DRAWER DOES NOT RENAME ANYTHING (MATERIALS_TABS_SPEC §7). It used
	// to write the catalog row itself — `db->renameAsset` — which is neither
	// of the two things a rename is: it bypasses the ONE name writer (so it
	// would rename a shipped preset, or take a preset's name) and it never
	// touched the DEFINITION, so the stored name stayed behind and the next
	// save of that material put the old one straight back. The page owns the
	// rename, for every drawer; this says which row the user typed in.
	if (!db || !project) return;   // nothing to rename without a library
	const QString guid = item->data(MODEL_GUID_ROLE).toString();
	const QString newName = item->data(Qt::DisplayRole).toString();
	if (guid.isEmpty() || newName.isEmpty()) return;
	if (newName == db->fetchAsset(guid).name) return;
	emit assetRenamed(guid, newName);
}


void ShaderAssetWidget::createFolder()
{
	if (!db || !project) return;   // a folder is a library row
	const QString newFolder = "New Folder";
	QListWidgetItem *item = new QListWidgetItem;
	item->setFlags(item->flags() | Qt::ItemIsEditable);
	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setIcon(QIcon(":/icons/icons8-folder-72.png"));

	item->setData(MODEL_GUID_ROLE, GUIDManager::generateGUID());
	item->setData(MODEL_PARENT_ROLE, assetItemShader.selectedGuid);
	item->setData(MODEL_ITEM_TYPE, MODEL_FOLDER);

	assetItemShader.wItem = item;

	QString folderName = newFolder;
	QStringList foldersInProject = db->fetchFolderNameByParent(assetItemShader.selectedGuid);

	// If we encounter the same file, make a duplicate...
	int increment = 1;
	while (foldersInProject.contains(folderName)) {
		folderName = newFolder + " " + QString::number(increment++);
	}

	const QString guid = item->data(MODEL_GUID_ROLE).toString();
	const QString parent = item->data(MODEL_PARENT_ROLE).toString();

	//// Create a new database entry for the new folder
	db->createFolder(folderName, parent, guid, project->getProjectGuid());

	// We could just addItem but this is by choice and also so we can order folders first
	updateAssetView(assetItemShader.selectedGuid);
}

void ShaderAssetWidget::addDroppedToProject(QListWidgetItem *item)
{
	// A LIBRARY TILE DROPPED ON THE PROJECT DRAWER IS AN ADD, NOT A MINT
	// (MATERIAL_BUNDLE_SPEC 5). This used to create a fresh ModelTypes::Shader
	// row in the project from `app/templates/ShaderTemplate.shader` — a format
	// that predates the graph and cannot be reopened by the graph loader, so
	// the tile it produced was unopenable. The gesture is the same pin every
	// other asset gets: one row, its closure pinned with it.
	if (!db || !project || project->getProjectGuid().isEmpty() || !item) return;
	const QString guid = item->data(MODEL_GUID_ROLE).toString();
	if (guid.isEmpty()) return;
	ProjectAssets::addToProject(guid, db, project, ProjectAssets::AddKind::Direct);
	refresh();
}


QByteArray ShaderAssetWidget::fetchAsset(QString string)
{
	if (!db) return QByteArray();
	return db->fetchAssetData(string);
}


