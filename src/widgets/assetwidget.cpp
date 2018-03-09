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

#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QDrag>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QJsonDocument>

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
    ui->assetView->setViewMode(QListWidget::IconMode);
    ui->assetView->setIconSize(QSize(88, 88));
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
    connect(ThumbnailGenerator::getSingleton()->renderThread, SIGNAL(thumbnailComplete(ThumbnailResult*)),
            this,                                             SLOT(onThumbnailResult(ThumbnailResult*)));
}

void AssetWidget::trigger()
{
    // generateAssetThumbnails();
    // It's important that this get's called after a project has been loaded (iKlsR)
    //populateAssetTree(true);
	populateAssetView();
}

void AssetWidget::populateAssetView()
{
	updateAssetView(Globals::project->getProjectGuid());
}

void AssetWidget::updateLabels()
{
    //updateAssetView(assetItem.selectedPath);
    //populateAssetTree(false);
}

void AssetWidget::extractTexturesAndMaterialFromMesh(const QString &filePath, QStringList &textureList, QJsonObject &material)
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

		mat->setValue("diffuseColor", data.diffuseColor);
		mat->setValue("specularColor", data.specularColor);
		mat->setValue("ambientColor", QColor(110, 110, 110));	// assume this color, some formats set this to pitch black
		mat->setValue("emissionColor", data.emissionColor);
		mat->setValue("shininess", data.shininess);
		mat->setValue("useAlpha", true);

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

    SceneWriter::writeSceneNode(material, node, false);
	textureList = texturesToCopy;
}

AssetWidget::~AssetWidget()
{
    delete ui;
}

void AssetWidget::populateAssetTree(bool initialRun)
{
    // TODO - revamp this, the actual project directory is up one level. ok
    auto rootTreeItem = new QTreeWidgetItem();
    rootTreeItem->setText(0, "Assets");
    rootTreeItem->setIcon(0, QIcon(":/icons/ic_folder_large.svg"));
    rootTreeItem->setData(0, Qt::UserRole, Globals::project->getProjectFolder());
    updateTree(rootTreeItem, Globals::project->getProjectFolder());

    ui->assetTree->clear();
    ui->assetTree->addTopLevelItem(rootTreeItem);
    ui->assetTree->expandItem(rootTreeItem);

    if (initialRun) {
        updateAssetView(rootTreeItem->data(0, Qt::UserRole).toString());
        rootTreeItem->setSelected(true);
        assetItem.item = rootTreeItem;
        assetItem.selectedPath = rootTreeItem->data(0, Qt::UserRole).toString();
    }
}

void AssetWidget::updateTree(QTreeWidgetItem *parent, QString path)
{
    QFileInfoList folders = QDir(path).entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs);
    foreach (const QFileInfo &folder, folders) {
        auto item = new QTreeWidgetItem();
        item->setIcon(0, QIcon(":/icons/ic_folder_large.svg"));
		if (!Globals::assetNames.value(folder.fileName()).isEmpty()) {
			// item->setData(0, Qt::DisplayRole, Globals::assetNames.value(folder.fileName()));
		}
		else {
			item->setData(0, Qt::DisplayRole, folder.fileName());
			parent->addChild(item);
		}
        item->setData(0, Qt::UserRole, folder.absoluteFilePath());
        updateTree(item, folder.absoluteFilePath());
    }
}

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

