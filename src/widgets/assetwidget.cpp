#include "assetwidget.h"
#include "ui_assetwidget.h"

#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/materials/custommaterial.h"
#include "../core/thumbnailmanager.h"
#include "../core/project.h"
#include "../globals.h"
#include "../constants.h"
#include "../uimanager.h"
#include "../widgets/sceneviewwidget.h"
#include "../editor/thumbnailgenerator.h"
#include "../core/database/database.h"
#include "../core/guidmanager.h"
#include "../io/scenewriter.h"
#include "assetview.h"
#include "../io/assetmanager.h"

#include <QProgressDialog>
#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QDrag>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QJsonDocument>

#include "../irisgl/src/materials/custommaterial.h"

AssetWidget::AssetWidget(Database *handle, QWidget *parent) : QWidget(parent), ui(new Ui::AssetWidget)
{
    ui->setupUi(this);

    this->db = handle;

    ui->assetView->viewport()->installEventFilter(this);
    ui->assetTree->viewport()->installEventFilter(this);
    ui->assetTree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->assetTree,  SIGNAL(itemClicked(QTreeWidgetItem*, int)),
            this,           SLOT(treeItemSelected(QTreeWidgetItem*)));

    connect(ui->assetTree,  SIGNAL(itemChanged(QTreeWidgetItem*, int)),
            this,           SLOT(treeItemChanged(QTreeWidgetItem*, int)));

    connect(ui->assetTree,  SIGNAL(customContextMenuRequested(const QPoint&)),
            this,           SLOT(sceneTreeCustomContextMenu(const QPoint&)));

    ui->assetView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->assetView->setResizeMode(QListWidget::Adjust);
    ui->assetView->setMovement(QListView::Static);
    ui->assetView->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->assetView->setSelectionMode(QAbstractItemView::SingleSelection);

    ui->assetView->setDragEnabled(true);
    ui->assetView->setDragDropMode(QAbstractItemView::DragDrop);

    connect(ui->assetView,  SIGNAL(itemClicked(QListWidgetItem*)),
            this,           SLOT(assetViewClicked(QListWidgetItem*)));

    connect(ui->assetView,  SIGNAL(customContextMenuRequested(const QPoint&)),
            this,           SLOT(sceneViewCustomContextMenu(const QPoint&)));

    connect(ui->assetView,  SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,           SLOT(assetViewDblClicked(QListWidgetItem*)));

    connect(ui->assetView->itemDelegate(),  &QAbstractItemDelegate::commitData,
            this,                           &AssetWidget::OnLstItemsCommitData);

    connect(ui->searchBar,  SIGNAL(textChanged(QString)),
            this,           SLOT(searchAssets(QString)));

    connect(ui->importBtn,  SIGNAL(pressed()), SLOT(importAssetB()));

    // The signal will be emitted from another thread (Nick)
    connect(ThumbnailGenerator::getSingleton()->renderThread,	SIGNAL(thumbnailComplete(ThumbnailResult*)),
            this,												SLOT(onThumbnailResult(ThumbnailResult*)));

	breadCrumbLayout = new QHBoxLayout;
	breadCrumbLayout->setContentsMargins(4, 0, 4, 0);
	breadCrumbLayout->setSpacing(0);
	ui->breadCrumb->setObjectName(QStringLiteral("BreadCrumb"));
	ui->breadCrumb->setFixedHeight(32);
	ui->breadCrumb->setLayout(breadCrumbLayout);
	ui->breadCrumb->setStyleSheet(
		"QWidget#BreadCrumb { background: #222; border-top: 1px solid black; border-bottom: 1px solid black; }"
		"QPushButton { background-color: #222; padding: 4px 16px; border-right: 1px solid black; color: #999; }"
		"QPushButton:checked { color: white; border-right: 1px solid black; }"
	);

	assetViewToggleButtonGroup = new QButtonGroup;
	toggleIconView = new QPushButton(tr("Icon"));
	toggleIconView->setCheckable(true);
	//localAssetsButton->setIcon(QIcon(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-server-50.png")));
	//localAssetsButton->setIconSize(QSize(16, 16));
	toggleIconView->setCursor(Qt::PointingHandCursor);
	// Todo - use preferences
	toggleIconView->setChecked(true);

	toggleListView = new QPushButton(tr("List"));
	toggleListView->setCheckable(true);
	//onlineAssetsButton->setIcon(QIcon(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-cloud-50.png")));
	//onlineAssetsButton->setIconSize(QSize(16, 16));
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

	iconSize = QSize(72, 96);
	listSize = QSize(12, 12);
	currentSize = iconSize;

	ui->assetView->setViewMode(QListWidget::IconMode);
	ui->assetView->setSpacing(8);
	ui->assetView->setIconSize(currentSize);

	connect(toggleIconView, &QPushButton::pressed, [this]() {
		ui->assetView->setViewMode(QListWidget::IconMode);
		ui->assetView->setSpacing(8);
		currentSize = iconSize;
		ui->assetView->setIconSize(currentSize);
		updateAssetView(assetItem.selectedGuid);
	});

	connect(toggleListView, &QPushButton::pressed, [this]() {
		ui->assetView->setViewMode(QListWidget::ListMode);
		ui->assetView->setSpacing(4);
		currentSize = listSize;
		ui->assetView->setIconSize(currentSize);
		updateAssetView(assetItem.selectedGuid);
	});

	ui->switcher->setLayout(toggleLayout);
	ui->switcher->setObjectName("Switcher");
	ui->switcher->setStyleSheet(
		"QWidget#Switcher { background: #222; border-top: 1px solid black; border-bottom: 1px solid black; }"
		"QPushButton { background-color: #333; padding: 4px 16px; }"
		"QPushButton:checked { background: #2980b9; }"
	);

	progressDialog = new ProgressDialog;
	progressDialog->setLabelText("Importing assets...");
}

