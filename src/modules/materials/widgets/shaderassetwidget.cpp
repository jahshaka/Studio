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
	// Every branch below is a library write; with no library open there is
	// nothing to delete (the list this menu came from is empty too).
	if (!db || !project) return;
	auto item = assetViewWidget->currentItem();

	// Delete folder and contents
	if (item->data(MODEL_ITEM_TYPE).toInt() == MODEL_FOLDER) {
		for (const auto &files : db->deleteFolderAndDependencies(item->data(MODEL_GUID_ROLE).toString())) {
			auto file = QFileInfo(QDir(project->getProjectFolder()).filePath(files));
			if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
		}
	}

	// Delete asset and dependencies
	if (item->data(MODEL_ITEM_TYPE).toInt() == MODEL_ASSET) {
		QStringList dependentAssets;
		for (const auto &files :
			db->fetchAssetGUIDAndDependencies(item->data(MODEL_GUID_ROLE).toString()))
		{
			dependentAssets.append(files);
		}

		// If a asset is single, remove it
		// If an asset has multiple dependers, warn
		// If an asset has dependencies that have multiple dependers, warn

		QStringList otherDependers;
		QStringList assetWithDeps;

		for (const auto &asset : dependentAssets) {
			auto dependers = db->hasMultipleDependers(asset);
			if (dependers.count() > 1) {
				otherDependers.append(dependers);
				assetWithDeps.append(asset);
			}
		}

		// Don't warn if it's a single asset, just break stuff
		if (assetWithDeps.isEmpty()) {
			// do a normal delete and return
			for (const auto &files : db->deleteAssetAndDependencies(item->data(MODEL_GUID_ROLE).toString())) {
				auto file = QFileInfo(QDir(project->getProjectFolder()).filePath(files));
				if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
			}

			refresh();
			return;
		}

		QListWidget *assetsToRemove = new QListWidget;

		bool assetHasDependencies = db->hasDependencies(item->data(MODEL_GUID_ROLE).toString());

		if (assetHasDependencies) {
			QStringListIterator it(dependentAssets);
			int iter = 0;
			while (it.hasNext()) {
				auto guid = it.next();
				QListWidgetItem *listItem = new QListWidgetItem(db->fetchAsset(guid).name, assetsToRemove);
				listItem->setData(Qt::UserRole, guid);
				if (!iter || db->fetchAsset(guid).type == static_cast<int>(ModelTypes::Mesh)) {
					listItem->setFlags(item->flags() & ~Qt::ItemIsEnabled);
				}
				if (assetWithDeps.contains(guid)) {
					listItem->setCheckState(Qt::Unchecked);
				}
				else {
					listItem->setCheckState(Qt::Checked);
				}
				assetsToRemove->addItem(listItem);
				iter++;
			}
		}
		else {
			QStringListIterator it(otherDependers);
			int iter = 0;
			while (it.hasNext()) {
				auto guid = it.next();
				QListWidgetItem *listItem = new QListWidgetItem(db->fetchAsset(guid).name, assetsToRemove);
				listItem->setFlags(item->flags() & ~Qt::ItemIsSelectable);
				listItem->setData(Qt::UserRole, guid);
				assetsToRemove->addItem(listItem);
				iter++;
			}
		}

		QDialog dialog;
		dialog.setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
		dialog.setWindowTitle("Dependent Assets");

		QLabel *textLabel = new QLabel;

		if (assetHasDependencies) {
			textLabel->setText(
				"The assets below will be deleted as dependencies.\n"
				"Unticked items are being used with other assets, select them to remove them as well."
			);
		}
		else {
			textLabel->setText(
				"The assets below are dependent on this asset.\n"
				"If you choose to continue removing this asset, those assets will be affected."
			);
		}

		auto layout = new QVBoxLayout;
		dialog.setLayout(layout);

		layout->addWidget(textLabel);
		layout->addSpacing(8);
		layout->addWidget(assetsToRemove);

		auto blayout = new QHBoxLayout;
		auto bwidget = new QWidget;
		bwidget->setLayout(blayout);
		QPushButton *deleteSelected = new QPushButton("Delete Selected");
		QPushButton *cancel = new QPushButton("Cancel");
		blayout->addStretch(1);
		blayout->addWidget(deleteSelected);
		blayout->addWidget(cancel);
		layout->addWidget(bwidget);

		if (!assetHasDependencies) {
			connect(deleteSelected, &QPushButton::pressed, this, [&]() {
				dialog.close();

				for (const auto &files : db->deleteAssetAndDependencies(item->data(MODEL_GUID_ROLE).toString())) {
					auto file = QFileInfo(QDir(project->getProjectFolder()).filePath(files));
					if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
				}

				//delete ui->assetView->takeItem(ui->assetView->row(item));
				refresh();
			});
		}
		else {
			connect(deleteSelected, &QPushButton::pressed, this, [&]() {
				dialog.close();

				for (int i = 0; i < assetsToRemove->count(); ++i) {
					QListWidgetItem *item = assetsToRemove->item(i);
					auto itemGuid = item->data(Qt::UserRole).toString();

					if (item->checkState() == Qt::Checked) {
						db->deleteAsset(itemGuid);
						db->deleteDependency(item->data(MODEL_GUID_ROLE).toString(), itemGuid);

						auto file = QFileInfo(QDir(project->getProjectFolder()).filePath(db->fetchAsset(itemGuid).name));
						if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
					}
				}

				//delete ui->assetView->takeItem(ui->assetView->row(item));
				refresh();
			});
		}

		connect(cancel, &QPushButton::pressed, this, [&dialog]() {
			dialog.close();
		});

		dialog.setStyleSheet(StyleSheet::ShaderAssetConfirmDialog());
		dialog.exec();
	}
	
}

void ShaderAssetWidget::editingFinishedOnListItem(QListWidgetItem *item)
{
	if (!db || !project) return;   // nothing to rename without a library
	QString newName = item->data(Qt::DisplayRole).toString();
	const QString guid = item->data(MODEL_GUID_ROLE).toString();
	const QString oldName = db->fetchAsset(guid).name;
	qDebug() << oldName << newName;
	if (newName == oldName) return;
	else {
		//item->setText(newName);
		item->setData(Qt::DisplayRole, newName);
		item->setData(Qt::UserRole, newName);
		db->renameAsset(guid, newName);
		refresh();
	}
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


