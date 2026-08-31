/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/assetwidget.h"
#include "ui_assetwidget.h"

#include <iostream>
#include <QDate>
#include <QAbstractItemModel>
#include <QBuffer>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProgressDialog>
#include <QProcess>
#include <QTemporaryDir>
#include <QComboBox>

#include <algorithm>

#include "irisgl/core/irisutils.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/scenegraph/particlesystemnode.h" 
#include "irisgl/document/scenegraph/scene.h" 
#include "zip.h"

#include "ui/pages/assetview.h"
#include "data/constants.h"

#include "shell/mainwindow.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "data/settingsmanager.h"
#include "services/thumbnailmanager.h"
#include "services/thumbnailgenerator.h"
#include "services/assethelper.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimportservice.h"
#include "services/import/importbatchrunner.h"
#include "services/projectassets.h"
#include "services/assetcas.h"
#include <QSqlDatabase>
#include "io/ziphelper.h"
#include "io/assetmanager.h"
#include "io/scenewriter.h"
#include "services/subscriber.h"
#include "data/materialpreset.h"
#include "io/materialpresetreader.h"
#include "io/materialreader.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"
#include <QActionGroup>

namespace {
// Pin-world byte resolution for the .jaf exporters (phase 4): an asset's
// bytes live in the CAS, addressed through the project pin - the flat
// project folder holds nothing.
QString resolvePinnedAssetPath(Project *project, const QString &assetGuid, QString *nameOut)
{
    return AssetCas::resolvePinned(QSqlDatabase::database(), AssetStorePaths::root(),
                                   project ? project->getProjectGuid() : QString(),
                                   assetGuid, nameOut);
}
} // namespace


AssetWidget::AssetWidget(Database *handle, QWidget *parent) : QWidget(parent), ui(new Ui::AssetWidget)
{
	ui->setupUi(this);

	this->db = handle;

	ui->assetView->setAttribute(Qt::WA_MacShowFocusRect, false);
	ui->assetTree->setAttribute(Qt::WA_MacShowFocusRect, false);
	ui->assetView->viewport()->setAttribute(Qt::WA_MacShowFocusRect, false);
	ui->assetTree->viewport()->setAttribute(Qt::WA_MacShowFocusRect, false);

	ui->assetView->viewport()->installEventFilter(this);
	ui->assetTree->viewport()->installEventFilter(this);
	ui->assetTree->setContextMenuPolicy(Qt::CustomContextMenu);
	
	connect(ui->assetTree, SIGNAL(itemClicked(QTreeWidgetItem*, int)),
		this, SLOT(treeItemSelected(QTreeWidgetItem*)));

	connect(ui->assetTree, SIGNAL(itemChanged(QTreeWidgetItem*, int)),
		this, SLOT(treeItemChanged(QTreeWidgetItem*, int)));

	connect(ui->assetTree, SIGNAL(customContextMenuRequested(const QPoint&)),
		this, SLOT(sceneTreeCustomContextMenu(const QPoint&)));

	ui->assetView->setContextMenuPolicy(Qt::CustomContextMenu);
	ui->assetView->setResizeMode(QListWidget::Adjust);
	ui->assetView->setMovement(QListView::Static);
    ui->assetView->setSelectionBehavior(QAbstractItemView::SelectItems);
	ui->assetView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->assetView->setSelectionRectVisible(false);

	ui->assetView->setDragEnabled(true);
	ui->assetView->setDragDropMode(QAbstractItemView::DragDrop);

	activeFilter = SettingsManager::getDefaultManager()->getValue("active_filter", 0).toInt();
	showDependencies = SettingsManager::getDefaultManager()->getValue("show_dependencies", false).toBool();
	ui->showDeps->setChecked(showDependencies);

	connect(ui->showDeps, &QCheckBox::toggled, [this](bool state) {
		showDependencies = state;
		SettingsManager::getDefaultManager()->setValue("show_dependencies", state);
		updateAssetView(assetItem.selectedGuid, activeFilter, showDependencies);
	});

    ui->assetView->setItemDelegate(new ListViewDelegate());
    ui->assetView->setTextElideMode(Qt::ElideRight);

	connect(ui->assetView,  SIGNAL(itemClicked(QListWidgetItem*)),
		    this,           SLOT(assetViewClicked(QListWidgetItem*)));

	connect(ui->assetView,  SIGNAL(customContextMenuRequested(const QPoint&)),
		    this,           SLOT(sceneViewCustomContextMenu(const QPoint&)));

	connect(ui->assetView,  SIGNAL(itemDoubleClicked(QListWidgetItem*)),
		    this,           SLOT(assetViewDblClicked(QListWidgetItem*)));

	connect(ui->assetView->itemDelegate(), &QAbstractItemDelegate::commitData, this, &AssetWidget::OnLstItemsCommitData);

	connect(ui->searchBar, SIGNAL(textChanged(QString)), this, SLOT(searchAssets(QString)));

	connect(ui->importBtn, SIGNAL(pressed()), SLOT(importAssetB()));
    ui->importBtn->setVisible(false);

	// The signal will be emitted from another thread (Nick) — or, on the engine
	// viewport, from the main thread. Either way the generator needs the database
	// for asset/material lookups (the legacy viewport also sets it, later).
	ThumbnailGenerator::getSingleton()->setDatabase(db);
	connect(ThumbnailGenerator::getSingleton(),   SIGNAL(thumbnailComplete(ThumbnailResult*)),
		    this,                                 SLOT(onThumbnailResult(ThumbnailResult*)));

	breadCrumbLayout = new QHBoxLayout;
	breadCrumbLayout->setSpacing(0);
	ui->breadCrumb->setObjectName(QStringLiteral("BreadCrumb"));
	ui->breadCrumb->setLayout(breadCrumbLayout);
	// Display ▾ — same grey popup-button pattern as the desktop footer's
	// Tile Size/Desktops/Layouts buttons (owner direction): static label,
	// the checked popup entry is the current view mode. Replaces the old
	// Icon/List toggle-button pair.
	displayButton = new QPushButton(tr("Display ▾"));
	displayButton->setCursor(Qt::PointingHandCursor);

	displayMenu = new QMenu(this);
	displayMenu->setStyleSheet(StyleSheet::QMenuDarkDesktop());
	auto displayGroup = new QActionGroup(displayMenu);
	displayGroup->setExclusive(true);
	displayGridAction = displayMenu->addAction(tr("Grid"));
	displayGridAction->setCheckable(true);
	// Todo - use preferences
	displayGridAction->setChecked(true);
	displayGroup->addAction(displayGridAction);
	displayListAction = displayMenu->addAction(tr("List"));
	displayListAction->setCheckable(true);
	displayGroup->addAction(displayListAction);

	QHBoxLayout *toggleLayout = new QHBoxLayout;
	toggleLayout->setSpacing(0);
	toggleLayout->setSizeConstraint(QLayout::SetFixedSize);
	toggleLayout->addWidget(displayButton);

	iconSize = QSize(72, 72);
	listSize = QSize(32, 32);
	currentSize = iconSize;

    goBackOneControl = new QPushButton(tr("<"));
    goUpOneControl = new QPushButton(tr("Go Up"));
    goUpOneControl->setEnabled(false);

    QHBoxLayout *dirControlLayout = new QHBoxLayout;
    dirControlLayout->setSpacing(0);
    dirControlLayout->setSizeConstraint(QLayout::SetFixedSize);
    //dirControlLayout->addWidget(goBackOneControl);
    dirControlLayout->addWidget(goUpOneControl);

    ui->dirControls->setLayout(dirControlLayout);
    ui->dirControls->setObjectName("DirControl");

    connect(goBackOneControl, &QPushButton::pressed, [this]() {
        //updateAssetView(assetItem.selectedGuid);
    });

    connect(goUpOneControl, &QPushButton::pressed, [this]() {
        updateAssetView(db->fetchAsset(assetItem.selectedGuid).parent, activeFilter, showDependencies);
    });

	setMouseTracking(true);
	ui->assetView->setMouseTracking(true);

	ui->assetView->setViewMode(QListWidget::IconMode);
	ui->assetView->setSpacing(4);
	ui->assetView->setIconSize(currentSize);

	connect(displayGridAction, &QAction::triggered, this, [this]() {
		ui->assetView->setViewMode(QListWidget::IconMode);
		ui->assetView->setAlternatingRowColors(false);
		ui->assetView->setSpacing(4);
		currentSize = iconSize;
		ui->assetView->setIconSize(currentSize);
		ui->assetView->setItemDelegate(new ListViewDelegate());
		updateAssetView(assetItem.selectedGuid, activeFilter, showDependencies);
	});

	connect(displayListAction, &QAction::triggered, this, [this]() {
		ui->assetView->setViewMode(QListWidget::ListMode);
		ui->assetView->setAlternatingRowColors(true);
		ui->assetView->setSpacing(0);
		currentSize = listSize;
		ui->assetView->setIconSize(currentSize);
		ui->assetView->setItemDelegate(new QStyledItemDelegate());
		updateAssetView(assetItem.selectedGuid, activeFilter, showDependencies);
	});

	connect(displayButton, &QPushButton::pressed, this, [this]() {
		displayMenu->exec(displayButton->mapToGlobal(QPoint(0, displayButton->height())));
	});

	ui->switcher->setLayout(toggleLayout);
	ui->switcher->setObjectName("Switcher");

	if (!ThemeManager::classicActive()) {
		// The shared chrome button spec at panel-header height (owner
		// direction): rounded grey with side gutters, compact. Classic keeps
		// its #Switcher/#DirControl sheets bit-for-bit.
		goUpOneControl->setStyleSheet(ThemeManager::chromeCompactButtonSheet());
		displayButton->setStyleSheet(ThemeManager::chromeCompactButtonSheet());
	}

    filterGroupLayout = new QHBoxLayout;
    filterGroupLayout->setContentsMargins(0, 0, 0, 0);
    filterGroupLayout->setSpacing(0);
    ui->filterWidget->setObjectName(QStringLiteral("FilterWidget"));
    ui->filterWidget->setLayout(filterGroupLayout);

    assetFilterCombo = new QComboBox(this);
    assetFilterCombo->addItem("All Assets", QVariant::fromValue(0));
    assetFilterCombo->addItem("Objects", QVariant::fromValue(static_cast<int>(ModelTypes::Object)));
    assetFilterCombo->addItem("Materials", QVariant::fromValue(static_cast<int>(ModelTypes::Material)));
    assetFilterCombo->addItem("Particle Systems", QVariant::fromValue(static_cast<int>(ModelTypes::ParticleSystem)));
    assetFilterCombo->addItem("Shaders", QVariant::fromValue(static_cast<int>(ModelTypes::Shader)));
    assetFilterCombo->addItem("Textures", QVariant::fromValue(static_cast<int>(ModelTypes::Texture)));
    assetFilterCombo->addItem("Files", QVariant::fromValue(static_cast<int>(ModelTypes::File)));
    assetFilterCombo->addItem("Music", QVariant::fromValue(static_cast<int>(ModelTypes::Music)));
    assetFilterCombo->addItem("Skies", QVariant::fromValue(static_cast<int>(ModelTypes::Sky)));

	int index = assetFilterCombo->findData(activeFilter);
	assetFilterCombo->setCurrentIndex(index);

    filterGroupLayout->addWidget(new QLabel("Filter Assets:"));
    filterGroupLayout->addWidget(assetFilterCombo);

    connect<void(QComboBox::*)(int)>(assetFilterCombo, &QComboBox::currentIndexChanged, this, [&](int index) {
		activeFilter = assetFilterCombo->itemData(index).toInt();
		SettingsManager::getDefaultManager()->setValue("active_filter", activeFilter);
        updateAssetView(assetItem.selectedGuid, activeFilter, showDependencies);
    });

    ui->filterWidget->setStyleSheet(StyleSheet::AssetWidgetFilterPane());

	ui->searchBar->setPlaceholderText(tr("Type to search for assets..."));

	progressDialog = new ProgressDialog;
	progressDialog->setLabelText("Importing assets...");

	setStyleSheet(StyleSheet::AssetWidgetPanel());
}