void AssetWidget::addItem(const QString &asset)
{
	QPixmap pixmap;
    QFileInfo file(asset);

    QListWidgetItem *item = new QListWidgetItem();

	if (file.isDir()) {
		item->setIcon(QIcon(":/icons/ic_folder.svg"));
	}
	else {
		if (Constants::IMAGE_EXTS.contains(file.suffix())) {
			// Use the database thumb, smaller.
			auto thumb = ThumbnailManager::createThumbnail(file.absoluteFilePath(), 256, 256);
			pixmap = QPixmap::fromImage(*thumb->thumb);
			item->setIcon(QIcon(pixmap));
		}
		else if (Constants::MODEL_EXTS.contains(file.suffix())) {
			// find the meta file as well
			QString jsonMeta;
			QFile file(IrisUtils::buildFileName(file.absoluteFilePath(), Constants::META_EXT));
			file.open(QFile::ReadOnly | QFile::Text);
			jsonMeta = file.readAll();
			file.close();
			QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonMeta.toUtf8());
			QJsonObject json = jsonDoc.object();
			const QString assetGuid = json["guid"].toString();

			QList<Asset*>::iterator thumb = std::find_if(AssetManager::getAssets().begin(),
													     AssetManager::getAssets().end(),
														 find_asset_thumbnail(assetGuid));

			if (thumb != AssetManager::getAssets().end()) pixmap = (*thumb)->thumbnail;

			item->setIcon(pixmap);
		}
		else {
			item->setIcon(QIcon(":/icons/ic_file.svg"));
		}
	}

	item->setData(Qt::DisplayRole, file.baseName());
	item->setData(Qt::UserRole, file.absolutePath());

	item->setSizeHint(QSize(128, 128));
	item->setTextAlignment(Qt::AlignCenter);
	item->setFlags(item->flags() | Qt::ItemIsEditable);

	if (file.suffix() != "meta") {
		ui->assetView->addItem(item);
	}

  //  if (file.isDir()) {
		//QPixmap pixmap;

		//// we need our own search predicate since we have a vector that houses structs
		//if (!Globals::assetNames.value(QFileInfo(file.fileName()).baseName()).isEmpty()) {
		//	QVector<AssetData>::iterator thumb = std::find_if(assetList.begin(),
		//		assetList.end(),
		//		find_asset_thumbnail(QFileInfo(file.fileName()).baseName()));

		//	item = new QListWidgetItem(QFileInfo(thumb->name).baseName());
		//	pixmap = QPixmap::fromImage(QImage::fromData(thumb->thumbnail));
		//	item->setIcon(QIcon(pixmap));
		//	item->setData(Qt::DisplayRole, QFileInfo(thumb->name).baseName());
		//	item->setData(Qt::UserRole, file.absoluteFilePath());
		//	item->setData(MODEL_EXT_ROLE, thumb->extension);
		//	item->setData(MODEL_GUID_ROLE, thumb->guid);
		//	item->setData(MODEL_TYPE_ROLE, thumb->type);
		//}
		//else {
		//	item = new QListWidgetItem(file.fileName());
		//	item->setIcon(QIcon(":/icons/ic_folder.svg"));
		//	item->setData(Qt::DisplayRole, file.fileName());
		//	item->setData(Qt::UserRole, file.absolutePath());
		//}
  //  } else {
  //      QPixmap pixmap;
  //      item = new QListWidgetItem(file.fileName());
		//
		//if (Constants::IMAGE_EXTS.contains(file.suffix())) {
		//	auto thumb = ThumbnailManager::createThumbnail(file.absoluteFilePath(), 256, 256);
		//	pixmap = QPixmap::fromImage(*thumb->thumb);
		//	item->setIcon(QIcon(pixmap));

  //          item->setData(MODEL_EXT_ROLE, "");
		//	item->setData(MODEL_GUID_ROLE, file.absoluteFilePath());
		//	item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Texture));
		//}
		//else if (Constants::MODEL_EXTS.contains(file.suffix())) {
		//	if (db->hasCachedThumbnail(file.fileName())) {
		//		QPixmap cachedPixmap;
		//		const QByteArray blob = db->fetchCachedThumbnail(file.fileName());
		//		if (cachedPixmap.loadFromData(blob, "PNG")) {
		//			item->setIcon(QIcon(cachedPixmap));
		//		}

  //              item->setData(MODEL_EXT_ROLE, "");
		//	    item->setData(MODEL_GUID_ROLE, file.absoluteFilePath());
		//	    item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Object));
		//	}
		//	else {
		//		item->setIcon(QIcon(":/icons/ic_file.svg"));
		//		auto asset = AssetManager::getAssetByPath(file.absoluteFilePath());
		//		if (asset != nullptr) {
		//			if (!asset->thumbnail.isNull()) item->setIcon(QIcon(asset->thumbnail));
		//		}
		//	}

		//	item->setData(Qt::DisplayRole, file.baseName());
		//	item->setData(Qt::UserRole, file.absolutePath());
		//	// item->setData(MODEL_EXT_ROLE, "");
		//	// item->setData(MODEL_GUID_ROLE, file.absoluteFilePath());
		//	// item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Undefined));
		//}
		//else if (file.suffix() == "shader") {
		//	item->setIcon(QIcon(":/icons/ic_file.svg"));
		//}
		//else {
		//	item->setIcon(QIcon(":/icons/ic_file.svg"));
		//}

		//item->setData(Qt::UserRole, file.absolutePath());
  //  }

  //  item->setSizeHint(QSize(128, 128));
  //  item->setTextAlignment(Qt::AlignCenter);
  //  item->setFlags(item->flags() | Qt::ItemIsEditable);

    /*ui->assetView->addItem(item);*/
}

