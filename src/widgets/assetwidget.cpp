/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "assetwidget.h"
#include "ui_assetwidget.h"

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

#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/materials/custommaterial.h"
#include "irisgl/src/scenegraph/particlesystemnode.h" 
#include "irisgl/src/scenegraph/scene.h" 
#include "irisgl/src/zip/zip.h"

#include "assetview.h"
#include "constants.h"
#include "globals.h"
#include "uimanager.h"

#include "mainwindow.h"
#include "core/database/database.h"
#include "core/guidmanager.h"
#include "core/project.h"
#include "core/settingsmanager.h"
#include "core/thumbnailmanager.h"
#include "editor/thumbnailgenerator.h"
#include "core/assethelper.h"
#include "io/assetmanager.h"
#include "io/scenewriter.h"
#include "widgets/sceneviewwidget.h"

#include "core/materialpreset.h"
#include "io/materialpresetreader.h"

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

	connect(ui->hideDeps, &QCheckBox::toggled, [this](bool state) {
		hideDependencies = !state;
		updateAssetView(assetItem.selectedGuid, -1, !state);
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

	// The signal will be emitted from another thread (Nick)
	connect(ThumbnailGenerator::getSingleton()->renderThread,   SIGNAL(thumbnailComplete(ThumbnailResult*)),
		    this,                                               SLOT(onThumbnailResult(ThumbnailResult*)));

	breadCrumbLayout = new QHBoxLayout;
	breadCrumbLayout->setSpacing(0);
	ui->breadCrumb->setObjectName(QStringLiteral("BreadCrumb"));
	ui->breadCrumb->setLayout(breadCrumbLayout);
	assetViewToggleButtonGroup = new QButtonGroup;
	toggleIconView = new QPushButton(tr("Icon"));
	toggleIconView->setCheckable(true);
	toggleIconView->setCursor(Qt::PointingHandCursor);
	// Todo - use preferences
	toggleIconView->setChecked(true);

	toggleListView = new QPushButton(tr("List"));
	toggleListView->setCheckable(true);
	toggleListView->setCursor(Qt::PointingHandCursor);

	assetViewToggleButtonGroup->addButton(toggleIconView);
	assetViewToggleButtonGroup->addButton(toggleListView);

	QHBoxLayout *toggleLayout = new QHBoxLayout;
	toggleLayout->setSpacing(0);
	toggleLayout->setSizeConstraint(QLayout::SetFixedSize);
	toggleLayout->addWidget(new QLabel(tr("Display:")));
	toggleLayout->addSpacing(8);
	toggleLayout->addWidget(toggleIconView);
	toggleLayout->addWidget(toggleListView);

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
        updateAssetView(db->fetchAsset(assetItem.selectedGuid).parent, -1, false);
    });

	setMouseTracking(true);
	ui->assetView->setMouseTracking(true);

	ui->assetView->setViewMode(QListWidget::IconMode);
	ui->assetView->setSpacing(4);
	ui->assetView->setIconSize(currentSize);

	connect(toggleIconView, &QPushButton::pressed, [this]() {
		ui->assetView->setViewMode(QListWidget::IconMode);
		ui->assetView->setAlternatingRowColors(false);
		ui->assetView->setSpacing(4);
		currentSize = iconSize;
		ui->assetView->setIconSize(currentSize);
		ui->assetView->setItemDelegate(new ListViewDelegate());
		updateAssetView(assetItem.selectedGuid);
	});

	connect(toggleListView, &QPushButton::pressed, [this]() {
		ui->assetView->setViewMode(QListWidget::ListMode);
		ui->assetView->setAlternatingRowColors(true);
		ui->assetView->setSpacing(0);
		currentSize = listSize;
		ui->assetView->setIconSize(currentSize);
		ui->assetView->setItemDelegate(new QStyledItemDelegate());
		updateAssetView(assetItem.selectedGuid);
	});

	ui->switcher->setLayout(toggleLayout);
	ui->switcher->setObjectName("Switcher");

    filterGroupLayout = new QHBoxLayout;
    filterGroupLayout->setMargin(0);
    filterGroupLayout->setSpacing(0);
    ui->filterWidget->setObjectName(QStringLiteral("FilterWidget"));
    ui->filterWidget->setLayout(filterGroupLayout);

    assetFilterCombo = new QComboBox;
    assetFilterCombo->addItem("All Assets", QVariant::fromValue(static_cast<int>(-1)));
    assetFilterCombo->addItem("Objects", QVariant::fromValue(static_cast<int>(ModelTypes::Object)));
    assetFilterCombo->addItem("Materials", QVariant::fromValue(static_cast<int>(ModelTypes::Material)));
    assetFilterCombo->addItem("Particle Systems", QVariant::fromValue(static_cast<int>(ModelTypes::ParticleSystem)));
    assetFilterCombo->addItem("Shaders", QVariant::fromValue(static_cast<int>(ModelTypes::Shader)));
    assetFilterCombo->addItem("Textures", QVariant::fromValue(static_cast<int>(ModelTypes::Texture)));
    assetFilterCombo->addItem("Files", QVariant::fromValue(static_cast<int>(ModelTypes::File)));

    filterGroupLayout->addWidget(new QLabel("Filter Assets:"));
    filterGroupLayout->addWidget(assetFilterCombo);

    connect<void(QComboBox::*)(int)>(assetFilterCombo, &QComboBox::currentIndexChanged, this, [&](int index) {
        updateAssetView(assetItem.selectedGuid, assetFilterCombo->itemData(index).toInt());
    });

    ui->filterWidget->setStyleSheet(
        "#filterPane { background: #1E1E1E; border-bottom: 1px solid #111; }"
        "QLabel { font-size: 12px; margin-right: 8px; }"
        "QPushButton[accessibleName=\"filterObj\"] { border-radius: 0; padding: 10px 8px; }"
        "QComboBox { background: #222; border-radius: 1px; color: #BBB; padding: 0 12px; min-height: 24px; min-width: 64px; border: 1px solid #111;}"
        "QComboBox::drop-down { border: 0; margin: 0; padding: 0; min-height: 20px; }"
        "QComboBox::down-arrow { image: url(:/icons/down_arrow_check.png); width: 18px; height: 14px; }"
        "QComboBox::down-arrow:!enabled { image: url(:/icons/down_arrow_check_disabled.png); width: 18px; height: 14px; }"
        "QComboBox QAbstractItemView::item { min-height: 28px; selection-background-color: #404040; color: #cecece; }"
        "QComboBox QAbstractItemView { background-color: #1A1A1A; selection-background-color: #404040; border: 0; outline: none; }"
        "QComboBox QAbstractItemView::item:selected { background: #404040; }"
    );

	ui->searchBar->setPlaceholderText(tr("Type to search for assets..."));

	progressDialog = new ProgressDialog;
	progressDialog->setLabelText("Importing assets...");

	setStyleSheet(
		"QWidget#headerTEMP { background: #1A1A1A;}"
		"QWidget#Switcher { background: #1A1A1A; border-top: 1px solid #151515; border-bottom: 1px solid #151515; }"
		"QWidget#Switcher QPushButton { background-color: #333; padding: 4px 16px; }"
        "QWidget#DirControl { background: #1A1A1A; }"
		"QWidget#Switcher QPushButton:checked { background: #2980b9; }"
		"QWidget#BreadCrumb { background: #1A1A1A; border-top: 1px solid #151515; border-bottom: 1px solid #151515; }"
		"QWidget#BreadCrumb QPushButton { background: transparent; padding: 4px 16px;"
		"									border-right: 1px solid black; color: #999; }"
		"QWidget#BreadCrumb QPushButton:checked { color: white; border-right: 1px solid black; }"
        "QWidget#FilterWidget QPushButton:checked { color: white; background: #2980b9; }"
		"QWidget#assetTree { background: #202020; border: 0; }"
		"QWidget#assetView { background: #202020; border: 0; outline: 0; padding: 0; margin: 0; }"
		"QSplitter::handle { width: 1px; background: #151515; }"
		"QLineEdit { border: 0; background: #292929; color: #EEE; padding: 4px 8px; selection-background-color: #404040; color: #EEE; }"
		"QTreeView, QTreeWidget { show-decoration-selected: 1; }"
		"QWidget#assetView { alternate-background-color: #222; selection-background-color: transparent; }"
		"QListView::item:selected { background-color: #303030; }"
		"QListView::item:hover { background-color: #292929; }"
		"QTreeWidget { outline: none; selection-background-color: #404040; color: #EEE; }"
		"QTreeWidget::branch { background-color: #202020; }"
		"QTreeWidget::branch:hover { background-color: #303030; }"
        "QTreeView::branch:open { image: url(:/icons/expand_arrow_open.png); }"
        "QTreeView::branch:closed:has-children { image: url(:/icons/expand_arrow_closed.png); }"
		"QTreeWidget::branch:selected { background-color: #404040; }"
		"QTreeWidget::item:selected { selection-background-color: #404040;"
		"								background: #404040; outline: none; padding: 5px 0; }"
		/* Important, this is set for when the widget loses focus to fill the left gap */
		"QTreeWidget::item:selected:!active { background: #404040; padding: 5px 0; color: #EEE; }"
		"QTreeWidget::item:selected:active { background: #404040; padding: 5px 0; }"
		"QTreeWidget::item { padding: 5px 0; }"
		"QTreeWidget::item:hover { background: #303030; padding: 5px 0; }"
		"QPushButton{ background-color: #333; color: #DEDEDE; border : 0; padding: 4px 16px; }"
		"QPushButton:hover{ background-color: #555; }"
		"QPushButton:pressed{ background-color: #444; }"
        "QPushButton:disabled{ color: #444; }"
	);
}

