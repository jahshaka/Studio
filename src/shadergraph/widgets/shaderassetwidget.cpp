/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "shaderassetwidget.h"
#include <QMenu>
#include <QEvent>
#include <QMouseEvent>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDebug>


#include "irisgl/src/core/irisutils.h"
#include "../core/materialhelper.h"
#include "../models/properties.h"
#include <QStandardPaths>
#include <QDirIterator>

#include "../shadergraphmainwindow.h"
#if(EFFECT_BUILD_AS_LIB)
#include "../../io/assetmanager.h"
#include "../../uimanager.h"
#include "../../globals.h"
#include "../../core/database/database.h"
#include "../../core/guidmanager.h"
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
		createShader(item);
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

	for (const auto &asset : db->fetchChildAssets(path, static_cast<int>(ModelTypes::Shader))) 
		addItem(asset);

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
	//remove noWidget if preset and add assetViewWidget
	if (UiManager::isSceneOpen) {
		this->db = db;
		updateAssetView(Globals::project->getProjectGuid());
		assetItemShader.selectedGuid = Globals::project->getProjectGuid();
		
	}
}

void ShaderAssetWidget::refresh()
{
	updateAssetView(Globals::project->getProjectGuid());
}

void ShaderAssetWidget::setWidgetToBeShown()
{
	if (UiManager::isSceneOpen) {
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
	auto item = assetViewWidget->currentItem();

	// Delete folder and contents
	if (item->data(MODEL_ITEM_TYPE).toInt() == MODEL_FOLDER) {
		for (const auto &files : db->deleteFolderAndDependencies(item->data(MODEL_GUID_ROLE).toString())) {
			auto file = QFileInfo(QDir(Globals::project->getProjectFolder()).filePath(files));
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
				auto file = QFileInfo(QDir(Globals::project->getProjectFolder()).filePath(files));
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
					auto file = QFileInfo(QDir(Globals::project->getProjectFolder()).filePath(files));
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

						auto file = QFileInfo(QDir(Globals::project->getProjectFolder()).filePath(db->fetchAsset(itemGuid).name));
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

		dialog.setStyleSheet(
			"* { color: #EEE; }"
			"QDialog { background: #202020; padding: 4px; }"
			"QPushButton { background: #444; color: #EEE; border: 0; padding: 6px 10px; }"
			"QPushButton:hover { background: #555; color: #EEE; }"
			"QPushButton:pressed { background: #333; color: #EEE; }"
			"QListWidget { show-decoration-selected: 1; background: #202020; border: 0; outline: 0 }"
			"QListWidget::item:selected { background-color: #191919; }"
			"QListWidget::item:selected:active { background-color: #191919; }"
			"QListWidget::item { padding: 5px 0; }"
			"QListWidget::item:hover { background: #303030; }"
			"QListWidget::item:disabled { background: #202020; color: #888; }"
			"QListWidget::item:disabled:hover { background: #202020; color: #888; }"
			"QListWidget::item:hover:!active { background: #202020; color: #888; }"
			"QListWidget { spacing: 0 5px; }"
			"QListWidget::indicator { width: 18px; height: 18px; }"
			"QListWidget::indicator::unchecked { image: url(:/icons/check-unchecked.png); }"
			"QListWidget::indicator::checked { image: url(:/icons/check-checked.png); }"
			"QListWidget::indicator::disabled { image: url(:/icons/check-disabled.png); }"
		);
		dialog.exec();
	}
	
}

void ShaderAssetWidget::editingFinishedOnListItem(QListWidgetItem *item)
{
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
	db->createFolder(folderName, parent, guid);

	// We could just addItem but this is by choice and also so we can order folders first
	updateAssetView(assetItemShader.selectedGuid);
}

void ShaderAssetWidget::createShader(QString *shaderName)
{
	QString newShader;
	if (shaderName)	 newShader = *shaderName;
	else   newShader = "Untitled Shader";
	QListWidgetItem *item = new QListWidgetItem;
	item->setFlags(item->flags() | Qt::ItemIsEditable);
	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setIcon(QIcon(":/icons/icons8-file-72.png"));

	const QString assetGuid = GUIDManager::generateGUID();

	item->setData(MODEL_GUID_ROLE, assetGuid);
	item->setData(MODEL_PARENT_ROLE, assetItemShader.selectedGuid);
	item->setData(MODEL_ITEM_TYPE, MODEL_ASSET);
	item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Shader));
	assetItemShader.wItem = item;


	QStringList assetsInProject = db->fetchAssetNameByParent(assetItemShader.selectedGuid);

	//// If we encounter the same file, make a duplicate...
	int increment = 1;
	while (assetsInProject.contains(IrisUtils::buildFileName(newShader, "shader"))) {
		newShader = QString(newShader + " %1").arg(QString::number(increment++));
	}

	db->createAssetEntry(assetGuid,
		IrisUtils::buildFileName(newShader, "shader"),
		static_cast<int>(ModelTypes::Shader),
		assetItemShader.selectedGuid,
		QByteArray());

	item->setText(newShader);
	assetViewWidget->addItem(item);

	QFile *templateShaderFile = new QFile(IrisUtils::getAbsoluteAssetPath("app/templates/ShaderTemplate.shader"));
	templateShaderFile->open(QIODevice::ReadOnly | QIODevice::Text);
	QJsonObject shaderDefinition = QJsonDocument::fromJson(templateShaderFile->readAll()).object();
	templateShaderFile->close();
	shaderDefinition["name"] = newShader;
	shaderDefinition.insert("guid", assetGuid);

	auto assetShader = new AssetMaterial;
	assetShader->fileName = newShader;// IrisUtils::buildFileName(newShader, "material");
	assetShader->assetGuid = assetGuid;
	assetShader->path = IrisUtils::join(Globals::project->getProjectFolder(), IrisUtils::buildFileName(newShader, "shader"));
	assetShader->setValue(QVariant::fromValue(shaderDefinition));

    db->updateAssetAsset(assetGuid, QJsonDocument(shaderDefinition).toJson());

	AssetManager::addAsset(assetShader);
}