void AssetWidget::addItem(const FolderData &folderData)
{
	QListWidgetItem *item = new QListWidgetItem;
	item->setData(Qt::DisplayRole, folderData.name);
	item->setData(DATA_GUID_ROLE, folderData.guid);
	item->setData(DATA_PARENT_ROLE, folderData.parent);

	item->setSizeHint(QSize(128, 128));
	item->setTextAlignment(Qt::AlignCenter);
	item->setFlags(item->flags() | Qt::ItemIsEditable);

	item->setIcon(QIcon(":/icons/ic_folder.svg"));

	ui->assetView->addItem(item);
}

void AssetWidget::addItem(const AssetTileData &assetData)
{
	QListWidgetItem *item = new QListWidgetItem;
	item->setData(Qt::DisplayRole, QFileInfo(assetData.name).baseName());
	item->setData(DATA_GUID_ROLE, assetData.guid);
	item->setData(DATA_PARENT_ROLE, assetData.parent);

	if (assetData.type == static_cast<int>(AssetType::Texture)) {

	}
	else if (assetData.type == static_cast<int>(AssetType::Object)) {
		//item->setData(Qt::UserRole,		file.absoluteFilePath());
		const QString meshAssetGuid = db->getDependencyByType(static_cast<int>(AssetType::Object), assetData.guid);
		const AssetTileData meshAssetName = db->fetchAsset(meshAssetGuid);
		item->setData(MODEL_GUID_ROLE, assetData.guid);
		item->setData(MODEL_TYPE_ROLE, assetData.type);
		item->setData(MODEL_MESH_ROLE, meshAssetName.name);
	}

	QPixmap thumbnail;
	if (thumbnail.loadFromData(assetData.thumbnail, "PNG")) {
		item->setIcon(QIcon(thumbnail));
	}
	else {
		item->setIcon(QIcon(":/icons/ic_file.svg"));
	}

	item->setSizeHint(QSize(128, 128));
	item->setTextAlignment(Qt::AlignCenter);
	item->setFlags(item->flags() | Qt::ItemIsEditable);

	// Hide meshes for now, we work with objects
	if (assetData.type != static_cast<int>(AssetType::Mesh)) {
		ui->assetView->addItem(item);
	}
}

void AssetWidget::updateAssetView(const QString &path)
{
    ui->assetView->clear();
	assetItem.selectedGuid = path;

	for (const auto &folder : db->fetchChildFolders(path)) {
		addItem(folder);
	}

	for (const auto &asset : db->fetchChildAssets(path)) {
		addItem(asset);
	}
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
    assetItem.selectedPath = item->data(0, Qt::UserRole).toString();
    updateAssetView(item->data(0, Qt::UserRole).toString());
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
        if ((*it)->data(0, Qt::UserRole) == path) {
            ui->assetTree->clearSelection();
            (*it)->setSelected(true);
            ui->assetTree->expandItem((*it));
            break;
        }

        ++it;
    }
}