void AssetWidget::trigger()
{
    // NO clearAssetList() here (IMAGE_PLANE_SPEC §6): trigger() runs AFTER
    // ProjectManager::registerProjectSessionAssets in every open flow
    // (loadProjectAssets[Sync] → MainWindow::openProject → trigger), so a
    // clear at this point silently WIPED the session hydration — the
    // "material drag no-ops after reopen" defect. Every path here already
    // cleared: closeProject() and loadProjectAssets[Sync]() both start with
    // AssetManager::clearAssetList(), so the built-in presets below register
    // exactly once per open.

    auto dir = QDir(IrisUtils::getAbsoluteAssetPath("app/content/materials"));
    auto files = dir.entryInfoList(QStringList(), QDir::Files);

    auto reader = new MaterialPresetReader();

    // needs opengl context so we have to call this after the window is shown...
    for (const auto &file : files) {
        auto preset = reader->readMaterialPreset(file.absoluteFilePath());

        auto m = iris::CustomMaterial::create();
        m->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));

        m->setValue("diffuseTexture", preset.diffuseTexture);
        m->setValue("specularTexture", preset.specularTexture);
        m->setValue("normalTexture", preset.normalTexture);
        m->setValue("reflectionTexture", preset.reflectionTexture);

        m->setValue("ambientColor", preset.ambientColor);
        m->setValue("diffuseColor", preset.diffuseColor);
        m->setValue("specularColor", preset.specularColor);

        m->setValue("shininess", preset.shininess);
        m->setValue("normalIntensity", preset.normalIntensity);
        m->setValue("reflectionInfluence", preset.reflectionInfluence);
        m->setValue("textureScale", preset.textureScale);

        auto assetMat = new AssetMaterial;
        assetMat->fileName = preset.name;
        assetMat->assetGuid = Constants::Reserved::DefaultMaterials.key(preset.name);
        assetMat->setValue(QVariant::fromValue(m));
        AssetManager::addAsset(assetMat);
    }

	// It's important that this gets called after a project has been loaded (iKlsR)
	populateAssetTree(true);

	for (auto &asset : AssetManager::getAssets()) {
		if (asset->type == ModelTypes::Object) {
			// Not every Object asset holds an AssimpObject: add-to-project registers
			// an AssetNodeObject (value = SceneNodePtr) under the same type, and
			// value<AssimpObject*>() then returns null — dereferencing unchecked was
			// a latent crash (ASSET_ADD_AUDIT D3). Those assets are already in their
			// final form; skip them.
			AssimpObject *assimpObject = asset->getValue().value<AssimpObject*>();
			if (!assimpObject || !assimpObject->getSceneData()) {
				qDebug() << "AssetWidget::trigger: Object asset" << asset->assetGuid
						 << "carries no assimp scene (already a node asset) — skipped";
				continue;
			}

			auto material = db->fetchAssetData(asset->assetGuid);
            auto materialObj = QJsonDocument::fromJson(material);

			auto node = iris::MeshNode::loadAsSceneFragment(QString(), assimpObject->getSceneData(),
				[&](iris::MeshPtr mesh, iris::MeshMaterialData& data)
			{
				auto mat = iris::CustomMaterial::create();
				mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

				return mat;
			});

			AssetHelper::updateNodeMaterial(node, materialObj.object(), db);

			//QString meshGuid = db->fetchObjectMesh(asset->assetGuid, static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh));

			//std::function<void(iris::SceneNodePtr&)> updateNodeValues = [&](iris::SceneNodePtr &node) -> void {
			//	if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
			//		auto n = node.staticCast<iris::MeshNode>();
			//		n->meshPath = meshGuid;
			//		auto mat = n->getMaterial().staticCast<iris::CustomMaterial>();
			//		for (auto prop : mat->properties) {
			//			if (prop->type == iris::PropertyType::Texture) {
			//				if (!prop->getValue().toString().isEmpty()) {
			//					mat->setValue(prop->name,
			//						IrisUtils::join(project->getProjectFolder(), "Textures",
			//							db->fetchAsset(prop->getValue().toString()).name));
			//				}
			//			}
			//		}
			//	}

			//	if (node->hasChildren()) {
			//		for (auto &child : node->children) {
			//			updateNodeValues(child);
			//		}
			//	}
			//};

			//updateNodeValues(node);

			QVariant variant = QVariant::fromValue(node);
			auto nodeAsset = new AssetNodeObject;
			nodeAsset->fileName = asset->fileName;
			nodeAsset->assetGuid = asset->assetGuid;
			nodeAsset->setValue(variant);

			// Replace the raw aiScene with a SceneNode
			asset = nodeAsset;
		}
	}
}