void AssetWidget::trigger()
{
    // It's important that this gets called after a project has been loaded (iKlsR)
    populateAssetTree(true);
}

void AssetWidget::updateLabels()
{
    //updateAssetView(assetItem.selectedPath);
    //populateAssetTree(false);
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
					? QDir(QFileInfo(filePath).absolutePath())
					.filePath(materialDefinition[prop->name].toString())
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

iris::SceneNodePtr AssetWidget::extractTexturesAndMaterialFromMesh(
	const QString &filePath,
	QStringList &textureList,
	QJsonObject &material
) const
{
	auto ssource = new iris::SceneSource();
	// load mesh as scene
	auto node = iris::MeshNode::loadAsSceneFragment(filePath, [&](iris::MeshPtr mesh, iris::MeshMaterialData& data)
	{
		auto mat = iris::CustomMaterial::create();

		if (mesh->hasSkeleton())
			mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/DefaultAnimated.shader"));
		else
			mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

		mat->setValue("diffuseColor",	data.diffuseColor);
		mat->setValue("specularColor",	data.specularColor);
		mat->setValue("ambientColor",	QColor(110, 110, 110));	// assume this color, some formats set this to pitch black
		mat->setValue("emissionColor",	data.emissionColor);
		mat->setValue("shininess",		data.shininess);
		mat->setValue("useAlpha",		true);

		if (QFile(data.diffuseTexture).exists() && QFileInfo(data.diffuseTexture).isFile())
			mat->setValue("diffuseTexture", data.diffuseTexture);

		if (QFile(data.specularTexture).exists() && QFileInfo(data.specularTexture).isFile())
			mat->setValue("specularTexture", data.specularTexture);

		if (QFile(data.normalTexture).exists() && QFileInfo(data.normalTexture).isFile())
			mat->setValue("normalTexture", data.normalTexture);

		return mat;
	}, ssource);

	const aiScene *scene = ssource->importer.GetScene();

	QStringList texturesToCopy;

	for (int i = 0; i < scene->mNumMeshes; i++) {
		auto mesh = scene->mMeshes[i];
		auto material = scene->mMaterials[mesh->mMaterialIndex];

		aiString textureName;

		if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
			material->GetTexture(aiTextureType_DIFFUSE, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}

		if (material->GetTextureCount(aiTextureType_SPECULAR) > 0) {
			material->GetTexture(aiTextureType_SPECULAR, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}

		if (material->GetTextureCount(aiTextureType_NORMALS) > 0) {
			material->GetTexture(aiTextureType_NORMALS, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}

		if (material->GetTextureCount(aiTextureType_HEIGHT) > 0) {
			material->GetTexture(aiTextureType_HEIGHT, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}
	}

	textureList = texturesToCopy;
	SceneWriter::writeSceneNode(material, node, false);

	return node;
}

AssetWidget::~AssetWidget()
{
    delete ui;
}

void AssetWidget::populateAssetTree(bool initialRun)
{
    auto rootTreeItem = new QTreeWidgetItem();
    rootTreeItem->setText(0, "Assets");
    rootTreeItem->setIcon(0, QIcon(":/icons/icons8-folder.svg"));
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
		item->setIcon(0, QIcon(":/icons/icons8-folder.svg"));
		item->setData(0, Qt::DisplayRole,	folder.name);
		item->setData(0, MODEL_GUID_ROLE,	folder.guid);
		item->setData(0, MODEL_PARENT_ROLE,	folder.parent);
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

void AssetWidget::addItem(const FolderData &folderData)
{
	QListWidgetItem *item = new QListWidgetItem;
	item->setData(Qt::DisplayRole, folderData.name);
	item->setData(MODEL_ITEM_TYPE, MODEL_FOLDER);
	item->setData(MODEL_GUID_ROLE, folderData.guid);
	item->setData(MODEL_PARENT_ROLE, folderData.parent);

	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setFlags(item->flags() | Qt::ItemIsEditable);
	item->setIcon(QIcon(":/icons/icons8-folder.svg"));

	ui->assetView->addItem(item);
}

void AssetWidget::addItem(const AssetTileData &assetData)
{
	QListWidgetItem *item = new QListWidgetItem;
	item->setData(Qt::DisplayRole, QFileInfo(assetData.name).baseName());
	item->setData(MODEL_ITEM_TYPE, MODEL_ASSET);
	item->setData(MODEL_GUID_ROLE, assetData.guid);
	item->setData(MODEL_PARENT_ROLE, assetData.parent);

	if (assetData.type == static_cast<int>(AssetType::Texture)) {

	}
	else if (assetData.type == static_cast<int>(AssetType::Object)) {
		//item->setData(Qt::UserRole,		file.absoluteFilePath());
		const QString meshAssetGuid = db->getDependencyByType(static_cast<int>(AssetType::Object), assetData.guid);
		const AssetTileData meshAssetName = db->fetchAsset(meshAssetGuid);
		item->setData(MODEL_GUID_ROLE,		assetData.guid);
		item->setData(MODEL_PARENT_ROLE,	assetData.parent);
		item->setData(MODEL_TYPE_ROLE,		assetData.type);
		item->setData(MODEL_MESH_ROLE,		meshAssetName.name);
	}

	QPixmap thumbnail;
	if (thumbnail.loadFromData(assetData.thumbnail, "PNG")) {
		item->setIcon(QIcon(thumbnail));
	}
	else {
		item->setIcon(QIcon(":/icons/icons8-file.svg"));
	}

	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setFlags(item->flags() | Qt::ItemIsEditable);

	// Hide meshes for now, we work with objects which are parents for meshes, materials etc
	if (assetData.type != static_cast<int>(AssetType::Mesh)) {
		ui->assetView->addItem(item);
	}
}

void AssetWidget::addCrumbs(const QVector<FolderData> &folderData)
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

void AssetWidget::updateAssetView(const QString &path)
{
    ui->assetView->clear();

	for (const auto &folder : db->fetchChildFolders(path)) addItem(folder);
	for (const auto &asset : db->fetchChildAssets(path)) addItem(asset);
	addCrumbs(db->fetchCrumbTrail(path));
}

bool AssetWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->assetView->viewport()) {
        switch (event->type()) {
            case QEvent::MouseButtonPress: {
                auto evt = static_cast<QMouseEvent*>(event);
                if (evt->button() == Qt::LeftButton) {
                    startPos = evt->pos();
                }

				ui->assetView->clearSelection();

                AssetWidget::mousePressEvent(evt);
                break;
            }

            case QEvent::MouseMove: {
                auto evt = static_cast<QMouseEvent*>(event);
                if (evt->buttons() & Qt::LeftButton) {
                    int distance = (evt->pos() - startPos).manhattanLength();
                    if (distance >= QApplication::startDragDistance()) {
                        auto item = ui->assetView->currentItem();
						QDrag *drag = new QDrag(this);
						QMimeData *mimeData = new QMimeData;
						//QModelIndex index = ui->assetView->indexAt(evt->pos());
						//auto item = static_cast<QListWidgetItem*>(index.internalPointer());
						ui->assetView->clearSelection();
                        if (item) {
							QByteArray mdata;
							QDataStream stream(&mdata, QIODevice::WriteOnly);
							QMap<int, QVariant> roleDataMap;

							roleDataMap[0] = QVariant(item->data(MODEL_TYPE_ROLE).toInt());
							roleDataMap[1] = QVariant(item->data(Qt::DisplayRole).toString());
							roleDataMap[2] = QVariant(item->data(MODEL_MESH_ROLE).toString());
							roleDataMap[3] = QVariant(item->data(MODEL_GUID_ROLE).toString());

							stream << roleDataMap;

							mimeData->setData(QString("application/x-qabstractitemmodeldatalist"), mdata);
                            drag->setMimeData(mimeData);

                            // only hide for object models
                            //drag->setPixmap(QPixmap());
                            drag->exec();
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

    importAsset(list);

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
    QAction *action;

    QMenu *createMenu = menu.addMenu("Create");
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
    QAction *action;

    if (index.isValid()) {
        auto item = ui->assetView->itemAt(pos);
        assetItem.wItem = item;
        //assetItem.selectedPath = item->data(Qt::UserRole).toString();

        // action = new QAction(QIcon(), "Rename", this);
        // connect(action, SIGNAL(triggered()), this, SLOT(renameViewItem()));
        // menu.addAction(action);

        action = new QAction(QIcon(), "Delete", this);
        connect(action, SIGNAL(triggered()), this, SLOT(deleteItem()));
        menu.addAction(action);
    }
    else {
        QMenu *createMenu = menu.addMenu("Create");
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
	const QString guid = item->data(MODEL_GUID_ROLE).toString();
	assetItem.selectedGuid = guid;
	updateAssetView(guid);
	syncTreeAndView(guid);
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

void AssetWidget::searchAssets(QString searchString)
{
    ui->assetView->clear();

    if (!searchString.isEmpty()) {
		// keep a list of last db fetch in memory OR search entire db...
    } else {
        updateAssetView(assetItem.selectedGuid);
    }
}

void AssetWidget::OnLstItemsCommitData(QWidget *listItem)
{
    QString folderName = qobject_cast<QLineEdit*>(listItem)->text();
	const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();
	const QString parent = assetItem.wItem->data(MODEL_PARENT_ROLE).toString();

	// Create a new database entry for the new folder
	if (!folderName.isEmpty()) {
		db->insertFolder(folderName, parent, guid);
	}

	// Update the tree browser
    QTreeWidgetItem *child = ui->assetTree->currentItem();
    if (child) {    // should always be set but just in case
        auto branch = new QTreeWidgetItem();
        branch->setIcon(0, QIcon(":/icons/ic_folder_large.svg"));
        branch->setText(0, folderName);
        branch->setData(0, MODEL_GUID_ROLE, guid);
		branch->setData(0, MODEL_PARENT_ROLE, parent);
        child->addChild(branch);
        ui->assetTree->clearSelection();
        branch->setSelected(true);
	}
	
	//populateAssetTree(false);
	//updateAssetView(assetItem.selectedGuid);
	//syncTreeAndView(assetItem.selectedGuid);
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
	
	QStringList filesToRemove;

	// Delete folder and contents
	if (item->data(MODEL_ITEM_TYPE).toInt() == MODEL_FOLDER) {
		for (const auto &files : db->deleteFolderAndDependencies(item->data(MODEL_GUID_ROLE).toString())) {
			auto file = QFileInfo(QDir(Globals::project->getProjectFolder()).filePath(files));
			if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
		}
		
		delete ui->assetView->takeItem(ui->assetView->row(item));
	}
	// Delete asset and dependencies
	else if (item->data(MODEL_ITEM_TYPE).toInt() == MODEL_ASSET) {
		for (const auto &files : db->deleteAssetAndDependencies(item->data(MODEL_GUID_ROLE).toString())) {
			auto file = QFileInfo(QDir(Globals::project->getProjectFolder()).filePath(files));
			if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
		}

		delete ui->assetView->takeItem(ui->assetView->row(item));
	}
}

void AssetWidget::openAtFolder()
{

}

void AssetWidget::createFolder()
{
    QListWidgetItem *item = new QListWidgetItem("New Folder");
    item->setFlags(item->flags() | Qt::ItemIsEditable);
	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setIcon(QIcon(":/icons/ic_folder.svg"));

	item->setData(MODEL_GUID_ROLE, GUIDManager::generateGUID());
	item->setData(MODEL_PARENT_ROLE, assetItem.selectedGuid);

	assetItem.wItem = item;
    ui->assetView->addItem(item);
    ui->assetView->editItem(item);
}

void AssetWidget::importAssetB()
{
    auto fileNames = QFileDialog::getOpenFileNames(this, "Import Asset");
    importAsset(fileNames);
}

void AssetWidget::createDirectoryStructure(const QList<directory_tuple> &fileNames)
{
	int counter = 0;
	// If we're loading a single asset, it's likely a single large file, make the progress indeterminate
	int maxRange = fileNames.size() == 1 ? 0 : fileNames.size();
	
	progressDialog->setRange(0, maxRange);
	progressDialog->setValue(0);
	progressDialog->show();
	QApplication::processEvents();

	QList<directory_tuple> imagesInUse;

	foreach (const auto &entry, fileNames) {
		QFileInfo entryInfo(entry.path);

		if (entryInfo.isDir()) {
			db->insertFolder(entryInfo.baseName(), entry.parent_guid, entry.guid);
		}
		else {
			QString fileName;
			QString fileAbsolutePath;

			AssetType type;
			QPixmap thumbnail;

			thumbnail = QPixmap(":/icons/icons8-file.svg");

			QString destDir;

			// handle all model extensions here
			if (Constants::IMAGE_EXTS.contains(entryInfo.suffix().toLower())) {
				auto thumb = ThumbnailManager::createThumbnail(entryInfo.absoluteFilePath(), 512, 512);
				thumbnail = QPixmap::fromImage(*thumb->thumb);
				type = AssetType::Texture;
				destDir = "Textures";
			}
			else if (Constants::MODEL_EXTS.contains(entryInfo.suffix().toLower())) {
				type = AssetType::Mesh;
				destDir = "Models";
			}
			else if (entryInfo.suffix() == Constants::SHADER_EXT) {
				type = AssetType::Shader;
				destDir = "Shaders";
			}
			else if (entryInfo.suffix() == Constants::MATERIAL_EXT) {
				type = AssetType::Material;
				destDir = "Materials";
			}
			// Generally if we don't explicitly specify an extension, don't import or add it to the database (iKlsR)
			else {
				type = AssetType::Invalid;
			}

			auto asset = new AssetVariant;
			asset->type         = type;
			asset->fileName     = entryInfo.fileName();
			asset->path         = entry.path;
			asset->thumbnail    = thumbnail;

			if (asset->type != AssetType::Invalid) {
				QString pathToCopyTo = IrisUtils::join(Globals::project->getProjectFolder(), destDir);
				QString fileToCopyTo = IrisUtils::join(pathToCopyTo, asset->fileName);

				int increment = 1;
				QFileInfo checkFile(fileToCopyTo);
				
				// If we encounter the same file, make a duplicate...
				// Maybe ask the user to replace sometime later on (iKlsR)
				while (checkFile.exists()) {
					QString newName = entryInfo.baseName() + " " + QString::number(increment++);
					checkFile = QFileInfo(IrisUtils::buildFileName(IrisUtils::join(pathToCopyTo, newName), entryInfo.suffix()));
					asset->fileName = checkFile.fileName();
				}

				pathToCopyTo = checkFile.absoluteFilePath();

				QByteArray thumbnailBytes;
				QBuffer buffer(&thumbnailBytes);
				buffer.open(QIODevice::WriteOnly);
				thumbnail.save(&buffer, "PNG");

				const QString assetGuid = db->createAssetEntry(entry.guid,
															   asset->fileName,
															   static_cast<int>(asset->type),
															   entry.parent_guid,
															   thumbnailBytes);

				// Accumulate a list of all the images imported so we can use this to update references
				// If they are used in assets that depend on them such as Materials and Objects
				if (asset->type == AssetType::Texture) {
					directory_tuple dt;
					dt.parent_guid	= entry.parent_guid;
					dt.guid			= entry.guid;
					dt.path			= entryInfo.fileName();
					imagesInUse.append(dt);
				}

				if (asset->type == AssetType::Material) {
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
							db->insertGlobalDependency(static_cast<int>(AssetType::Material), assetGuid, image.guid);
						}
					}
				}

				if (asset->type == AssetType::Mesh) {
					// Copy textures over to project and create embedded node hierarchy
					QJsonObject jsonSceneNode;
					QStringList texturesToCopy;
					this->sceneView->makeCurrent();
					auto scene = extractTexturesAndMaterialFromMesh(asset->path, texturesToCopy, jsonSceneNode);
					this->sceneView->doneCurrent();

					// You can't manipulate Qt's json when nested
					std::function<void(iris::SceneNodePtr&)> updateNodeValues = [&](iris::SceneNodePtr &node) -> void {
						if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
							auto n = node.staticCast<iris::MeshNode>();
							if (QFileInfo(n->meshPath).fileName() == entryInfo.fileName()) {
								n->meshPath = assetGuid;
							}
							auto mat = n->getMaterial().staticCast<iris::CustomMaterial>();
							for (auto prop : mat->properties) {
								if (prop->type == iris::PropertyType::Texture) {
									for (const auto &image : imagesInUse) {
										if (texturesToCopy.contains(QFileInfo(prop->getValue().toString()).fileName()) &&
											(image.path == QFileInfo(prop->getValue().toString()).fileName()))
										{
											mat->setValue(prop->name, image.guid);
										}
									}
								}
							}
						}

						if (node->hasChildren()) {
							for (auto &child : node->children) {
								updateNodeValues(child);
							}
						}
					};

					updateNodeValues(scene);
					QJsonObject newJson;
					SceneWriter::writeSceneNode(newJson, scene, false);

					// Create an actual object from a mesh, objects hold materials
					const QString objectGuid = db->createAssetEntry(GUIDManager::generateGUID(),
																	QFileInfo(asset->fileName).baseName(),
																	static_cast<int>(AssetType::Object),
																	entry.parent_guid,
																	QByteArray(),
																	QByteArray(),
																	QByteArray(),
																	QJsonDocument(newJson).toBinaryData());

					// Add to persistent store
					{
						QVariant variant = QVariant::fromValue(scene);
						auto nodeAsset = new AssetNodeObject;
						nodeAsset->setValue(variant);
						AssetManager::addAsset(objectGuid, nodeAsset);
					}

					// Create dependencies to the object for the textures used
					for (const auto &image : imagesInUse) {
						if (texturesToCopy.contains(QFileInfo(image.path).fileName())) {
							db->insertGlobalDependency(static_cast<int>(AssetType::Material), objectGuid, image.guid);
						}
					}

					// Insert a dependency for the mesh to the object
					db->insertGlobalDependency(static_cast<int>(AssetType::Object), objectGuid, assetGuid);
					// Remove the thumbnail from the object asset
					db->updateAssetAsset(assetGuid, QByteArray());

					ThumbnailGenerator::getSingleton()->requestThumbnail(
						ThumbnailRequestType::Mesh, asset->path, objectGuid
					);
				}

				bool copyFile = QFile::copy(entry.path, pathToCopyTo);

				progressDialog->setLabelText("Copying " + asset->fileName);
				progressDialog->setValue(counter++);
			}
		}
	}

	progressDialog->hide();
}

void AssetWidget::importAsset(const QStringList &path)
{
    QStringList fileNames;
    if (path.isEmpty()) {   // This is hit when we call this function via import menu
        fileNames = QFileDialog::getOpenFileNames(this, "Import Asset");
    } else {
        fileNames = path;
    }

	// Get the entire directory listing if there are nested folders
	std::function<void(QStringList, QString guid, QList<directory_tuple>&)> getImportManifest =
		[&](QStringList files, QString guid, QList<directory_tuple> &items) -> void
	{
		foreach (const QFileInfo &file, files) {
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

	// Path, GUID, Parent
	QList<directory_tuple> finalImportList;

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

	for (const auto &mesh : fileNameList) {
		if (Constants::MODEL_EXTS.contains(QFileInfo(mesh.path).suffix().toLower())) {
			finalImportList.append(mesh);
		}
	}

	createDirectoryStructure(finalImportList);
	populateAssetTree(false);
	updateAssetView(assetItem.selectedGuid);
    //syncTreeAndView(assetItem.selectedGuid);
}

void AssetWidget::onThumbnailResult(ThumbnailResult *result)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	auto thumbnail = QPixmap::fromImage(result->thumbnail);
	thumbnail.save(&buffer, "PNG");

	db->updateAssetThumbnail(result->id, bytes);

	// Refresh the view if we're still there
	for (int i = 0; i < ui->assetView->count(); i++) {
		QListWidgetItem* item = ui->assetView->item(i);
		if (item->data(MODEL_GUID_ROLE).toString() == result->id) {
			updateAssetView(assetItem.selectedGuid);
		}
	}

    delete result;
}