void AssetWidget::assetViewDblClicked(QListWidgetItem *item)
{
    // TODO - depending on file type, we can open mini dialogs for texture preview
    // TODO - store guid in a custom role
    // Or we can directly add model files to the scene
    //QFileInfo fInfo;
    //if (!Globals::assetNames.key(item->text()).isEmpty()) {
    //    fInfo.setFile(QDir(item->data(Qt::UserRole).toString()).filePath(Globals::assetNames.key(item->text())));
    //}
    //else {
    //    fInfo.setFile(QDir(item->data(Qt::UserRole).toString()).filePath(item->text()));
    //}

	updateAssetView(item->data(DATA_GUID_ROLE).toString());
	//assetItem.selectedGuid = item->data(DATA_GUID_ROLE).toString();

    //if (fInfo.isDir()) {
    //    assetItem.wItem = item;
    //    assetItem.selectedPath = fInfo.absoluteFilePath();
    //    updateAssetView(fInfo.absoluteFilePath());
    //    syncTreeAndView(fInfo.absoluteFilePath());
    //}
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
        for (auto item : AssetManager::getAssets()) {
            if (item->fileName.contains(searchString)) {
                addItem(item->path);
            }
        }
    } else {
        updateAssetView(assetItem.selectedPath);
    }
}

void AssetWidget::OnLstItemsCommitData(QWidget *listItem)
{
    QString folderName = reinterpret_cast<QLineEdit*>(listItem)->text();
	if (!folderName.isEmpty()) {
		db->insertFolder(folderName, assetItem.selectedGuid);
		//reinterpret_cast<QListWidgetItem*>(listItem)->setData(Qt::DisplayRole, folderName);
		//reinterpret_cast<QListWidgetItem*>(listItem)->setData(DATA_GUID_ROLE, assetItem.selectedGuid);
	}

    //QDir dir(assetItem.selectedPath + '/' + folderName);
    //if (!dir.exists()) {
    //    dir.mkpath(".");
    //}

    //auto child = ui->assetTree->currentItem();

    //if (child) {    // should always be set but just in case
    //    auto branch = new QTreeWidgetItem();
    //    branch->setIcon(0, QIcon(":/icons/ic_folder.svg"));
    //    branch->setText(0, folderName);
    //    branch->setData(0, Qt::UserRole, assetItem.selectedPath + '/' + folderName);
    //    child->addChild(branch);

    //    ui->assetTree->clearSelection();
    //    branch->setSelected(true);
    //}
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
	QFileInfo itemInfo;
	itemInfo.setFile(QDir(assetItem.selectedPath).filePath(item->text()));

    QDir dir(itemInfo.absoluteFilePath());

	if (itemInfo.isDir()) {
		if (dir.removeRecursively()) {
			delete ui->assetView->takeItem(ui->assetView->row(item));
		}
	}
	else if (Globals::assetNames.contains(QFileInfo(assetItem.selectedPath).baseName())) {
		const QString guid = QFileInfo(assetItem.selectedPath).baseName();
		QDir guid_path(assetItem.selectedPath);
		// delete asset from project TODO (make it red in the tree)
		if (guid_path.removeRecursively()) {
			db->deleteAsset(guid);
			delete ui->assetView->takeItem(ui->assetView->row(item));
		}
	}
	
	if (itemInfo.isFile()) {
        QFile file(itemInfo.absoluteFilePath());
		if (file.remove()) {
			delete ui->assetView->takeItem(ui->assetView->row(item));
		}
	}
}

void AssetWidget::openAtFolder()
{

}

void AssetWidget::createFolder()
{
    QIcon icon = QIcon(":/icons/ic_folder.svg");
    QListWidgetItem *item = new QListWidgetItem(icon, "New Folder");
    item->setData(DATA_GUID_ROLE, QString()); // empty until we confirm
    item->setFlags(item->flags() | Qt::ItemIsEditable);

    ui->assetView->addItem(item);
    ui->assetView->editItem(item);
}

void AssetWidget::importAssetB()
{
    auto fileNames = QFileDialog::getOpenFileNames(this, "Import Asset");
    importAsset(fileNames);
}