void AssetWidget::refresh()
{
	updateAssetView(assetItem.selectedGuid, activeFilter, showDependencies);
	populateAssetTree(false);
}

void AssetWidget::extractTexturesAndMaterialFromMaterial(
	const QString &filePath,
	QStringList &textureList,
	QJsonObject &mat)
{
	QFile *file = new QFile(filePath);
	file->open(QIODevice::ReadOnly | QIODevice::Text);
	QJsonDocument doc = QJsonDocument::fromJson(file->readAll());

	const QJsonObject materialDefinition = doc.object();
	auto shaderName = Constants::SHADER_DEFS + materialDefinition["name"].toString() + ".shader";

	auto material = iris::CustomMaterial::create();
	material->generate(IrisUtils::getAbsoluteAssetPath(shaderName));
	material->setName(materialDefinition["name"].toString());

	for (const auto &prop : material->properties) {
		if (materialDefinition.contains(prop->name)) {
			if (prop->type == iris::PropertyType::Texture) {
				auto textureStr = !materialDefinition[prop->name].toString().isEmpty()
					? materialDefinition[prop->name].toString()
					: QString();
				material->setValue(prop->name, textureStr);
				if (!textureStr.isEmpty()) {
					textureList.append(QFileInfo(textureStr).fileName());
				}
			}
			else {
				material->setValue(prop->name, materialDefinition[prop->name].toVariant());
			}
		}
	}

	SceneWriter::writeSceneNodeMaterial(mat, material, false);
}

void AssetWidget::extractTexturesAndMaterialFromMaterial(
	const QByteArray &blob,
	QStringList &textureList,
	QJsonObject &mat)
{
    QJsonDocument doc = QJsonDocument::fromJson(blob);
	const QJsonObject materialDefinition = doc.object();
	auto shaderName = Constants::SHADER_DEFS + materialDefinition["name"].toString() + ".shader";

	auto material = iris::CustomMaterial::create();
	material->generate(IrisUtils::getAbsoluteAssetPath(shaderName));
	material->setName(materialDefinition["name"].toString());

	for (const auto &prop : material->properties) {
		if (materialDefinition.contains(prop->name)) {
			if (prop->type == iris::PropertyType::Texture) {
				auto textureStr = !materialDefinition[prop->name].toString().isEmpty()
					? materialDefinition[prop->name].toString()
					: QString();
				material->setValue(prop->name, textureStr);
				//if (!textureStr.isEmpty()) {
				//	textureList.append(QFileInfo(textureStr).fileName());
				//}
			}
			else {
				material->setValue(prop->name, materialDefinition[prop->name].toVariant());
			}
		}
	}

	SceneWriter::writeSceneNodeMaterial(mat, material, false);
}

void AssetWidget::setEventBus(Subscriber *bus)
{
	// (Phase 4: was a Globals::eventSubscriber connect in the constructor.)
	if (bus) connect(bus,	&Subscriber::updateAssetSkyItemFromSkyPropertyWidget,
	                 this,	&AssetWidget::updateAssetSkyItemFromSkyPropertyWidget);
}

AssetWidget::~AssetWidget()
{
	delete ui;
}

void AssetWidget::populateAssetTree(bool initialRun)
{
	auto rootTreeItem = new QTreeWidgetItem();
	rootTreeItem->setText(0, "Assets");
	rootTreeItem->setIcon(0, QIcon(":/icons/icons8-folder-72.png"));
	rootTreeItem->setData(0, MODEL_GUID_ROLE, project->getProjectGuid());
	updateTree(rootTreeItem, project->getProjectGuid());

	ui->assetTree->clear();
	ui->assetTree->addTopLevelItem(rootTreeItem);
	ui->assetTree->expandItem(rootTreeItem);

	if (initialRun) {
		updateAssetView(project->getProjectGuid(), activeFilter, showDependencies);
		rootTreeItem->setSelected(true);
		assetItem.item = rootTreeItem;
		assetItem.selectedGuid = project->getProjectGuid();
	}
}

void AssetWidget::updateTree(QTreeWidgetItem *parent, QString path)
{
	for (const auto &folder : db->fetchChildFolders(path, project->getProjectGuid())) {
		auto item = new QTreeWidgetItem();
		item->setIcon(0, QIcon(":/icons/icons8-folder-72.png"));
		item->setData(0, Qt::DisplayRole, folder.name);
		item->setData(0, MODEL_GUID_ROLE, folder.guid);
		item->setData(0, MODEL_PARENT_ROLE, folder.parent);
		parent->addChild(item);
		// Add children if any
		updateTree(item, folder.guid);
	}
}

// Use this a force thumbnail generator in the future
void AssetWidget::generateAssetThumbnails()
{
	//foreach (auto asset, AssetManager::assets) {
	//    if (asset->type == AssetType::Object) {
	//        // TODO - fetch a list and check that instead of hitting the db, low cost but better way
	//        if (!db->hasCachedThumbnail(asset->fileName)) {
	//            ThumbnailGenerator::getSingleton()->requestThumbnail(
	//                ThumbnailRequestType::Mesh, asset->path, asset->path
	//            );
	//        }
	//    }
	//}
}

void AssetWidget::addItem(const FolderRecord &folderData)
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

	ui->assetView->addItem(item);
}

void AssetWidget::addItem(const AssetRecord &assetData)
{
    auto prop = QJsonDocument::fromJson(assetData.properties).object();
    if (!prop["type"].toString().isEmpty()) {
        // No need to check further, this is a builtin asset
        return;
    }

	QListWidgetItem *item = new QListWidgetItem;
	item->setData(Qt::DisplayRole, QFileInfo(assetData.name).baseName());
    item->setData(Qt::UserRole, assetData.name);
    item->setData(MODEL_TYPE_ROLE, assetData.type);
	item->setData(MODEL_ITEM_TYPE, MODEL_ASSET);
	item->setData(MODEL_GUID_ROLE, assetData.guid);
	item->setData(MODEL_PARENT_ROLE, assetData.parent);

    QPixmap thumbnail;
    if (thumbnail.loadFromData(assetData.thumbnail, "PNG")) {
        item->setIcon(QIcon(thumbnail));
    }
    else {
        item->setIcon(QIcon(":/icons/empty_object.png"));
    }

	if (assetData.type == static_cast<int>(ModelTypes::Texture)) {

	}

	if (assetData.type == static_cast<int>(ModelTypes::Sky)) {
		int skyType = prop.value("sky").toObject().value("type").toInt();
		item->setData(SKY_TYPE_ROLE, skyType);
		item->setData(MODEL_TYPE_ROLE, assetData.type);
		item->setIcon(QIcon(":/icons/icons8-file-sky.png"));
	}

	if (assetData.type == static_cast<int>(ModelTypes::Music)) {
		item->setData(MODEL_TYPE_ROLE, assetData.type);
		item->setIcon(QIcon(":/icons/icons8-file-music.png"));
	}

    if (assetData.type == static_cast<int>(ModelTypes::Shader)) {
        item->setData(MODEL_TYPE_ROLE, assetData.type);
		if(thumbnail.loadFromData(assetData.thumbnail, "PNG"))   item->setIcon(QIcon(thumbnail));
		else item->setIcon(QIcon(":/icons/icons8-file-72.png"));
    }

    if (assetData.type == static_cast<int>(ModelTypes::ParticleSystem)) {
        item->setData(MODEL_TYPE_ROLE, assetData.type);
        item->setIcon(QIcon(":/icons/icons8-file-72-ps.png"));
    }

    if (assetData.type == static_cast<int>(ModelTypes::File)) {
        item->setData(MODEL_TYPE_ROLE, assetData.type);
        // TODO - make this some generic value all assets can use
        //item->setData(MODEL_MESH_ROLE, shaderAssetName.name);
        item->setIcon(QIcon(":/icons/icons8-file-72-file.png"));
    }
	
    if (assetData.type == static_cast<int>(ModelTypes::Material)) {
		item->setData(MODEL_TYPE_ROLE, assetData.type);
	}
	
    if (assetData.type == static_cast<int>(ModelTypes::Object)) {
		const QString meshAssetGuid =
            db->getDependencyByType(static_cast<int>(ModelTypes::Mesh), assetData.guid);
		item->setData(MODEL_TYPE_ROLE, assetData.type);
		item->setData(MODEL_MESH_ROLE, db->fetchAsset(meshAssetGuid).name);
	}

	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setFlags(item->flags() | Qt::ItemIsEditable);

	// Hide meshes for now, we work with objects which are parents for meshes, materials etc
	ui->assetView->addItem(item);
}