void AssetWidget::trigger()
{
    auto dir = QDir(IrisUtils::getAbsoluteAssetPath("app/content/materials"));
    auto files = dir.entryInfoList(QStringList(), QDir::Files);

    auto reader = new MaterialPresetReader();

    // needs opengl context so we have to call this after the window is shown...
    sceneView->makeCurrent();
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
    sceneView->doneCurrent();

	// It's important that this gets called after a project has been loaded (iKlsR)
	populateAssetTree(true);

	sceneView->makeCurrent();
	for (auto &asset : AssetManager::getAssets()) {
		if (asset->type == ModelTypes::Object) {
			auto material = db->fetchAssetData(asset->assetGuid);
			auto materialObj = QJsonDocument::fromBinaryData(material);

			auto node = iris::MeshNode::loadAsSceneFragment(QString(), asset->getValue().value<AssimpObject*>()->getSceneData(),
				[&](iris::MeshPtr mesh, iris::MeshMaterialData& data)
			{
				auto mat = iris::CustomMaterial::create();

				if (mesh->hasSkeleton())
					mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/DefaultAnimated.shader"));
				else
					mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

				return mat;
			});

			AssetHelper::updateNodeMaterial(node, materialObj.object());

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
			//						IrisUtils::join(Globals::project->getProjectFolder(), "Textures",
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
	sceneView->doneCurrent();
}

void AssetWidget::refresh()
{
	updateAssetView(assetItem.selectedGuid);
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
	QJsonDocument doc = QJsonDocument::fromBinaryData(blob);
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

AssetWidget::~AssetWidget()
{
	delete ui;
}

void AssetWidget::populateAssetTree(bool initialRun)
{
	auto rootTreeItem = new QTreeWidgetItem();
	rootTreeItem->setText(0, "Assets");
	rootTreeItem->setIcon(0, QIcon(":/icons/icons8-folder-72.png"));
	rootTreeItem->setData(0, MODEL_GUID_ROLE, Globals::project->getProjectGuid());
	updateTree(rootTreeItem, Globals::project->getProjectGuid());

	ui->assetTree->clear();
	ui->assetTree->addTopLevelItem(rootTreeItem);
	ui->assetTree->expandItem(rootTreeItem);

	if (initialRun) {
		updateAssetView(Globals::project->getProjectGuid());
		rootTreeItem->setSelected(true);
		assetItem.item = rootTreeItem;
		assetItem.selectedGuid = Globals::project->getProjectGuid();
	}
}

void AssetWidget::updateTree(QTreeWidgetItem *parent, QString path)
{
	for (const auto &folder : db->fetchChildFolders(path)) {
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
    auto prop = QJsonDocument::fromBinaryData(assetData.properties).object();
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

    if (assetData.type == static_cast<int>(ModelTypes::Shader)) {
        item->setData(MODEL_TYPE_ROLE, assetData.type);
        item->setIcon(QIcon(":/icons/icons8-file-72.png"));
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
			updateAssetView(folder.guid);
			syncTreeAndView(folder.guid);
		});
		breadCrumbLayout->addWidget(crumb);
	}
}

void AssetWidget::updateAssetView(const QString &path, int filter, bool showDependencies)
{
	ui->assetView->clear();

    if (filter > 0) {
        for (const auto &asset : db->fetchChildAssets(path, filter, showDependencies)) addItem(asset);
    }
    else {
        for (const auto &folder : db->fetchChildFolders(path)) addItem(folder);
        for (const auto &asset : db->fetchChildAssets(path, filter, showDependencies)) addItem(asset);  /* TODO : irk this out */
        addCrumbs(db->fetchCrumbTrail(path));
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
	updateAssetView(item->data(0, MODEL_GUID_ROLE).toString());
}

void AssetWidget::treeItemChanged(QTreeWidgetItem *item, int column)
{

}

void AssetWidget::sceneTreeCustomContextMenu(const QPoint& pos)
{
	QModelIndex index = ui->assetTree->indexAt(pos);

	if (!index.isValid()) return;

	assetItem.item = ui->assetTree->itemAt(pos);
	assetItem.selectedPath = assetItem.item->data(0, Qt::UserRole).toString();

	QMenu menu;
	menu.setStyleSheet(
		"QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
		"QMenu::item : disabled { color: #555; }"
	);

	QAction *action;

	QMenu *createMenu = menu.addMenu("Create");
	action = new QAction(QIcon(), "Shader", this);
	connect(action, SIGNAL(triggered()), this, SLOT(createShader()));
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

void AssetWidget::sceneViewCustomContextMenu(const QPoint& pos)
{
	QModelIndex index = ui->assetView->indexAt(pos);

	QMenu menu;
	menu.setStyleSheet(
		"QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
		"QMenu::item : disabled { color: #555; }"
	);
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

    if (item->data(MODEL_TYPE_ROLE) == static_cast<int>(ModelTypes::Shader)) {
        emit assetItemSelected(item);
    }
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
        //            Globals::project->getProjectFolder(), "Textures",
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
        updateAssetView(guid);
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
    auto shaderGuid = QJsonDocument::fromBinaryData(db->fetchAssetData(guid)).object()["guid"].toString();
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
        QFile::copy(
            IrisUtils::join(Globals::project->getProjectFolder(), asset),
            IrisUtils::join(writePath, "assets", QFileInfo(asset).fileName())
        );
    }

    // get all the files and directories in the project working directory
    QDir workingProjectDirectory(writePath);
    QDirIterator projectDirIterator(writePath,
        QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs,
        QDirIterator::Subdirectories);

    QVector<QString> fileNames;
    while (projectDirIterator.hasNext()) fileNames.push_back(projectDirIterator.next());

    //ZipWrapper exportNode("path", "read / write", "folder / file list");
    //exportNode.setOutputPath();
    //exportNode.setMode();
    //exportNode.setCompressionLevel();
    //exportNode.setFolder();
    //exportNode.setFileList();
    //exportNode.createArchive();

    // open a basic zip file for writing, maybe change compression level later (iKlsR)
    struct zip_t *zip = zip_open(filePath.toStdString().c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');

    for (int i = 0; i < fileNames.count(); i++) {
        QFileInfo fInfo(fileNames[i]);

        // we need to pay special attention to directories since we want to write empty ones as well
        if (fInfo.isDir()) {
            zip_entry_open(
                zip,
                /* will only create directory if / is appended */
                QString(workingProjectDirectory.relativeFilePath(fileNames[i]) + "/").toStdString().c_str()
            );
            zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
        }
        else {
            zip_entry_open(
                zip,
                workingProjectDirectory.relativeFilePath(fileNames[i]).toStdString().c_str()
            );
            zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
        }

        // we close each entry after a successful write
        zip_entry_close(zip);
    }

    // close our now exported file
    zip_close(zip);
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
    auto shaderGuid = QJsonDocument::fromBinaryData(db->fetchAssetData(guid)).object()["guid"].toString();
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
		QFile::copy(
			IrisUtils::join(Globals::project->getProjectFolder(), asset),
			IrisUtils::join(writePath, "assets", QFileInfo(asset).fileName())
		);
	}

	// get all the files and directories in the project working directory
	QDir workingProjectDirectory(writePath);
	QDirIterator projectDirIterator(writePath,
		QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs,
		QDirIterator::Subdirectories);

	QVector<QString> fileNames;
	while (projectDirIterator.hasNext()) fileNames.push_back(projectDirIterator.next());

	//ZipWrapper exportNode("path", "read / write", "folder / file list");
	//exportNode.setOutputPath();
	//exportNode.setMode();
	//exportNode.setCompressionLevel();
	//exportNode.setFolder();
	//exportNode.setFileList();
	//exportNode.createArchive();

	// open a basic zip file for writing, maybe change compression level later (iKlsR)
	struct zip_t *zip = zip_open(filePath.toStdString().c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');

	for (int i = 0; i < fileNames.count(); i++) {
		QFileInfo fInfo(fileNames[i]);

		// we need to pay special attention to directories since we want to write empty ones as well
		if (fInfo.isDir()) {
			zip_entry_open(
				zip,
				/* will only create directory if / is appended */
				QString(workingProjectDirectory.relativeFilePath(fileNames[i]) + "/").toStdString().c_str()
			);
			zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
		}
		else {
			zip_entry_open(
				zip,
				workingProjectDirectory.relativeFilePath(fileNames[i]).toStdString().c_str()
			);
			zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
		}

		// we close each entry after a successful write
		zip_entry_close(zip);
	}

	// close our now exported file
	zip_close(zip);
}

void AssetWidget::exportMaterialPreview()
{
    auto assetGuid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();
    auto materialDef = QJsonDocument::fromBinaryData(db->fetchAssetData(assetGuid)).object();

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
        Globals::project->getProjectFolder(),
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
        auto asset = db->fetchAsset(assetGuid);
        auto assetPath = QDir(Globals::project->getProjectFolder()).filePath(asset.name);
        QFileInfo assetInfo(assetPath);
        if (assetInfo.exists()) {
            QFile::copy(
                IrisUtils::join(assetPath),
                IrisUtils::join(writePath, "assets", assetInfo.fileName())
            );
        }
    }

    // get all the files and directories in the project working directory
    QDir workingProjectDirectory(writePath);
    QDirIterator projectDirIterator(
        writePath,
        QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs | QDir::Hidden, QDirIterator::Subdirectories
    );

    QVector<QString> fileNames;
    while (projectDirIterator.hasNext()) fileNames.push_back(projectDirIterator.next());

    // open a basic zip file for writing, maybe change compression level later (iKlsR)
    struct zip_t *zip = zip_open(filePath.toStdString().c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');

    for (int i = 0; i < fileNames.count(); i++) {
        QFileInfo fInfo(fileNames[i]);

        // we need to pay special attention to directories since we want to write empty ones as well
        if (fInfo.isDir()) {
            zip_entry_open(
                zip,
                /* will only create directory if / is appended */
                QString(workingProjectDirectory.relativeFilePath(fileNames[i]) + "/").toStdString().c_str()
            );
            zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
        }
        else {
            zip_entry_open(
                zip,
                workingProjectDirectory.relativeFilePath(fileNames[i]).toStdString().c_str()
            );
            zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
        }

        // we close each entry after a successful write
        zip_entry_close(zip);
    }

    // close our now exported file
    zip_close(zip);
}

void AssetWidget::exportAssetPack()
{
    QDateTime currentDateTime = QDateTime::currentDateTimeUtc();
     // get the export file path from a save dialog
    auto filePath = QFileDialog::getSaveFileName(
        this,
        "Choose export path",
        QString("AssetBundle_%1").arg(QString::number(currentDateTime.toTime_t())),
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
        stream << "bundle" << endl;
        for (const auto &item : assetGuids) stream << item << endl;
    }
    manifest.close();

    for (const auto &guid : assetGuids) {
        QDir assetDir(QDir(writePath).filePath("assets"));
        assetDir.mkpath(guid);

        for (const auto &assetGuid : AssetHelper::fetchAssetAndAllDependencies(guid, db)) {
            auto asset = db->fetchAsset(assetGuid);
            auto assetPath = QDir(Globals::project->getProjectFolder()).filePath(asset.name);
            QFileInfo assetInfo(assetPath);

            if (assetInfo.exists()) {
                QFile::copy(
                    IrisUtils::join(assetPath),
                    IrisUtils::join(assetDir.absolutePath(), guid, assetInfo.fileName())
                );
            }
        }
    }

    // get all the files and directories in the project working directory
    QDir workingProjectDirectory(writePath);
    QDirIterator projectDirIterator(writePath,
        QDir::NoDotAndDotDot | QDir::Files |
        QDir::Dirs | QDir::Hidden,
        QDirIterator::Subdirectories);

    QVector<QString> fileNames;
    while (projectDirIterator.hasNext()) fileNames.push_back(projectDirIterator.next());

    // open a basic zip file for writing, maybe change compression level later (iKlsR)
    struct zip_t *zip = zip_open(filePath.toStdString().c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');

    for (int i = 0; i < fileNames.count(); i++) {
        QFileInfo fInfo(fileNames[i]);

        // we need to pay special attention to directories since we want to write empty ones as well
        if (fInfo.isDir()) {
            zip_entry_open(
                zip,
                /* will only create directory if / is appended */
                QString(workingProjectDirectory.relativeFilePath(fileNames[i]) + "/").toStdString().c_str()
            );
            zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
        }
        else {
            zip_entry_open(
                zip,
                workingProjectDirectory.relativeFilePath(fileNames[i]).toStdString().c_str()
            );
            zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
        }

        // we close each entry after a successful write
        zip_entry_close(zip);
    }

    // close our now exported file
    zip_close(zip);
}

void AssetWidget::searchAssets(QString searchString)
{
	ui->assetView->clear();

	if (!searchString.isEmpty()) {
		// keep a list of last db fetch in memory OR search entire db...
	}
	else {
		updateAssetView(assetItem.selectedGuid);
	}
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
			QFile assetToRename(QDir(Globals::project->getProjectFolder()).filePath(oldName));
			//if (!assetToRename.exists()) return;
			if (!assetToRename.rename(QDir(Globals::project->getProjectFolder()).filePath(newFileName))) {
				if (rename(
					QDir(Globals::project->getProjectFolder()).filePath(oldName).toStdString().c_str(),
					QDir(Globals::project->getProjectFolder()).filePath(newFileName).toStdString().c_str()
				)) {
					for (auto &asset : AssetManager::getAssets()) {
                        if (asset->assetGuid == guid) {
                            asset->fileName = newFileName;
                            if (!asset->path.isEmpty()) {
                                asset->path = QDir(Globals::project->getProjectFolder()).filePath(newFileName);
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
                            asset->path = QDir(Globals::project->getProjectFolder()).filePath(newFileName);
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

            updateAssetView(assetItem.selectedGuid);
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
                    auto file = QFileInfo(QDir(Globals::project->getProjectFolder()).filePath(files));
                    if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
                }

                //delete ui->assetView->takeItem(ui->assetView->row(item));
                updateAssetView(assetItem.selectedGuid);
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

                        auto file = QFileInfo(QDir(Globals::project->getProjectFolder()).filePath(db->fetchAsset(itemGuid).name));
                        if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
                    }
                }

                //delete ui->assetView->takeItem(ui->assetView->row(item));
                updateAssetView(assetItem.selectedGuid);
                populateAssetTree(false);
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
    //assetShader->path = IrisUtils::join(Globals::project->getProjectFolder(), IrisUtils::buildFileName(shaderName, "shader"));
    assetShader->setValue(QVariant::fromValue(shaderDefinition));

    // Write to project dir, and update the path to that location
    //QFile jsonFile(assetShader->path);
    //jsonFile.open(QFile::WriteOnly);
    //jsonFile.write(QJsonDocument(shaderDefinition).toJson());

    db->updateAssetAsset(assetGuid, QJsonDocument(shaderDefinition).toBinaryData());

    AssetManager::addAsset(assetShader);
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
	db->createFolder(folderName, parent, guid);

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
	updateAssetView(assetItem.selectedGuid);
	//syncTreeAndView(assetItem.selectedGuid);
}

void AssetWidget::importAssetB()
{
	auto fileNames = QFileDialog::getOpenFileNames(this, "Import Asset");
	if (!fileNames.isEmpty()) importAsset(fileNames);
}

void AssetWidget::importJafAssets(const QList<directory_tuple> &fileNames)
{
    // Can be a plain zipped file containing an asset or an exported one
    // Check for a manifest file and treat it accordingly
    int counter = 0;
    // If we're loading a single asset, it's likely a single large file, make the progress indeterminate
    int maxRange = fileNames.size() == 1 ? 0 : fileNames.size();

    progressDialog->reset();
    progressDialog->setRange(0, maxRange);
    progressDialog->setValue(0);

    foreach(const auto &entry, fileNames) {
        QFileInfo entryInfo(entry.path);
        // create a temporary directory and extract our project into it
        // we need a sure way to get the project name, so we have to extract it first and check the blob
        QTemporaryDir temporaryDir;
        if (temporaryDir.isValid()) {
            zip_extract(entryInfo.absoluteFilePath().toStdString().c_str(),
                temporaryDir.path().toStdString().c_str(),
                Q_NULLPTR, Q_NULLPTR
            );

            QFile f(QDir(temporaryDir.path()).filePath(".manifest"));
            if (!f.exists()) {
                QMessageBox::warning(
                    this,
                    "Incompatible Asset format",
                    "This asset was made with a deprecated version of Jahshaka\n"
                    "You can extract the contents manually and try importing as regular assets.",
                    QMessageBox::Ok
                );

                continue;
            }

            if (!f.open(QFile::ReadOnly | QFile::Text)) break;
            QTextStream in(&f);
            const QString jafString = in.readLine();
            f.close();

            // Copy assets over to project folder
            // If the file already exists, increment the filename and do the same when inserting the db entry
            // get all the files and directories in the project working directory
            QString assetsDir = QDir(temporaryDir.path()).filePath("assets");
            QDirIterator projectDirIterator(assetsDir, QDir::NoDotAndDotDot | QDir::Files);

            QStringList fileNames;
            while (projectDirIterator.hasNext()) fileNames << projectDirIterator.next();

            // Create a pair that holds the original name and the new name (if any)
            QVector<QPair<QString, QString>> files;	/* original x new */
            QStringList fullFileList;

            for (const auto &image : fileNames) {
                if (Constants::IMAGE_EXTS.contains(QFileInfo(image).suffix().toLower())) {
                    fullFileList.append(image);
                }
            }

            for (const auto &material : fileNames) {
                if (QFileInfo(material).suffix() == Constants::MATERIAL_EXT) {
                    fullFileList.append(material);
                }
            }

            for (const auto &file : fileNames) {
                if (Constants::WHITELIST.contains(QFileInfo(file).suffix().toLower())) {
                    fullFileList.append(file);
                }
            }

            for (const auto &shader : fileNames) {
                if (QFileInfo(shader).suffix() == Constants::SHADER_EXT) {
                    fullFileList.append(shader);
                }
            }

            for (const auto &mesh : fileNames) {
                if (Constants::MODEL_EXTS.contains(QFileInfo(mesh).suffix().toLower())) {
                    fullFileList.append(mesh);
                }
            }

            QMap<QString, QString> guidCompareMap; /* old x new */

            QString placeHolderGuid = GUIDManager::generateGUID();

            for (const auto &file : fullFileList) {
                QFileInfo fileInfo(file);
                ModelTypes jafType = AssetHelper::getAssetTypeFromExtension(fileInfo.suffix().toLower());

                QString pathToCopyTo = Globals::project->getProjectFolder();
                QString fileToCopyTo = IrisUtils::join(pathToCopyTo, fileInfo.fileName());

                int increment = 1;
                QFileInfo checkFile(fileToCopyTo);

                // If we encounter the same file, make a duplicate...
                QString newFileName = fileInfo.fileName();

                while (checkFile.exists()) {
                    QString newName = QString(fileInfo.baseName() + " %1").arg(QString::number(increment++));
                    checkFile = QFileInfo(IrisUtils::buildFileName(
                        IrisUtils::join(pathToCopyTo, newName), fileInfo.suffix())
                    );
                    newFileName = checkFile.fileName();
                }

                files.push_back(QPair<QString, QString>(file, QDir(pathToCopyTo).filePath(newFileName)));
                bool copyFile = QFile::copy(file, QDir(pathToCopyTo).filePath(newFileName));
                progressDialog->setLabelText("Importing " + fileInfo.fileName());

                if (jafType == ModelTypes::File) {
                    auto assetFile = new AssetFile;
                    assetFile->fileName = checkFile.fileName();
                    assetFile->assetGuid = placeHolderGuid;
                    assetFile->path = checkFile.absoluteFilePath();
                    AssetManager::addAsset(assetFile);
                }

                if (jafType == ModelTypes::Texture) {
                    auto assetTexture = new AssetTexture;
                    assetTexture->fileName = checkFile.fileName();
                    assetTexture->assetGuid = placeHolderGuid;
                    assetTexture->path = checkFile.absoluteFilePath();
                    AssetManager::addAsset(assetTexture);
                }

                if (jafType == ModelTypes::Mesh) {
                    this->sceneView->makeCurrent();
                    auto ssource = new iris::SceneSource();
                    // load mesh as scene
                    auto node = iris::MeshNode::loadAsSceneFragment(
                        checkFile.absoluteFilePath(),
                        [&](iris::MeshPtr mesh, iris::MeshMaterialData& data)
                    {
                        auto mat = iris::CustomMaterial::create();
                        if (mesh->hasSkeleton())
                            mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/DefaultAnimated.shader"));
                        else
                            mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));
                        return mat;
                    }, ssource);
                    this->sceneView->doneCurrent();

                    // Add to persistent store
                    QVariant variant = QVariant::fromValue(node);
                    auto nodeAsset = new AssetNodeObject;
                    nodeAsset->assetGuid = placeHolderGuid;	/* temp guid */
                    nodeAsset->path = checkFile.absoluteFilePath();
                    nodeAsset->setValue(variant);
                    AssetManager::addAsset(nodeAsset);
                }

                progressDialog->setValue(counter++);
            }

            QMap<QString, QString> newNames;	/* original x new */
            for (const auto &file : files) {
                newNames.insert(
                    QFileInfo(file.first).fileName(),
                    QFileInfo(file.second).fileName()
                );
            }

            progressDialog->setLabelText("Populating database...");

            // We can discern most types from their extension, we don't store material files so we use the manifest
            ModelTypes jafType;
            if (jafString == "material") {
                jafType = ModelTypes::Material;
            }
            else if (jafString == "texture") {
                jafType = ModelTypes::Texture;
            }
            else if (jafString == "object") {
                jafType = ModelTypes::Object;
            }
            else if (jafString == "shader") {
                jafType = ModelTypes::Shader;
            }
            else if (jafString == "particle_system") {
                jafType = ModelTypes::ParticleSystem;
            }
            else {
                // Default to files since we know what archives can contain
                jafType = ModelTypes::File;
            }

            QVector<AssetRecord> oldAssetRecords;

            QString guidReturned = db->importAsset(
                jafType,
                QDir(temporaryDir.path()).filePath("asset.db"),
                newNames,
                guidCompareMap,
                oldAssetRecords,
                assetItem.selectedGuid
            );

            // The multiple for loops are intentional, don't try to optimize this, each asset type must be updated
            for (auto &asset : AssetManager::getAssets()) {
                if (asset->type == ModelTypes::File) {
                    for (const auto &record : oldAssetRecords) {
                        if (record.name == asset->fileName) {
                            asset->assetGuid = record.guid;
                        }
                    }
                }
            }

            if (jafType == ModelTypes::Texture) {
                for (auto &asset : AssetManager::getAssets()) {
                    if (asset->assetGuid == placeHolderGuid && asset->type == ModelTypes::Texture) {
                        asset->assetGuid = guidReturned;
                    }
                }
            }

            if (jafType == ModelTypes::Shader) {
                QJsonDocument matDoc = QJsonDocument::fromBinaryData(db->fetchAssetData(guidReturned));
                QJsonObject shaderDefinition = matDoc.object();

                auto assetShader = new AssetShader;
                assetShader->assetGuid = guidReturned;
                assetShader->fileName = db->fetchAsset(guidReturned).name;
                assetShader->setValue(QVariant::fromValue(shaderDefinition));
                AssetManager::addAsset(assetShader);
            }
            else {
                for (const auto &asset : oldAssetRecords) {
                    if (asset.type == static_cast<int>(ModelTypes::Shader)) {
                        QJsonDocument matDoc = QJsonDocument::fromBinaryData(asset.asset);
                        QJsonObject shaderDefinition = matDoc.object();

                        auto assetShader = new AssetShader;
                        assetShader->assetGuid = asset.guid;
                        assetShader->fileName = asset.name;
                        assetShader->setValue(QVariant::fromValue(shaderDefinition));
                        AssetManager::addAsset(assetShader);
                    }
                }
            }

            if (jafType == ModelTypes::Material) {
                QJsonDocument matDoc = QJsonDocument::fromBinaryData(db->fetchAssetData(guidReturned));
                QJsonObject matObject = matDoc.object();
                iris::CustomMaterialPtr material = iris::CustomMaterialPtr::create();

                QFileInfo shaderFile;

                QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
                while (it.hasNext()) {
                    it.next();
                    if (it.key() == matObject["guid"].toString()) {
                        shaderFile = QFileInfo(IrisUtils::getAbsoluteAssetPath(it.value()));
                        break;
                    }
                }

                if (shaderFile.exists()) {
                    material->generate(shaderFile.absoluteFilePath());
                }
                else {
                    for (auto asset : AssetManager::getAssets()) {
                        if (asset->type == ModelTypes::Shader) {
                            if (asset->assetGuid == matObject["guid"].toString()) {
                                auto def = asset->getValue().toJsonObject();
                                auto vertexShader = def["vertex_shader"].toString();
                                auto fragmentShader = def["fragment_shader"].toString();
                                for (auto asset : AssetManager::getAssets()) {
                                    if (asset->type == ModelTypes::File) {
                                        if (vertexShader == asset->assetGuid) vertexShader = asset->path;
                                        if (fragmentShader == asset->assetGuid) fragmentShader = asset->path;
                                    }
                                }
                                def["vertex_shader"] = vertexShader;
                                def["fragment_shader"] = fragmentShader;
                                material->generate(def);
                            }
                        }
                    }
                }

                for (const auto &prop : material->properties) {
                    if (prop->type == iris::PropertyType::Color) {
                        QColor col;
                        col.setNamedColor(matObject.value(prop->name).toString());
                        material->setValue(prop->name, col);
                    }
                    else if (prop->type == iris::PropertyType::Texture) {
                        QString materialName = db->fetchAsset(matObject.value(prop->name).toString()).name;
                        QString textureStr = IrisUtils::join(Globals::project->getProjectFolder(), materialName);
                        material->setValue(prop->name, !materialName.isEmpty() ? textureStr : QString());
                    }
                    else {
                        material->setValue(prop->name, QVariant::fromValue(matObject.value(prop->name)));
                    }
                }

                auto assetMat = new AssetMaterial;
                assetMat->assetGuid = guidReturned;
                assetMat->setValue(QVariant::fromValue(material));
                AssetManager::addAsset(assetMat);
            }

            if (jafType == ModelTypes::ParticleSystem) {
                QJsonDocument particleDoc = QJsonDocument::fromBinaryData(db->fetchAssetData(guidReturned));
                QJsonObject particleObject = particleDoc.object();

                auto assetPS = new AssetParticleSystem;
                assetPS->assetGuid = guidReturned;
                assetPS->setValue(QVariant::fromValue(particleObject));
                AssetManager::addAsset(assetPS);
            }

            if (jafType == ModelTypes::Object) {
                for (auto &asset : AssetManager::getAssets()) {
                    if (asset->assetGuid == placeHolderGuid && asset->type == ModelTypes::Object) {
                        asset->assetGuid = guidReturned;
                        auto node = asset->getValue().value<iris::SceneNodePtr>();
                        
                        auto materialObj = QJsonDocument::fromBinaryData(db->fetchAssetData(asset->assetGuid));
                        QJsonObject matObject = materialObj.object();
                        
                        //node.staticCast<iris::MeshNode>()->setMaterial(material);
                        AssetHelper::updateNodeMaterial(node, materialObj.object());
                    }
                }
            }
        }
    }
}

void AssetWidget::importRegularAssets(const QList<directory_tuple> &fileNames)
{
	int counter = 0;
	// If we're loading a single asset, it's likely a single large file, make the progress indeterminate
	int maxRange = fileNames.size() == 1 ? 0 : fileNames.size();

	progressDialog->setRange(0, maxRange);
	progressDialog->setValue(0);
	progressDialog->show();
	QApplication::processEvents();

	QList<directory_tuple> imagesInUse;

	foreach(const auto &entry, fileNames) {
		QFileInfo entryInfo(entry.path);

		if (entryInfo.isDir()) {
			db->createFolder(entryInfo.baseName(), entry.parent_guid, entry.guid);
		}
		else {
			QString fileName;
			QString fileAbsolutePath;

			ModelTypes type;
			QPixmap thumbnail = QPixmap(":/icons/empty_object.png");

			auto asset = new AssetVariant;
			asset->type		 = AssetHelper::getAssetTypeFromExtension(entryInfo.suffix().toLower());
			asset->fileName  = entryInfo.fileName();
			asset->path		 = entry.path;
			asset->thumbnail = thumbnail;

			if (asset->type != ModelTypes::Undefined) {
				QString pathToCopyTo = Globals::project->getProjectFolder();
				QString fileToCopyTo = IrisUtils::join(pathToCopyTo, asset->fileName);

				int increment = 1;
				QFileInfo checkFile(fileToCopyTo);

				// If we encounter the same file, make a duplicate...
				// Maybe ask the user to replace sometime later on (iKlsR)
				while (checkFile.exists()) {
                    // Repeatedly test if a file exists by incrementally adding a numeral to the base name
					QString newName = QString(entryInfo.baseName() + " %1").arg(QString::number(increment++));
					checkFile = QFileInfo(
                        IrisUtils::buildFileName(IrisUtils::join(pathToCopyTo, newName), entryInfo.suffix())
                    );
					asset->fileName = checkFile.fileName();
					fileToCopyTo = checkFile.absoluteFilePath();
				}

                // Accumulate a list of all the images imported so we can use this to update references
                // If they are used in assets that depend on them such as Materials and Objects
                {
                    if (asset->type == ModelTypes::Texture) {
                        auto thumb = ThumbnailManager::createThumbnail(entryInfo.absoluteFilePath(), 72, 72);
                        thumbnail = QPixmap::fromImage(*thumb->thumb);

                        directory_tuple dt;
                        dt.parent_guid = entry.parent_guid;
                        dt.guid = entry.guid;
                        dt.path = entryInfo.fileName();
                        imagesInUse.append(dt);
                    }
                }

				const QString assetGuid = db->createAssetEntry(entry.guid,
															   asset->fileName,
															   static_cast<int>(asset->type),
															   entry.parent_guid,
                                                               QString(),
                                                               QString(),
                                                               AssetHelper::makeBlobFromPixmap(thumbnail));

                if (asset->type == ModelTypes::File) {
                    auto assetFile = new AssetFile;
                    assetFile->assetGuid = assetGuid;
                    assetFile->fileName = asset->fileName;
                    assetFile->path = fileToCopyTo;
                    AssetManager::addAsset(assetFile);
                }

                if (asset->type == ModelTypes::Texture) {
                    auto assetTexture = new AssetTexture;
                    assetTexture->assetGuid = assetGuid;
                    assetTexture->fileName = asset->fileName;
                    assetTexture->path = fileToCopyTo;
                    AssetManager::addAsset(assetTexture);
                }

                if (asset->type == ModelTypes::Shader) {
                    QFile *shaderFile = new QFile(asset->path);
                    shaderFile->open(QIODevice::ReadOnly | QIODevice::Text);
                    QJsonObject shaderDefinition = QJsonDocument::fromJson(shaderFile->readAll()).object();
                    shaderFile->close();

                    shaderDefinition["name"] = QFileInfo(asset->fileName).baseName();
                    shaderDefinition["guid"] = assetGuid;

                    db->updateAssetAsset(assetGuid, QJsonDocument(shaderDefinition).toBinaryData());

                    auto assetShader = new AssetShader;
                    assetShader->assetGuid = assetGuid;
                    assetShader->fileName = QFileInfo(asset->fileName).baseName();
                    assetShader->setValue(QVariant::fromValue(shaderDefinition));
                    AssetManager::addAsset(assetShader);
                }

				if (asset->type == ModelTypes::Material) {
					ThumbnailGenerator::getSingleton()->requestThumbnail(
						ThumbnailRequestType::Material, asset->path, assetGuid
					);

					QJsonObject jsonMaterial;
					QStringList texturesToCopy;
					extractTexturesAndMaterialFromMaterial(asset->path, texturesToCopy, jsonMaterial);

					QString jsonMaterialString = QJsonDocument(jsonMaterial).toJson();

					// Update the embedded material to point to image asset guids
					for (const auto &image : imagesInUse) {
						if (texturesToCopy.contains(QFileInfo(image.path).fileName())) {
							jsonMaterialString.replace(QFileInfo(image.path).fileName(), image.guid);
						}
					}

					QJsonDocument jsonMaterialGuids = QJsonDocument::fromJson(jsonMaterialString.toUtf8());
					db->updateAssetAsset(assetGuid, jsonMaterialGuids.toBinaryData());

					// Create dependencies to the object for the textures used
					for (const auto &image : imagesInUse) {
						if (texturesToCopy.contains(QFileInfo(image.path).fileName())) {
							db->createDependency(
                                static_cast<int>(ModelTypes::Material), static_cast<int>(ModelTypes::Texture),
                                assetGuid, image.guid, Globals::project->getProjectGuid()
                            );
						}
					}

					QJsonDocument matDoc = QJsonDocument::fromBinaryData(db->fetchAssetData(assetGuid));
					QJsonObject matObject = matDoc.object();
					iris::CustomMaterialPtr material = iris::CustomMaterialPtr::create();
					material->generate(IrisUtils::join(
						IrisUtils::getAbsoluteAssetPath(Constants::SHADER_DEFS),
						IrisUtils::buildFileName(matObject.value("name").toString(), "shader"))
					);

					for (const auto &prop : material->properties) {
						if (prop->type == iris::PropertyType::Color) {
							QColor col;
							col.setNamedColor(matObject.value(prop->name).toString());
							material->setValue(prop->name, col);
						}
						else if (prop->type == iris::PropertyType::Texture) {
							QString materialName = db->fetchAsset(matObject.value(prop->name).toString()).name;
							QString textureStr = IrisUtils::join(Globals::project->getProjectFolder(), materialName);
							material->setValue(prop->name, !materialName.isEmpty() ? textureStr : QString());
						}
						else {
							material->setValue(prop->name, QVariant::fromValue(matObject.value(prop->name)));
						}
					}

					auto assetMat = new AssetMaterial;
					assetMat->assetGuid = assetGuid;
					assetMat->setValue(QVariant::fromValue(material));
					AssetManager::addAsset(assetMat);
				}

				if (asset->type == ModelTypes::Mesh) {
					QStringList texturesToCopy;
					this->sceneView->makeCurrent();
					auto scene = AssetHelper::extractTexturesAndMaterialFromMesh(asset->path, texturesToCopy);
					this->sceneView->doneCurrent();

					QString preObjectGuid = GUIDManager::generateGUID();

					// Replace all path references with GUIDs before storing in the database
					std::function<void(iris::SceneNodePtr&)> replacePathsWithGUIDs =
                        [&](iris::SceneNodePtr &node) -> void {
						if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
							auto meshNode = node.staticCast<iris::MeshNode>();
							if (QFileInfo(meshNode->meshPath).fileName() == entryInfo.fileName()) {
                                meshNode->meshPath = assetGuid;
							}

							meshNode->setGUID(preObjectGuid);

							auto material = meshNode->getMaterial().staticCast<iris::CustomMaterial>();
							for (auto prop : material->properties) {
								if (prop->type == iris::PropertyType::Texture) {
                                    // Cycle through any textures that were selected in the import and use them
									for (const auto &image : imagesInUse) {
                                        auto fileName = QFileInfo(prop->getValue().toString()).fileName();
										if (texturesToCopy.contains(fileName) && image.path == fileName) {
											material->setValue(prop->name, image.guid);
										}
									}
								}
							}
						}

						if (node->hasChildren()) {
							for (auto &child : node->children) {
                                replacePathsWithGUIDs(child);
							}
						}
					};

                    replacePathsWithGUIDs(scene);

					QJsonObject nodeWithGUIDs;
					SceneWriter::writeSceneNode(nodeWithGUIDs, scene, false);

					// Create an actual object from a mesh, materials are embedded into objects by default
					const QString objectGuid = db->createAssetEntry(preObjectGuid,
																	QFileInfo(asset->fileName).baseName(),
																	static_cast<int>(ModelTypes::Object),
																	entry.parent_guid,
                                                                    QString(),
                                                                    QString(),
																	QByteArray(),
																	QByteArray(),
																	QByteArray(),
																	QJsonDocument(nodeWithGUIDs).toBinaryData());

                    ThumbnailGenerator::getSingleton()->requestThumbnail(
                        ThumbnailRequestType::Mesh, asset->path, objectGuid
                    );

					{
						QVariant variant = QVariant::fromValue(scene);
						auto nodeAsset = new AssetNodeObject;
						nodeAsset->assetGuid = objectGuid;
						nodeAsset->setValue(variant);
						AssetManager::addAsset(nodeAsset);
					}

					// Create dependencies to the object for the textures used
					for (const auto &image : imagesInUse) {
						if (texturesToCopy.contains(QFileInfo(image.path).fileName())) {
							db->createDependency(
                                static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Texture),
                                objectGuid, image.guid, Globals::project->getProjectGuid()
                            );
						}
					}

					// Insert a dependency for the mesh to the object
					db->createDependency(
                        static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh),
                        objectGuid, assetGuid, Globals::project->getProjectGuid()
                    );
					// Remove the thumbnail from the object asset
					db->updateAssetAsset(assetGuid, QByteArray());
				}

				// Copy only models, textures and whitelisted files
				bool copyFile = QFile::copy(entry.path, fileToCopyTo);

				progressDialog->setLabelText("Copying " + asset->fileName);
				progressDialog->setValue(counter++);
			}
		}
	}

	progressDialog->hide();
}

void AssetWidget::importAsset(const QStringList &fileNames)
{
	// Get the entire directory listing if there are nested folders
	std::function<void(QStringList, QString guid, QList<directory_tuple>&)> getImportManifest =
		[&](QStringList files, QString guid, QList<directory_tuple> &items) -> void
	{
		foreach(const QFileInfo &file, files) {
			directory_tuple item;
			item.path = file.absoluteFilePath();
			item.parent_guid = guid;

			if (file.isDir()) {
				QStringList list;
				foreach(const QString &entry,
					QDir(file.absoluteFilePath()).entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs))
				{
					list << QDir(file.absoluteFilePath()).filePath(entry);
				}

				item.guid = GUIDManager::generateGUID();

				getImportManifest(list, item.guid, items);
				items.append(item);
			}
			else {
				item.guid = GUIDManager::generateGUID();
				items.append(item);
			}
		}
	};

	QList<directory_tuple> fileNameList;
	getImportManifest(fileNames, assetItem.selectedGuid, fileNameList);

	// Sort filenames by type, we want to import and create db entries in dependent order
	// 0. Folders
	// 1. Textures
	// 2. Materials
	// 3. Shaders
	// 4. Meshes
	// 5. Asset files (these contain their own assets)
    // 6. Regular files

	// Path, GUID, Parent
	QList<directory_tuple> finalImportList;
    QList<directory_tuple> finalJafAssetImportList;

	for (const auto &folder : fileNameList) {
		if (QFileInfo(folder.path).isDir()) {
			finalImportList.append(folder);
		}
	}

	for (const auto &image : fileNameList) {
		if (Constants::IMAGE_EXTS.contains(QFileInfo(image.path).suffix().toLower())) {
			finalImportList.append(image);
		}
	}

	for (const auto &material : fileNameList) {
		if (QFileInfo(material.path).suffix() == Constants::MATERIAL_EXT) {
			finalImportList.append(material);
		}
	}

    for (const auto &file : fileNameList) {
        if (Constants::WHITELIST.contains(QFileInfo(file.path).suffix().toLower())) {
            finalImportList.append(file);
        }
    }

    for (const auto &shader : fileNameList) {
        if (QFileInfo(shader.path).suffix() == Constants::SHADER_EXT) {
            finalImportList.append(shader);
        }
    }

	for (const auto &mesh : fileNameList) {
		if (Constants::MODEL_EXTS.contains(QFileInfo(mesh.path).suffix().toLower())) {
			finalImportList.append(mesh);
		}
	}

	for (const auto &archive : fileNameList) {
		if (QFileInfo(archive.path).suffix() == Constants::ASSET_EXT) {
            finalJafAssetImportList.append(archive);
		}
	}

	importRegularAssets(finalImportList);
    importJafAssets(finalJafAssetImportList);
	populateAssetTree(false);
	updateAssetView(assetItem.selectedGuid);
	//syncTreeAndView(assetItem.selectedGuid);
}

void AssetWidget::onThumbnailResult(ThumbnailResult *result)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);

    if (!result->preview) {
        auto thumbnail = QPixmap::fromImage(result->thumbnail).scaledToHeight(iconSize.height(), Qt::SmoothTransformation);
        thumbnail.save(&buffer, "PNG");

        db->updateAssetThumbnail(result->id, bytes);

        // Refresh the view if we're still there
        for (int i = 0; i < ui->assetView->count(); i++) {
            QListWidgetItem* item = ui->assetView->item(i);
            if (item->data(MODEL_GUID_ROLE).toString() == result->id) {
                updateAssetView(assetItem.selectedGuid);
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