void AssetWidget::createDirectoryStructure(const QStringList &fileNames, const directory_pair &dir_pair)
{
	foreach (const QFileInfo &file, fileNames) {
		if (file.isDir()) {
			QStringList list;
			foreach (const QString &item, QDir(file.absoluteFilePath()).entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs)) {
			    list << QDir(file.absoluteFilePath()).filePath(item);
			}

			directory_pair dp;
			dp.path = QDir(dir_pair.path).filePath(file.fileName());
			dp.guid = db->insertFolder(file.baseName(), dir_pair.guid);

			createDirectoryStructure(list, dp);
		}
		else {
			QString fileName;
			QString fileAbsolutePath;

			AssetType type;
			QPixmap thumbnail;

			thumbnail = QPixmap(":/icons/ic_file.svg");

			QString destDir;

			// handle all model extensions here
			if (Constants::IMAGE_EXTS.contains(file.suffix())) {
				auto thumb = ThumbnailManager::createThumbnail(file.absoluteFilePath(), 256, 256);
				thumbnail = QPixmap::fromImage(*thumb->thumb);
				type = AssetType::Texture;
				destDir = "Textures";
			}
			else if (Constants::MODEL_EXTS.contains(file.suffix())) {
				type = AssetType::Mesh;
				destDir = "Models";
			}
			else if (file.suffix() == Constants::SHADER_EXT) {
				type = AssetType::Shader;
				destDir = "Shaders";
			}
			else if (file.suffix() == Constants::MATERIAL_EXT) {
				type = AssetType::Material;
				destDir = "Materials";
			}
			// Generally if we don't explicitly specify an extension, don't import or add it to the database (iKlsR)
			else {
				type = AssetType::Invalid;
			}

			fileName = file.fileName();
			fileAbsolutePath = file.absoluteFilePath();
			
			auto asset = new AssetVariant;
			asset->type         = type;
			asset->fileName     = fileName;
			asset->path         = fileAbsolutePath;
			asset->thumbnail    = thumbnail;

			if (asset->type != AssetType::Invalid) {
				const QString pathToCopyTo =
					QDir(QDir(Globals::project->getProjectFolder()).filePath(destDir)).filePath(asset->fileName);

				QFileInfo check_file(pathToCopyTo);

				if (!check_file.exists()) {
					QByteArray bytes;
					QBuffer buffer(&bytes);
					buffer.open(QIODevice::WriteOnly);
					thumbnail.save(&buffer, "PNG");

					const QString assetGuid = db->insertAssetGlobal(asset->fileName, static_cast<int>(asset->type), dir_pair.guid, bytes);

					if (asset->type == AssetType::Mesh) {
						// Copy textures over to project and create embedded material
						QJsonObject material;
						QStringList texturesToCopy;
						extractTexturesAndMaterialFromMesh(file.absoluteFilePath(), texturesToCopy, material);
						// Create an actual object from a mesh, objects hold materials
						const QString objectGuid = db->insertAssetGlobal(file.baseName(),
																		 static_cast<int>(AssetType::Object),
																		 dir_pair.guid,
																		 QByteArray(),
																		 QByteArray(),
																		 QByteArray(),
																		 QJsonDocument(material).toBinaryData());

						qDebug() << material;
						qDebug() << texturesToCopy;
						asset->assetGuid = objectGuid;
						//AssetManager::assets.append(asset);

						// Insert a dependency for the mesh to the object
						db->insertGlobalDependency(static_cast<int>(AssetType::Object), objectGuid, assetGuid);

						ThumbnailGenerator::getSingleton()->requestThumbnail(
							ThumbnailRequestType::Mesh, file.absoluteFilePath(), objectGuid
						);
					}

					//asset->assetGuid = guid;

					//AssetManager::assets.append(asset);
				}

                // If it's being used in a material already this is redundant
				bool copyFile = QFile::copy(fileAbsolutePath, pathToCopyTo);
			}
		}
	}

    //foreach (const QFileInfo &file, fileNames) {
        //if (file.isFile()) {
   //         AssetType type;
   //         QPixmap pixmap;

			//QString fileName;
			//QString fileAbsolutePath;

   //         if (Constants::IMAGE_EXTS.contains(file.suffix())) {
   //             auto thumb = ThumbnailManager::createThumbnail(file.absoluteFilePath(), 256, 256);
   //             pixmap = QPixmap::fromImage(*thumb->thumb);
   //             type = AssetType::Texture;
			//	fileName = file.fileName();
			//	fileAbsolutePath = file.absoluteFilePath();
   //         } else if (Constants::MODEL_EXTS.contains(file.suffix())) {
   //             auto thumb = ThumbnailManager::createThumbnail(":/icons/ic_file.svg", 128, 128);
   //             pixmap = QPixmap::fromImage(*thumb->thumb);
   //             type = AssetType::Object;

			//	QString guid = GUIDManager::generateGUID();
			//	auto generatedPath = QDir(path).filePath(guid);
			//	QDir dir;
			//	dir.mkdir(generatedPath);

			//	QByteArray bytes;
			//	QBuffer buffer(&bytes);
			//	buffer.open(QIODevice::WriteOnly);
			//	pixmap.save(&buffer, "PNG");

			//	// the material is generated in the thumbnail handler and passed back in the result
			//	db->insertProjectAssetGlobal(file.fileName(), (int) ModelTypes::Object, bytes, QByteArray(), QByteArray(), QByteArray(), guid);
			//	Globals::assetNames.insert(guid, QFileInfo(file.fileName()).baseName());

			//	// guid filename
			//	fileName = QDir(generatedPath).filePath(guid + "." + file.suffix());
			//	fileAbsolutePath = file.absoluteFilePath();

   //         }  else if (file.suffix() == "shader") {
   //             auto thumb = ThumbnailManager::createThumbnail(":/icons/ic_file.svg", 128, 128);
   //             type = AssetType::Shader;
   //             pixmap = QPixmap::fromImage(*thumb->thumb);
			//	fileName = file.fileName();
			//	fileAbsolutePath = file.absoluteFilePath();
   //         } else {
   //             auto thumb = ThumbnailManager::createThumbnail(":/icons/ic_file.svg", 128, 128);
   //             type = AssetType::File;
   //             pixmap = QPixmap::fromImage(*thumb->thumb);
			//	fileName = file.fileName();
			//	fileAbsolutePath = file.absoluteFilePath();
   //         }

   //         auto asset = new AssetVariant;
   //         asset->type         = type;
   //         asset->fileName     = fileName;
   //         asset->path         = fileAbsolutePath;
   //         asset->thumbnail    = pixmap;

   //         bool copyFile = QFile::copy(fileAbsolutePath, QDir(path).filePath(fileName));

   //         if (copyFile) {
   //             if (asset->type == AssetType::Object) {
   //                 ThumbnailGenerator::getSingleton()->requestThumbnail(
   //                     ThumbnailRequestType::Mesh, asset->path, asset->path
   //                 );
   //             }

   //             AssetManager::addAsset(asset);
   //         } else {
   //             QString warningText = QString("Failed to import file %1. Possible reasons are:\n"
   //                                           "1. It already exists\n"
   //                                           "2. The file isn't valid")
   //                                   .arg(file.fileName());

   //             QMessageBox::warning(this, "Import failed", warningText, QMessageBox::Ok);
   //         }
   //     }
   //     else {
   //         auto thumb = ThumbnailManager::createThumbnail(":/app/icons/ic_folder.svg", 128, 128);
   //         auto asset = new AssetFolder;
   //         asset->fileName     = file.fileName();
   //         asset->path         = file.absoluteFilePath();
   //         asset->thumbnail    = QPixmap::fromImage(*thumb->thumb);

   //         // if we encounter a folder, create it and import files
   //         QDir importDir(QDir(path).filePath(file.fileName()));
   //         if (!importDir.exists()) {
   //             importDir.mkpath(".");
   //             AssetManager::addAsset(asset);
   //             QStringList list;
   //             foreach (auto item, QDir(file.absoluteFilePath())
   //                                 .entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs))
   //             {
   //                 list << QDir(file.absoluteFilePath()).filePath(item);
   //             }

   //             QString targetDir = QDir(path).filePath(file.fileName());
   //             createDirectoryStructure(list, targetDir);
   //         }
   //         else {
   //             QString warningText = QString("Failed to create folder %1. Aborting import for this folder")
   //                                   .arg(file.fileName());

   //             QMessageBox::warning(this,
   //                                  "Could not create folder",
   //                                  warningText,
   //                                  QMessageBox::Ok);
   //             continue;
   //         }
   //     }
    //}
}