void AssetWidget::addCrumbs(const QVector<FolderRecord> &folderData)
{
	breadCrumbLayout->setAlignment(Qt::AlignLeft);

	while (QLayoutItem* item = breadCrumbLayout->takeAt(0)) {
		Q_ASSERT(!item->layout()); // otherwise the layout will leak
		delete item->widget();
		delete item;
	}

	for (const auto &folder : folderData) {
		QPushButton *crumb = new QPushButton(folder.name);
		crumb->setCheckable(true);
		crumb->setCursor(Qt::PointingHandCursor);
		if (&folder == &folderData.back()) {
			crumb->setChecked(true);
		}
		connect(crumb, &QPushButton::pressed, [folder, crumb, this]() {
			assetItem.selectedGuid = folder.guid;
			updateAssetView(folder.guid, activeFilter, showDependencies);
			syncTreeAndView(folder.guid);
		});
		breadCrumbLayout->addWidget(crumb);
	}
}

void AssetWidget::updateAssetView(const QString &path, int filter, bool showDependencies)
{
	ui->assetView->clear();

    if (filter > 0) {
        for (const auto &asset : db->fetchChildAssets(path, project->getProjectGuid(), filter, showDependencies)) addItem(asset);
    }
    else {
        for (const auto &folder : db->fetchChildFolders(path, project->getProjectGuid())) addItem(folder);
        for (const auto &asset : db->fetchChildAssets(path, project->getProjectGuid(), filter, showDependencies)) addItem(asset);  /* TODO : irk this out */
        addCrumbs(db->fetchCrumbTrail(path, project->getProjectGuid()));
    }

    // Reference-with-pin (phase 4): project membership is a project_assets
    // ROW pinning a LIBRARY asset — there is no project-guid clone row for
    // fetchChildAssets to find, so the panel populated from the pre-pin
    // shape showed NOTHING for pinned assets ("Add to Project shows the
    // toast but nothing appears"). Pinned members list at the project ROOT,
    // from the same source as assets.list({scope:'project'}).
    if (path == project->getProjectGuid()) {
        QSet<QString> listed;
        for (int i = 0; i < ui->assetView->count(); ++i)
            listed.insert(ui->assetView->item(i)->data(MODEL_GUID_ROLE).toString());
        for (const auto &pinned :
             db->fetchProjectPinnedAssets(project->getProjectGuid(), showDependencies)) {
            if (listed.contains(pinned.guid)) continue;
            if (filter > 0 && pinned.type != filter) continue;
            addItem(pinned);
        }
    }

    goUpOneControl->setEnabled(false);
}

void AssetWidget::updateAssetContentsView(const QString &guid)
{
    ui->assetView->clear();
    for (const auto &asset : db->fetchAssetsFromParent(guid)) addItem(asset);
}

bool AssetWidget::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == ui->assetView->viewport()) {
		switch (event->type()) {
		    case QEvent::MouseButtonPress: {
			    auto evt = static_cast<QMouseEvent*>(event);
                if (evt->button() == Qt::LeftButton) {
                    startPos = evt->pos();
                    QModelIndex index = ui->assetView->indexAt(evt->pos());
                    if (index.isValid()) draggingItem = true;
                }

			    AssetWidget::mousePressEvent(evt);
			    break;
		    }

		    case QEvent::MouseButtonRelease: {
			    auto evt = static_cast<QMouseEvent*>(event);
                draggingItem = false;
                emit assetItemSelected(nullptr);
                AssetWidget::mouseReleaseEvent(evt);
			    break;
		    }

		    case QEvent::MouseMove: {
			    auto evt = static_cast<QMouseEvent*>(event);
			    if (evt->buttons() & Qt::LeftButton) {
                    if (draggingItem) {
                        int distance = (evt->pos() - startPos).manhattanLength();
                        if (distance >= QApplication::startDragDistance()) {
                            auto item = ui->assetView->currentItem();

                            if (item) {
                                auto drag = QPointer<QDrag>(new QDrag(this));
                                auto mimeData = QPointer<QMimeData>(new QMimeData);

                                QByteArray mdata;
                                QDataStream stream(&mdata, QIODevice::WriteOnly);
                                QMap<int, QVariant> roleDataMap;

                                roleDataMap[0] = QVariant(item->data(MODEL_TYPE_ROLE).toInt());
                                roleDataMap[1] = QVariant(item->data(Qt::UserRole).toString());
                                roleDataMap[2] = QVariant(item->data(MODEL_MESH_ROLE).toString());
                                roleDataMap[3] = QVariant(item->data(MODEL_GUID_ROLE).toString());

                                stream << roleDataMap;

                                mimeData->setData(QString("application/x-qabstractitemmodeldatalist"), mdata);
                                drag->setMimeData(mimeData);

                                drag->setPixmap(item->icon().pixmap(64, 64));
                                drag->exec();
                            }
                        }
                    }
			    }

			    AssetWidget::mouseMoveEvent(evt);
			    break;
		    }

		    default: break;
		}
	}

	return QObject::eventFilter(watched, event);
}

void AssetWidget::dragEnterEvent(QDragEnterEvent *evt)
{
	if (evt->mimeData()->hasUrls()) {
		evt->acceptProposedAction();
	}
}

void AssetWidget::dropEvent(QDropEvent *evt)
{
	QList<QUrl> droppedUrls = evt->mimeData()->urls();
	QStringList list;
	for (auto url : droppedUrls) {
		auto fileInfo = QFileInfo(url.toLocalFile());
		list << fileInfo.absoluteFilePath();
	}

	if (!list.isEmpty()) importAsset(list);

	evt->acceptProposedAction();
}

void AssetWidget::treeItemSelected(QTreeWidgetItem *item)
{
	assetItem.item = item;
	assetItem.selectedGuid = item->data(0, MODEL_GUID_ROLE).toString();
	updateAssetView(item->data(0, MODEL_GUID_ROLE).toString(), activeFilter, showDependencies);
}

void AssetWidget::treeItemChanged(QTreeWidgetItem *item, int column)
{

}

void AssetWidget::updateAssetSkyItemFromSkyPropertyWidget(const QString &guid, iris::SkyType skyType)
{
	for (int i = 0; i < ui->assetView->count(); i++) {
		QListWidgetItem* item = ui->assetView->item(i);
		if (item->data(MODEL_GUID_ROLE).toString() == guid) {
			item->setData(SKY_TYPE_ROLE, static_cast<int>(skyType));
			return;
		}
	}
}

void AssetWidget::sceneTreeCustomContextMenu(const QPoint& pos)
{
	QModelIndex index = ui->assetTree->indexAt(pos);

	if (!index.isValid()) return;

	assetItem.item = ui->assetTree->itemAt(pos);
	assetItem.selectedPath = assetItem.item->data(0, Qt::UserRole).toString();

	QMenu menu;
	menu.setStyleSheet(StyleSheet::QMenuDark());

	QAction *action;

	QMenu *createMenu = menu.addMenu("Create");
	action = new QAction(QIcon(), "Shader", this);
	connect(action, SIGNAL(triggered()), this, SLOT(createShader()));
	createMenu->addAction(action);

    action = new QAction(QIcon(), "Sky", this);
    connect(action, SIGNAL(triggered()), this, SLOT(createSky()));
    createMenu->addAction(action);

    action = new QAction(QIcon(), "New Folder", this);
	connect(action, SIGNAL(triggered()), this, SLOT(createFolder()));
	createMenu->addAction(action);

	//    action = new QAction(QIcon(), "Open in Explorer", this);
	//    connect(action, SIGNAL(triggered()), this, SLOT(openAtFolder()));
	//    menu.addAction(action);

	action = new QAction(QIcon(), "Import Asset", this);
	connect(action, SIGNAL(triggered()), this, SLOT(importAsset()));
	menu.addAction(action);

	//    action = new QAction(QIcon(), "Rename", this);
	//    connect(action, SIGNAL(triggered()), this, SLOT(renameTreeItem()));
	//    menu.addAction(action);

	action = new QAction(QIcon(), "Delete", this);
	connect(action, SIGNAL(triggered()), this, SLOT(deleteTreeFolder()));
	menu.addAction(action);

	menu.exec(ui->assetTree->mapToGlobal(pos));
}