QString ShaderAssetWidget::createShader(QListWidgetItem * item)
{
	const QString newShader = "Untitled Shader";
	
	item->setSizeHint(currentSize);

	const QString targetGuid = shadergraph::MainWindow::genGUID();

	
	assetItemShader.wItem = item;

	QString shaderName = item->data(Qt::DisplayRole).toString();
	QString sourceGuid = item->data(MODEL_GUID_ROLE).toString();

	AssetRecord sourceRecord = db->fetchAsset(sourceGuid);
	auto sourceData = db->fetchAssetData(sourceGuid);

    auto doc = QJsonDocument::fromJson(sourceData);
	auto obj = doc.object();
	auto list = obj["properties"].toArray();
	QString str;
	str = doc.toJson();

		for (auto prop : list) {
			auto type = prop.toObject()["type"].toString();
			if (type == "texture") {
				auto value = prop.toObject()["value"].toString();

				auto assetPath = IrisUtils::join(
                    QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
					"AssetStore"
				);
				

				const QString assetFolder = QDir(assetPath).filePath(value);
				QDirIterator it(assetFolder);

				while (it.hasNext()) {
					auto imgGuid = shadergraph::MainWindow::genGUID();
					auto fileName = it.next();
					auto splitted = fileName.split('/');
					if (splitted.back() == '.' || splitted.back() == "..") continue;

					//get extension only
					auto spl = splitted.back().split('.');

					// find the old guid and replace with the new guid
					str = str.replace(value, imgGuid);

					QFile file(fileName);
					//assigns the guid for the file name and the original extension
					auto newName = value +'.'+ spl.back();

					QFile::copy(file.fileName(), Globals::project->getProjectFolder()+'/'+ newName);
					
					db->createAssetEntry(Globals::project->getProjectGuid(), imgGuid, newName, static_cast<int>(ModelTypes::Texture));
					db->createDependency(static_cast<int>(ModelTypes::Shader), static_cast<int>(ModelTypes::Texture), targetGuid, imgGuid, Globals::project->getProjectGuid());

				}
			}
		}

		//update source data after editing guids - dirty
		auto var = QVariant(str);
		auto updatedDoc = QJsonDocument::fromJson(str.toUtf8());
		

	db->createAssetEntry(targetGuid,
		sourceRecord.name,
		static_cast<int>(ModelTypes::Shader),
		Globals::project->getProjectGuid());

	
    db->updateAssetAsset(targetGuid, updatedDoc.toJson());
	
	db->updateAssetThumbnail(targetGuid, db->fetchAsset(item->data(MODEL_GUID_ROLE).toString()).thumbnail);

	// todo: create new item
	assetViewWidget->addItem(item);

	auto assetShader = new AssetShader;
	assetShader->fileName = shaderName;//  IrisUtils::buildFileName(shaderName, "material");
	assetShader->assetGuid = targetGuid;
	assetShader->path = IrisUtils::join(Globals::project->getProjectFolder(), IrisUtils::buildFileName(shaderName, "shader"));

	//auto assetData = db->fetchAssetData(targetGuid);
    auto matObj = QJsonDocument::fromJson(sourceData).object();
	//auto mat = MaterialHelper::generateMaterialFromMaterialDefinition(matObj, true);
	//assetShader->setValue(QVariant::fromValue(mat));
	assetShader->setValue(matObj);

	//db->updateAssetAsset(assetGuid, QJsonDocument::fromBinaryData(fetchAsset(item->data(MODEL_GUID_ROLE).toString())).toBinaryData());

	// build material from definition
	AssetManager::addAsset(assetShader);
	refresh();
	return targetGuid;
}

QByteArray ShaderAssetWidget::fetchAsset(QString string)
{
	return db->fetchAssetData(string);
}