void AssetWidget::importAsset(const QStringList &path)
{
    QStringList fileNames;
    if (path.isEmpty()) {   // This is hit when we call this function via import menu
        fileNames = QFileDialog::getOpenFileNames(this, "Import Asset");
    } else {
        fileNames = path;
    }

    //if (!assetItem.selectedPath.isEmpty()) {
		directory_pair dir_pair;
		dir_pair.path = assetItem.selectedPath;
		dir_pair.guid = assetItem.selectedGuid;
        createDirectoryStructure(fileNames, dir_pair);
        //populateAssetTree(false);
        updateAssetView(assetItem.selectedGuid);
        // TODO - select the last imported directory!
        //syncTreeAndView(assetItem.selectedPath);
    //}
}

void AssetWidget::onThumbnailResult(ThumbnailResult* result)
{
	// Update the database with the new result
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	auto thumbnail = QPixmap::fromImage(result->thumbnail);
	thumbnail.save(&buffer, "PNG");
	db->updateAssetThumbnail(result->id, bytes);

	// Update the global asset manager with the new image
	//QList<Asset*>::iterator thumb = std::find_if(AssetManager::getAssets().begin(),
	//										     AssetManager::getAssets().end(),
	//											 find_asset_thumbnail(result->id));
	//
	//if (thumb != AssetManager::getAssets().end()) {
	//	int index = AssetManager::getAssets().indexOf(*thumb);
	//	AssetManager::getAssets()[index]->thumbnail = thumbnail;
	//}

	// Refresh the view if we're still there
	for (int i = 0; i < ui->assetView->count(); i++) {
		QListWidgetItem* item = ui->assetView->item(i);
		if (item->data(DATA_GUID_ROLE).toString() == result->id) {
			updateAssetView(assetItem.selectedGuid);
		}
	}
			// 

			//QString asset_guid = QFileInfo(asset->fileName).baseName();

			//if (!result->textureList.isEmpty()) {
			//	for (auto texture : result->textureList) {
			//		QString tex = QFileInfo(texture).isRelative()
			//			? QDir::cleanPath(QDir(QFileInfo(asset->path).absoluteDir()).filePath(texture))
			//			: QDir::cleanPath(texture);
			//		//QFile::copy(tex, QDir(assetItem.selectedPath).filePath(QFileInfo(texture).fileName()));
			//		QFile::copy(tex, QDir(QFileInfo(asset->fileName).absoluteDir()).filePath(QFileInfo(texture).fileName()));
			//	}
			//}

			//// todo? maybe update the asset thumbnail in this function as well?
   //         db->insertThumbnailGlobal(Globals::project->getProjectGuid(), asset->fileName, bytes, asset_guid);
			////auto material_id = db->insertProjectMaterialGlobal(QString(), asset_guid, QJsonDocument(result->material).toBinaryData());
			////db->insertGlobalDependency((int) AssetMetaType::Material, asset_guid, material_id);
			//db->updateAssetThumbnail(asset_guid, bytes);
			//db->updateAssetAsset(asset_guid, QJsonDocument(result->material).toBinaryData());

   //         // find item and update its icon
   //         auto items = ui->assetView->findItems(asset->fileName, Qt::MatchExactly);
   //         if (items.count() > 0) {
   //             items[0]->setIcon(asset->thumbnail);
   //         }
   //     }
   // }

	//updateAssetView(assetItem.selectedPath);

    delete result;
}