void AssetWidget::exportSky()
{
    // get the export file path from a save dialog
    auto filePath = QFileDialog::getSaveFileName(
        this,
        "Choose export path",
        assetItem.wItem->data(Qt::DisplayRole).toString(),
        "Supported Export Formats (*.jaf)");

    if (filePath.isEmpty() || filePath.isNull())
        return;

    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid())
        return;

    const QString writePath = temporaryDir.path();

    const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();

    db->createBlobFromAsset(guid, QDir(writePath).filePath("asset.db"));

    QDir tempDir(writePath);
    tempDir.mkpath("assets");

    QFile manifest(QDir(writePath).filePath(".manifest"));
    if (manifest.open(QIODevice::ReadWrite))
    {
        QTextStream stream(&manifest);
        stream << "sky";
    }
    manifest.close();

    for (const auto &assetGuid : AssetHelper::fetchAssetAndAllDependencies(guid, db))
    {
        QString name;
        const QString assetPath = resolvePinnedAssetPath(project, assetGuid, &name);
        if (assetPath.isEmpty()) continue;
        if (name.isEmpty()) name = db->fetchAsset(assetGuid).name;
        if (name.isEmpty()) name = QFileInfo(assetPath).fileName();
        QFile::copy(assetPath, IrisUtils::join(writePath, "assets", name));
    }

    // ONE zip loop (amendment 7): the shared helper replaces the
    // hand-rolled zip_entry sweep this site duplicated.
    ZipHelper::zipDirectory(writePath, filePath);
}

void AssetWidget::sceneViewCustomContextMenu(const QPoint& pos)
{
	QModelIndex index = ui->assetView->indexAt(pos);

	QMenu menu;
	menu.setStyleSheet(StyleSheet::QMenuDark());
	QAction *action;

	if (index.isValid()) {
		auto item = ui->assetView->itemAt(pos);
		assetItem.wItem = item;

		action = new QAction(QIcon(), "Rename", this);
		connect(action, SIGNAL(triggered()), this, SLOT(renameViewItem()));
		menu.addAction(action);

        if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Object) ||
            item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Material))
        {
            action = new QAction(QIcon(), "Favorite Asset", this);
            connect(action, SIGNAL(triggered()), this, SLOT(favoriteItem()));
            menu.addAction(action);

            action = new QAction(QIcon(), "Refresh Thumbnail", this);
            connect(action, SIGNAL(triggered()), this, SLOT(refreshThumbnail()));
            menu.addAction(action);
        }

        if (ui->assetView->selectedItems().count() > 1) {
            action = new QAction(QIcon(), "Export Asset Pack", this);
            connect(action, SIGNAL(triggered()), this, SLOT(exportAssetPack()));
            menu.addAction(action);
        }

        if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Texture)) {
            action = new QAction(QIcon(), "Export Texture", this);
            connect(action, SIGNAL(triggered()), this, SLOT(exportTexture()));
            menu.addAction(action);
        }

        if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Sky)) {
            action = new QAction(QIcon(), "Export Sky", this);
            connect(action, SIGNAL(triggered()), this, SLOT(exportSky()));
            menu.addAction(action);
        }

		if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Material)) {
			action = new QAction(QIcon(), "Export Material", this);
			connect(action, SIGNAL(triggered()), this, SLOT(exportMaterial()));
			menu.addAction(action);

            if (QGuiApplication::queryKeyboardModifiers() == Qt::ShiftModifier) {
                action = new QAction(QIcon(), "Save Material Preview", this);
                connect(action, SIGNAL(triggered()), this, SLOT(exportMaterialPreview()));
                menu.addAction(action);
            }
		}

        if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Shader) ||
            item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::File))
        {
            action = new QAction(QIcon(), "Edit", this);
            connect(action, SIGNAL(triggered()), this, SLOT(editFileExternally()));
            menu.addAction(action);
        }

		if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Shader)) {
            action = new QAction(QIcon(), "Export Shader", this);
            connect(action, SIGNAL(triggered()), this, SLOT(exportShader()));
            menu.addAction(action);
		}

		action = new QAction(QIcon(), "Delete", this);
		connect(action, SIGNAL(triggered()), this, SLOT(deleteItem()));
		menu.addAction(action);
	}
	else {
		QMenu *createMenu = menu.addMenu("Create");
		action = new QAction(QIcon(), "Shader", this);
		connect(action, SIGNAL(triggered()), this, SLOT(createShader()));
		createMenu->addAction(action);

        action = new QAction(QIcon(), "Sky", this);
        connect(action, SIGNAL(triggered()), this, SLOT(createSky()));
        createMenu->addAction(action);

        action = new QAction(QIcon(), "New Folder", this);
		connect(action, SIGNAL(triggered()), this, SLOT(createFolder()));
		createMenu->addAction(action);

		action = new QAction(QIcon(), "Import Asset", this);
		connect(action, SIGNAL(triggered()), this, SLOT(importAssetB()));
		menu.addAction(action);

		// action = new QAction(QIcon(), "Open in Explorer", this);
		// connect(action, SIGNAL(triggered()), this, SLOT(openAtFolder()));
		// menu.addAction(action);
	}

	menu.exec(ui->assetView->mapToGlobal(pos));
}

void AssetWidget::assetViewClicked(QListWidgetItem *item)
{
    assetItem.wItem = item;
    emit assetItemSelected(item);
}

void AssetWidget::syncTreeAndView(const QString &path)
{
	QTreeWidgetItemIterator it(ui->assetTree);
	while (*it) {
		if ((*it)->data(0, MODEL_GUID_ROLE).toString() == path) {
			ui->assetTree->clearSelection();
			(*it)->setSelected(true);
			ui->assetTree->expandItem((*it));
			ui->assetTree->scrollToItem((*it));
			break;
		}

		++it;
	}
}

void AssetWidget::assetViewDblClicked(QListWidgetItem *item)
{
    if (item->data(MODEL_ITEM_TYPE) == MODEL_ASSET) {
        //if (item->data(MODEL_TYPE_ROLE) == static_cast<int>(ModelTypes::Shader)) {
        //    editFileExternally();
        //}

        //if (item->data(MODEL_TYPE_ROLE) == static_cast<int>(ModelTypes::File)) {
        //    editFileExternally();
        //}

        //// Maybe  have an internal viewer?
        //if (item->data(MODEL_TYPE_ROLE) == static_cast<int>(ModelTypes::Texture)) {
        //    QDesktopServices::openUrl(QUrl(
        //        IrisUtils::join(
        //            project->getProjectFolder(), "Textures",
        //            db->fetchAsset(item->data(MODEL_GUID_ROLE).toString()).name
        //        )
        //    ));
        //}

        // If item has dependencies
        const QString guid = item->data(MODEL_GUID_ROLE).toString();
        if (!db->hasDependencies(guid)) return;
        assetItem.selectedGuid = guid;
        updateAssetContentsView(guid);
        goUpOneControl->setEnabled(true);
        //syncTreeAndView(guid);
    } else if (item->data(MODEL_ITEM_TYPE) == MODEL_FOLDER) {
        const QString guid = item->data(MODEL_GUID_ROLE).toString();
        assetItem.selectedGuid = guid;
        updateAssetView(guid, activeFilter, showDependencies);
        syncTreeAndView(guid);
    }
}

void AssetWidget::updateAssetItem()
{

}

void AssetWidget::renameTreeItem()
{

}

void AssetWidget::renameViewItem()
{
	ui->assetView->editItem(assetItem.wItem);
}

void AssetWidget::favoriteItem()
{
    mainWindow->favoriteItem(assetItem.wItem);
}

void AssetWidget::refreshThumbnail()
{
    mainWindow->refreshThumbnail(assetItem.wItem);
}

void AssetWidget::editFileExternally()
{
	//for (auto asset : AssetManager::getAssets()) {
 //       if (asset->type == ModelTypes::File) {
 //           if (asset->fileName == assetItem.wItem->text()) {
 //               auto editor = SettingsManager::getDefaultManager()->getValue("editor_path", "");
 //               if (!editor.toString().isEmpty()) {
 //                   QProcess *process = new QProcess(this);
 //                   QStringList argument;
 //                   argument << asset->path;
 //                   process->start(editor.toString(), argument);
 //               }
 //               else {
 //                   QDesktopServices::openUrl(QUrl(asset->path));
 //               }
 //           }
 //       }
	//	else if (asset->type == ModelTypes::Shader) {
			//if (asset->fileName == assetItem.wItem->text()) {
   //             auto editor = SettingsManager::getDefaultManager()->getValue("editor_path", "");
   //             if (!editor.toString().isEmpty()) {
   //                 QProcess *process = new QProcess(this);
   //                 QStringList argument;
   //                 argument << asset->path;
   //                 process->start(editor.toString(), argument);
   //             }
   //             else {
   //                 QDesktopServices::openUrl(QUrl(asset->path));
   //             }
			//}
	//	}
	//}
}

void AssetWidget::exportTexture()
{
    // get the export file path from a save dialog
    auto filePath = QFileDialog::getSaveFileName(
        this,
        "Choose export path",
        assetItem.wItem->data(Qt::DisplayRole).toString() + "_texture",
        "Supported Export Formats (*.jaf)"
    );

    if (filePath.isEmpty() || filePath.isNull()) return;

    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) return;

    const QString writePath = temporaryDir.path();

    const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();

    db->createBlobFromAsset(guid, QDir(writePath).filePath("asset.db"));

    QDir tempDir(writePath);
    tempDir.mkpath("assets");

    QFile manifest(QDir(writePath).filePath(".manifest"));
    if (manifest.open(QIODevice::ReadWrite)) {
        QTextStream stream(&manifest);
        stream << "texture";
    }
    manifest.close();

    QStringList fullFileList = db->fetchAssetAndDependencies(guid);
    auto shaderGuid = QJsonDocument::fromJson(db->fetchAssetData(guid)).object()["guid"].toString();
    bool exportCustomShader = false;
    QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
    while (it.hasNext()) {
        it.next();
        if (it.key() != shaderGuid) {
            exportCustomShader = true;
            break;
        }
    }
    if (exportCustomShader) fullFileList.append(db->fetchAssetAndDependencies(shaderGuid));

    for (const auto &asset : fullFileList) {
        // Name-listed dependency files resolve name -> guid -> pinned bytes.
        const QString depGuid = db->fetchAssetGUIDByName(asset, project->getProjectGuid());
        const QString assetPath = depGuid.isEmpty() ? QString()
                                                    : resolvePinnedAssetPath(project, depGuid, nullptr);
        if (assetPath.isEmpty()) continue;
        QFile::copy(assetPath, IrisUtils::join(writePath, "assets", QFileInfo(asset).fileName()));
    }

    // ONE zip loop (amendment 7): the shared helper replaces the
    // hand-rolled zip_entry sweep this site duplicated.
    ZipHelper::zipDirectory(writePath, filePath);
}

void AssetWidget::exportMaterial()
{
	// get the export file path from a save dialog
	auto filePath = QFileDialog::getSaveFileName(
		this,
		"Choose export path",
        assetItem.wItem->data(Qt::DisplayRole).toString() + "_material",
		"Supported Export Formats (*.jaf)"
	);

	if (filePath.isEmpty() || filePath.isNull()) return;

	QTemporaryDir temporaryDir;
	if (!temporaryDir.isValid()) return;

	const QString writePath = temporaryDir.path();
	const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();

	db->createBlobFromAsset(guid, QDir(writePath).filePath("asset.db"));

	QDir tempDir(writePath);
	tempDir.mkpath("assets");

	QFile manifest(QDir(writePath).filePath(".manifest"));
	if (manifest.open(QIODevice::ReadWrite)) {
		QTextStream stream(&manifest);
		stream << "material";
	}
	manifest.close();

    QStringList fullFileList = db->fetchAssetAndDependencies(guid);
    auto shaderGuid = QJsonDocument::fromJson(db->fetchAssetData(guid)).object()["guid"].toString();
    bool exportCustomShader = false;
    QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
    while (it.hasNext()) {
        it.next();
        if (it.key() != shaderGuid) {
            exportCustomShader = true;
            break;
        }
    }
    if (exportCustomShader) fullFileList.append(db->fetchAssetAndDependencies(shaderGuid));

    for (const auto &asset : fullFileList) {
        // Name-listed dependency files resolve name -> guid -> pinned bytes.
        const QString depGuid = db->fetchAssetGUIDByName(asset, project->getProjectGuid());
        const QString assetPath = depGuid.isEmpty() ? QString()
                                                    : resolvePinnedAssetPath(project, depGuid, nullptr);
        if (assetPath.isEmpty()) continue;
        QFile::copy(assetPath, IrisUtils::join(writePath, "assets", QFileInfo(asset).fileName()));
    }

    // ONE zip loop (amendment 7): the shared helper replaces the
    // hand-rolled zip_entry sweep this site duplicated.
    ZipHelper::zipDirectory(writePath, filePath);
}

void AssetWidget::exportMaterialPreview()
{
    auto assetGuid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();
    auto materialDef = QJsonDocument::fromJson(db->fetchAssetData(assetGuid)).object();

    QString jsonMaterialString = QJsonDocument(materialDef).toJson();

    for (const auto &value : materialDef) {
        if (value.isString() &&
            !db->fetchAsset(value.toString()).name.isEmpty() &&
            value.toString() != materialDef["guid"].toString())
        {
            jsonMaterialString.replace(value.toString(), QString(db->fetchAsset(value.toString()).name));
        }
    }

    QJsonDocument saveDoc = QJsonDocument::fromJson(jsonMaterialString.toUtf8());

    QString fileName = IrisUtils::join(
        project->getProjectFolder(),
        IrisUtils::buildFileName(db->fetchAsset(assetGuid).name, "material")
    );

    QFile file(fileName);
    file.open(QFile::WriteOnly);
    file.write(saveDoc.toJson());
    file.close();

    ThumbnailGenerator::getSingleton()->requestThumbnail(
        ThumbnailRequestType::Material, fileName, assetGuid, true
    );

    //QFile::remove(fileName);
}

void AssetWidget::exportShader()
{
    // get the export file path from a save dialog
    auto filePath = QFileDialog::getSaveFileName(
        this,
        "Choose export path",
        assetItem.wItem->data(Qt::DisplayRole).toString(),
        "Supported Export Formats (*.jaf)"
    );

    if (filePath.isEmpty() || filePath.isNull()) return;

    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) return;

    const QString writePath = temporaryDir.path();

    const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();

    db->createBlobFromAsset(guid, QDir(writePath).filePath("asset.db"));

    QDir tempDir(writePath);
    tempDir.mkpath("assets");

    QFile manifest(QDir(writePath).filePath(".manifest"));
    if (manifest.open(QIODevice::ReadWrite)) {
        QTextStream stream(&manifest);
        stream << "shader";
    }
    manifest.close();

    for (const auto &assetGuid : AssetHelper::fetchAssetAndAllDependencies(guid, db)) {
        QString name;
        const QString assetPath = resolvePinnedAssetPath(project, assetGuid, &name);
        if (assetPath.isEmpty()) continue;
        if (name.isEmpty()) name = db->fetchAsset(assetGuid).name;
        if (name.isEmpty()) name = QFileInfo(assetPath).fileName();
        QFile::copy(assetPath, IrisUtils::join(writePath, "assets", name));
    }

    // ONE zip loop (amendment 7): the shared helper replaces the
    // hand-rolled zip_entry sweep this site duplicated.
    ZipHelper::zipDirectory(writePath, filePath);
}

void AssetWidget::exportAssetPack()
{
    QDateTime currentDateTime = QDateTime::currentDateTimeUtc();
     // get the export file path from a save dialog
    auto filePath = QFileDialog::getSaveFileName(
        this,
        "Choose export path",
        QString("AssetBundle_%1").arg(QString::number(currentDateTime.toSecsSinceEpoch())),
        "Supported Export Formats (*.jaf)"
    );

    if (filePath.isEmpty() || filePath.isNull()) return;

    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) return;

    const QString writePath = temporaryDir.path();

    QStringList assetGuids;
    for (const auto &item : ui->assetView->selectedItems()) {
        assetGuids << item->data(MODEL_GUID_ROLE).toString();
    }

    db->createExportBundle(assetGuids, QDir(writePath).filePath("asset.db"));

    QDir tempDir(writePath);
    tempDir.mkpath("assets");

    QFile manifest(QDir(writePath).filePath(".manifest"));
    if (manifest.open(QIODevice::ReadWrite)) {
        QTextStream stream(&manifest);
        stream << "bundle\n";
        for (const auto &item : assetGuids) stream << item << "\n";
    }
    manifest.close();

    for (const auto &guid : assetGuids) {
        QDir assetDir(QDir(writePath).filePath("assets"));
        assetDir.mkpath(guid);

        for (const auto &assetGuid : AssetHelper::fetchAssetAndAllDependencies(guid, db)) {
            QString name;
            const QString assetPath = resolvePinnedAssetPath(project, assetGuid, &name);
            if (name.isEmpty()) name = db->fetchAsset(assetGuid).name;
            if (name.isEmpty()) name = QFileInfo(assetPath).fileName();

            if (!assetPath.isEmpty()) {
                QFile::copy(
                    IrisUtils::join(assetPath),
                    IrisUtils::join(assetDir.absolutePath(), guid, name)
                );
            }
        }
    }

    // ONE zip loop (amendment 7): the shared helper replaces the
    // hand-rolled zip_entry sweep this site duplicated.
    ZipHelper::zipDirectory(writePath, filePath);
}

void AssetWidget::searchAssets(QString searchString)
{
	// Type-to-search filter (was an empty stub — any query showed NOTHING):
	// shows every matching asset in the currently selected folder and its
	// subfolders, honoring the type filter combo. Clearing the box restores
	// the plain folder view.
	const QString needle = searchString.trimmed();
	if (needle.isEmpty()) {
		updateAssetView(assetItem.selectedGuid, activeFilter, showDependencies);
		return;
	}

	ui->assetView->clear();
	std::function<void(const QString &)> addMatches = [&](const QString &folderGuid) {
		for (const auto &folder : db->fetchChildFolders(folderGuid, project->getProjectGuid())) {
			if (folder.name.contains(needle, Qt::CaseInsensitive)) addItem(folder);
			addMatches(folder.guid);
		}
		for (const auto &asset : db->fetchChildAssets(folderGuid, project->getProjectGuid(),
		                                              activeFilter, showDependencies)) {
			if (asset.name.contains(needle, Qt::CaseInsensitive)) addItem(asset);
		}
	};
	addMatches(assetItem.selectedGuid);
}

void AssetWidget::OnLstItemsCommitData(QWidget *listItem)
{
	QString newName = qobject_cast<QLineEdit*>(listItem)->text();
	const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();
	const QString oldName = db->fetchAsset(guid).name;

    if (!newName.isEmpty()) {
        if (assetItem.wItem->data(MODEL_ITEM_TYPE) == MODEL_ASSET) {
			QString newFileName = IrisUtils::buildFileName(newName, QFileInfo(oldName).suffix());
            db->renameAsset(guid, newFileName);
			QFile assetToRename(QDir(project->getProjectFolder()).filePath(oldName));
			//if (!assetToRename.exists()) return;
			if (!assetToRename.rename(QDir(project->getProjectFolder()).filePath(newFileName))) {
				if (rename(
					QDir(project->getProjectFolder()).filePath(oldName).toStdString().c_str(),
					QDir(project->getProjectFolder()).filePath(newFileName).toStdString().c_str()
				)) {
					for (auto &asset : AssetManager::getAssets()) {
                        if (asset->assetGuid == guid) {
                            asset->fileName = newFileName;
                            if (!asset->path.isEmpty()) {
                                asset->path = QDir(project->getProjectFolder()).filePath(newFileName);
                            }
                        }
					}
				}
            }
            else {
                for (auto &asset : AssetManager::getAssets()) {
                    if (asset->assetGuid == guid) {
                        asset->fileName = newFileName;
                        if (!asset->path.isEmpty()) {
                            asset->path = QDir(project->getProjectFolder()).filePath(newFileName);
                        }
                    }
                }
            }
        }
        else {
            db->renameFolder(guid, newName);
            populateAssetTree(false);
        }
    }
}

void AssetWidget::deleteTreeFolder()
{
	QDir dir(assetItem.selectedPath);
	if (dir.removeRecursively()) {
		auto item = assetItem.item;
		delete item->parent()->takeChild(item->parent()->indexOfChild(item));
	}
}

void AssetWidget::deleteItem()
{
	auto item = assetItem.wItem;

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

            updateAssetView(assetItem.selectedGuid, activeFilter, showDependencies);
            populateAssetTree(false);
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
                updateAssetView(assetItem.selectedGuid, activeFilter, showDependencies);
                populateAssetTree(false);
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
                updateAssetView(assetItem.selectedGuid, activeFilter, showDependencies);
                populateAssetTree(false);
            });
        }

		connect(cancel, &QPushButton::pressed, this, [&dialog]() {
			dialog.close();
		});

		dialog.setStyleSheet(StyleSheet::AssetWidgetTagDialog());

		dialog.exec();
	}
}

void AssetWidget::openAtFolder()
{

}

void AssetWidget::createShader()
{
	const QString newShader = "Untitled Shader";
	QListWidgetItem *item = new QListWidgetItem;
	item->setFlags(item->flags() | Qt::ItemIsEditable);
	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setIcon(QIcon(":/icons/icons8-file-72.png"));

	const QString assetGuid = GUIDManager::generateGUID();

	item->setData(MODEL_GUID_ROLE, assetGuid);
	item->setData(MODEL_PARENT_ROLE, assetItem.selectedGuid);
	item->setData(MODEL_ITEM_TYPE, MODEL_ASSET);
    item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Shader));

	assetItem.wItem = item;

	QString shaderName = newShader;

	QStringList assetsInProject = db->fetchAssetNameByParent(assetItem.selectedGuid);

	//// If we encounter the same file, make a duplicate...
	int increment = 1;
	while (assetsInProject.contains(IrisUtils::buildFileName(shaderName, "shader"))) {
		shaderName = QString(newShader + " %1").arg(QString::number(increment++));
	}

	db->createAssetEntry(assetGuid,
						 IrisUtils::buildFileName(shaderName, "shader"),
						 static_cast<int>(ModelTypes::Shader),
					     assetItem.selectedGuid,
						 project->getProjectGuid(),
						 QByteArray());

	item->setText(shaderName);
	ui->assetView->addItem(item);

	QFile *templateShaderFile = new QFile(IrisUtils::getAbsoluteAssetPath("app/templates/ShaderTemplate.shader"));
	templateShaderFile->open(QIODevice::ReadOnly | QIODevice::Text);
	QJsonObject shaderDefinition = QJsonDocument::fromJson(templateShaderFile->readAll()).object();
	templateShaderFile->close();
    shaderDefinition["name"] = shaderName;
    shaderDefinition.insert("guid", assetGuid);

    auto assetShader = new AssetShader;
    assetShader->fileName = IrisUtils::buildFileName(shaderName, "shader");
    assetShader->assetGuid = assetGuid;
    //assetShader->path = IrisUtils::join(project->getProjectFolder(), IrisUtils::buildFileName(shaderName, "shader"));
    assetShader->setValue(QVariant::fromValue(shaderDefinition));

    // Write to project dir, and update the path to that location
    //QFile jsonFile(assetShader->path);
    //jsonFile.open(QFile::WriteOnly);
    //jsonFile.write(QJsonDocument(shaderDefinition).toJson());

    db->updateAssetAsset(assetGuid, QJsonDocument(shaderDefinition).toJson());

    AssetManager::addAsset(assetShader);
}

void AssetWidget::createSky()
{
    QListWidgetItem *item = new QListWidgetItem;
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    item->setSizeHint(currentSize);
    item->setTextAlignment(Qt::AlignCenter);
    item->setIcon(QIcon(":/icons/icons8-file-sky.png"));

    const QString assetGuid = GUIDManager::generateGUID();

    item->setData(MODEL_GUID_ROLE, assetGuid);
    item->setData(MODEL_PARENT_ROLE, assetItem.selectedGuid);
    item->setData(MODEL_ITEM_TYPE, MODEL_ASSET);
    item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Sky));
    item->setData(SKY_TYPE_ROLE, static_cast<int>(iris::SkyType::SINGLE_COLOR));

	QJsonObject properties;
	QJsonObject skyProps;
	skyProps.insert("type", item->data(SKY_TYPE_ROLE).toInt());
	properties.insert("sky", skyProps);

	QJsonObject skyDescription;
	// Need to leave the defaut sky properties empty, the widget will set it
	//skyDescription.insert("guid", assetGuid);
	//skyDescription.insert("skyColor", SceneWriter::jsonColor(QColor(255, 255, 255, 255)));

	db->createAssetEntry(
		assetGuid,
		"Sky",
		static_cast<int>(ModelTypes::Sky),
		project->getProjectGuid(),
		project->getProjectGuid(),
		QString(),
		QString(),
		AssetHelper::makeBlobFromPixmap(QPixmap(":/icons/icons8-file-sky.png")),
        QJsonDocument(properties).toJson(),
		QByteArray(),
        QJsonDocument(skyDescription).toJson()
	);

    item->setText("Sky");
    ui->assetView->addItem(item);
}

void AssetWidget::createFolder()
{
	const QString newFolder = "New Folder";
	QListWidgetItem *item = new QListWidgetItem;
	item->setFlags(item->flags() | Qt::ItemIsEditable);
	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setIcon(QIcon(":/icons/icons8-folder-72.png"));

	item->setData(MODEL_GUID_ROLE, GUIDManager::generateGUID());
	item->setData(MODEL_PARENT_ROLE, assetItem.selectedGuid);
	item->setData(MODEL_ITEM_TYPE, MODEL_FOLDER);

	assetItem.wItem = item;

	QString folderName = newFolder;

	QStringList foldersInProject = db->fetchFolderNameByParent(assetItem.selectedGuid);

	// If we encounter the same file, make a duplicate...
	int increment = 1;
	while (foldersInProject.contains(folderName)) {
		folderName = newFolder + " " + QString::number(increment++);
	}

	const QString guid = item->data(MODEL_GUID_ROLE).toString();
	const QString parent = item->data(MODEL_PARENT_ROLE).toString();

	//// Create a new database entry for the new folder
	db->createFolder(folderName, parent, guid, project->getProjectGuid());

	// Update the tree browser
	QTreeWidgetItem *child = ui->assetTree->currentItem();
	if (child) {    // should always be set but just in case
		auto branch = new QTreeWidgetItem();
		branch->setIcon(0, QIcon(":/icons/icons8-folder-72.png"));
		branch->setText(0, folderName);
		branch->setData(0, MODEL_GUID_ROLE, guid);
		branch->setData(0, MODEL_PARENT_ROLE, parent);
		child->addChild(branch);
		ui->assetTree->clearSelection();
		branch->setSelected(true);
	}

	populateAssetTree(false);
	// We could just addItem but this is by choice and also so we can order folders first
	updateAssetView(assetItem.selectedGuid, activeFilter, showDependencies);
	//syncTreeAndView(assetItem.selectedGuid);
}

void AssetWidget::importAssetB()
{
	auto fileNames = QFileDialog::getOpenFileNames(this, "Import Asset");
	if (!fileNames.isEmpty()) importAsset(fileNames);
}

void AssetWidget::importAsset(const QStringList &fileNames)
{
	// ONE pipeline + reference-with-pin (phases 3+4): a project-panel drop
	// is a library import through AssetImportService followed by a pin into
	// the open project. The old ~700 lines (recursive directory copies into
	// the store, Editor-filter ghost rows, commented-out project copies,
	// a second .jaf importer) died here.
	// Dropped directories expand to their files; each file imports on its
	// own merits (the pipeline stages exactly what each one references).
	QStringList expanded;
	for (const QString &fileName : fileNames) {
		if (QFileInfo(fileName).isDir()) {
			QDirIterator it(fileName, QDir::NoDotAndDotDot | QDir::Files,
			                QDirIterator::Subdirectories);
			while (it.hasNext()) expanded.append(it.next());
		} else {
			expanded.append(fileName);
		}
	}

	if (expanded.isEmpty()) return;

	// THREADED (UI-freeze fix): the pipeline's heavy half runs on
	// ImportBatchRunner's worker with one cancellable dialog for the whole
	// drop; the pin + panel refresh land back here per file / at the end.
	if (importRunner && importRunner->isRunning()) return;

	progressDialog->resetCancel();
	progressDialog->setCancelVisible(true);
	progressDialog->setRange(0, 0);
	progressDialog->setValue(0);
	progressDialog->setLabelText(tr("Preparing import…"));
	progressDialog->setStageText(QString());
	progressDialog->show();

	importRunner = new ImportBatchRunner(db, project, this);
	QVector<ImportRequest> requests;
	for (const QString &fileName : expanded) {
		ImportRequest request;
		request.sourcePath = fileName;
		requests.append(request);
	}
	importRunner->setRequests(requests);

	connect(progressDialog, &ProgressDialog::canceled,
	        importRunner, &ImportBatchRunner::cancel);

	connect(importRunner, &ImportBatchRunner::fileStarted, this,
	        [this](int index, int total, const QString &name) {
		const QString counter =
		    total > 1 ? tr(" (%1 of %2)").arg(index + 1).arg(total) : QString();
		progressDialog->setLabelText(tr("Importing %1%2").arg(name, counter));
		progressDialog->setRange(0, 0);
	});
	connect(importRunner, &ImportBatchRunner::stageProgress, this,
	        [this](int, const QString &stage, int done, int total) {
		progressDialog->setStageText(
		    total > 0 ? QStringLiteral("%1 (%2/%3)…").arg(stage).arg(done + 1).arg(total)
		              : stage + QStringLiteral("…"));
		progressDialog->setRange(0, total);
		if (total > 0) progressDialog->setValue(done);
	});
	connect(importRunner, &ImportBatchRunner::fileFinished, this,
	        [this](int, const ImportRequest &request, const ImportResult &result) {
		if (!result.ok()) {
			if (result.error != QStringLiteral("cancelled"))
				importErrors.append(QStringLiteral("%1: %2").arg(
				    QFileInfo(request.sourcePath).fileName(), result.error));
			return;
		}
		const auto pinned = ProjectAssets::addToProject(result.assetGuid, db, project);
		if (!pinned.ok()) importErrors.append(pinned.error);
	});
	connect(importRunner, &ImportBatchRunner::finished, this, [this](bool cancelled) {
		progressDialog->hide();
		auto *runner = importRunner;
		importRunner = nullptr;
		if (runner) runner->deleteLater();

		if (cancelled)
			progressDialog->setStageText(QString());
		if (!importErrors.isEmpty()) {
			QMessageBox::warning(this, tr("Import"),
			                     importErrors.join(QStringLiteral("\n")), QMessageBox::Ok);
			importErrors.clear();
		}
		populateAssetTree(false);
		updateAssetView(assetItem.selectedGuid, activeFilter, showDependencies);
	});

	importRunner->start();
}

void AssetWidget::onThumbnailResult(ThumbnailResult *result)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);

    if (!result->preview) {
        // Store the render at full size (requests are 512x512): scaling it to
        // the icon height here permanently degraded every stored thumbnail to
        // 72px (ASSETS_AUDIT.md finding 5). Views scale at display time.
        auto thumbnail = QPixmap::fromImage(result->thumbnail);
        thumbnail.save(&buffer, "PNG");

        db->updateAssetThumbnail(result->id, bytes);

        // Refresh the view if we're still there
        for (int i = 0; i < ui->assetView->count(); i++) {
            QListWidgetItem* item = ui->assetView->item(i);
            if (item->data(MODEL_GUID_ROLE).toString() == result->id) {
                updateAssetView(assetItem.selectedGuid, activeFilter, showDependencies);
            }
        }
    }
    else {
        auto thumbnail = QPixmap::fromImage(result->thumbnail).scaledToHeight(512, Qt::SmoothTransformation);
        thumbnail.save(&buffer, "PNG");

        auto filePath = QFileDialog::getSaveFileName(
            this,
            "Choose image path",
            QString("%1_preview.png").arg(QFileInfo(result->path).baseName()),
            "Supported Image Formats (*.jpg, *.png)"
        );

        if (filePath.isEmpty() || filePath.isNull()) return;
        thumbnail.save(filePath);
    }

	delete result;
}
