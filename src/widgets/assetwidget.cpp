#include "assetwidget.h"
#include "ui_assetwidget.h"

#include "irisgl/src/core/irisutils.h"
#include "../core/thumbnailmanager.h"
#include "../core/project.h"
#include "../globals.h"
#include "../constants.h"
#include "../uimanager.h"
#include "../widgets/sceneviewwidget.h"
#include "../editor/thumbnailgenerator.h"
#include "../core/database/database.h"

#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QDrag>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>

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
    populateAssetTree(true);
}

void AssetWidget::updateLabels()
{
    updateAssetView(assetItem.selectedPath);
    populateAssetTree(false);
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
    rootTreeItem->setIcon(0, QIcon(":/icons/ic_folder.svg"));
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
        item->setIcon(0, QIcon(":/icons/ic_folder.svg"));
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
    foreach (auto asset, AssetManager::assets) {
        if (asset->type == AssetType::Object) {
            // TODO - fetch a list and check that instead of hitting the db, low cost but better way
            if (!db->hasCachedThumbnail(asset->fileName)) {
                ThumbnailGenerator::getSingleton()->requestThumbnail(
                    ThumbnailRequestType::Mesh, asset->path, asset->path
                );
            }
        }
    }
}

void AssetWidget::addItem(const QString &asset)
{
    QFileInfo file(asset);
    QListWidgetItem *item;

    if (file.isDir()) {
		QPixmap pixmap;

		// we need our own search predicate since we have a vector that houses structs
		if (!Globals::assetNames.value(QFileInfo(file.fileName()).baseName()).isEmpty()) {
			QVector<AssetData>::iterator thumb = std::find_if(assetList.begin(),
				assetList.end(),
				find_asset_thumbnail(QFileInfo(file.fileName()).baseName()));

			item = new QListWidgetItem(QFileInfo(thumb->name).baseName());
			pixmap = QPixmap::fromImage(QImage::fromData(thumb->thumbnail));
			item->setIcon(QIcon(pixmap));
			item->setData(Qt::DisplayRole, QFileInfo(thumb->name).baseName());
			item->setData(Qt::UserRole, file.absoluteFilePath());
			item->setData(MODEL_EXT_ROLE, thumb->extension);
			item->setData(MODEL_GUID_ROLE, thumb->guid);
			item->setData(MODEL_TYPE_ROLE, thumb->type);
		}
		else {
			item = new QListWidgetItem(file.fileName());
			item->setIcon(QIcon(":/icons/ic_folder.svg"));
			item->setData(Qt::DisplayRole, file.fileName());
			item->setData(Qt::UserRole, file.absolutePath());
		}
    } else {
        QPixmap pixmap;
        item = new QListWidgetItem(file.fileName());
		
		if (Constants::IMAGE_EXTS.contains(file.suffix())) {
			auto thumb = ThumbnailManager::createThumbnail(file.absoluteFilePath(), 256, 256);
			pixmap = QPixmap::fromImage(*thumb->thumb);
			item->setIcon(QIcon(pixmap));
		}
		else if (Constants::MODEL_EXTS.contains(file.suffix())) {
			if (db->hasCachedThumbnail(file.fileName())) {
				QPixmap cachedPixmap;
				const QByteArray blob = db->fetchCachedThumbnail(file.fileName());
				if (cachedPixmap.loadFromData(blob, "PNG")) {
					item->setIcon(QIcon(cachedPixmap));
				}
			}
			else {
				item->setIcon(QIcon(":/icons/ic_file.svg"));
				auto asset = AssetManager::getAssetByPath(file.absoluteFilePath());
				if (asset != nullptr) {
					if (!asset->thumbnail.isNull()) item->setIcon(QIcon(asset->thumbnail));
				}
			}

			item->setData(Qt::DisplayRole, file.baseName());
			item->setData(Qt::UserRole, file.absolutePath());
			item->setData(MODEL_EXT_ROLE, "");
			item->setData(MODEL_GUID_ROLE, file.absoluteFilePath());	// TODO: actually generate guids and use these
			item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Object));
		}
		else if (file.suffix() == "shader") {
			item->setIcon(QIcon(":/icons/ic_file.svg"));
		}
		else {
			item->setIcon(QIcon(":/icons/ic_file.svg"));
		}

		item->setData(Qt::UserRole, file.absolutePath());
    }

    item->setSizeHint(QSize(128, 128));
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(item->flags() | Qt::ItemIsEditable);

    ui->assetView->addItem(item);
}

void AssetWidget::updateAssetView(const QString &path)
{
	assetList = db->fetchThumbnails();

	// for (auto asset : assetList) qDebug() << asset.name;

    // clear the old view
    ui->assetView->clear();
    // set to new view path by itering path dir
    for (auto file : QDir(path).entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs)) {
        addItem(QDir(path).filePath(file));
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
							qint8 assetType = item->data(MODEL_TYPE_ROLE).toInt();
							QString name = item->data(Qt::DisplayRole).toString();
							roleDataMap[0] = QVariant(assetType);
							roleDataMap[1] = QVariant(name);
							roleDataMap[3] = IrisUtils::buildFileName(
								QDir(item->data(Qt::UserRole).toString()).filePath(item->data(MODEL_GUID_ROLE).toString()),
								item->data(MODEL_EXT_ROLE).toString()
							);
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
        assetItem.selectedPath = item->data(Qt::UserRole).toString();

        // action = new QAction(QIcon(), "Rename", this);
        // connect(action, SIGNAL(triggered()), this, SLOT(renameViewItem()));
        // menu.addAction(action);

        action = new QAction(QIcon(), "Delete", this);
        connect(action, SIGNAL(triggered()), this, SLOT(deleteViewFolder()));
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
    QFileInfo fInfo;
    if (!Globals::assetNames.key(item->text()).isEmpty()) {
        fInfo.setFile(QDir(item->data(Qt::UserRole).toString()).filePath(Globals::assetNames.key(item->text())));
    }
    else {
        fInfo.setFile(QDir(item->data(Qt::UserRole).toString()).filePath(item->text()));
    }

    if (fInfo.isDir()) {
        assetItem.wItem = item;
        assetItem.selectedPath = fInfo.absoluteFilePath();
        updateAssetView(fInfo.absoluteFilePath());
        syncTreeAndView(fInfo.absoluteFilePath());
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
    QDir dir(assetItem.selectedPath + '/' + folderName);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    auto child = ui->assetTree->currentItem();

    if (child) {    // should always be set but just in case
        auto branch = new QTreeWidgetItem();
        branch->setIcon(0, QIcon(":/icons/ic_folder.svg"));
        branch->setText(0, folderName);
        branch->setData(0, Qt::UserRole, assetItem.selectedPath + '/' + folderName);
        child->addChild(branch);

        ui->assetTree->clearSelection();
        branch->setSelected(true);
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

void AssetWidget::deleteViewFolder()
{
    auto item = assetItem.wItem;
    QDir dir(assetItem.selectedPath + '/' + item->text());
    if (dir.removeRecursively()) {
        delete ui->assetView->takeItem(ui->assetView->row(item));
    }
}

void AssetWidget::openAtFolder()
{

}

void AssetWidget::createFolder()
{
    QIcon icon = QIcon(":/icons/ic_folder.svg");
    QListWidgetItem *item = new QListWidgetItem(icon, "New Folder");
    item->setData(Qt::UserRole, "");    // null until we confirm
    item->setFlags(item->flags() | Qt::ItemIsEditable);

    ui->assetView->addItem(item);
    ui->assetView->editItem(item);
}

void AssetWidget::importAssetB()
{
    auto fileNames = QFileDialog::getOpenFileNames(this, "Import Asset");
    importAsset(fileNames);
}

void AssetWidget::createDirectoryStructure(const QStringList &fileNames, const QString &path)
{
    foreach (const QFileInfo &file, fileNames) {
        if (file.isFile()) {
            AssetType type;
            QPixmap pixmap;

            if (Constants::IMAGE_EXTS.contains(file.suffix())) {
                auto thumb = ThumbnailManager::createThumbnail(file.absoluteFilePath(), 256, 256);
                pixmap = QPixmap::fromImage(*thumb->thumb);
                type = AssetType::Texture;
            } else if (Constants::MODEL_EXTS.contains(file.suffix())) {
                auto thumb = ThumbnailManager::createThumbnail(":/icons/ic_file.svg", 128, 128);
                pixmap = QPixmap::fromImage(*thumb->thumb);
                type = AssetType::Object;
            }  else if (file.suffix() == "shader") {
                auto thumb = ThumbnailManager::createThumbnail(":/icons/ic_file.svg", 128, 128);
                type = AssetType::Shader;
                pixmap = QPixmap::fromImage(*thumb->thumb);
            } else {
                auto thumb = ThumbnailManager::createThumbnail(":/icons/ic_file.svg", 128, 128);
                type = AssetType::File;
                pixmap = QPixmap::fromImage(*thumb->thumb);
            }

            auto asset = new AssetVariant;
            asset->type         = type;
            asset->fileName     = file.fileName();
            asset->path         = file.absoluteFilePath();
            asset->thumbnail    = pixmap;

            bool copyFile = QFile::copy(file.absoluteFilePath(), QDir(path).filePath(file.fileName()));

            if (copyFile) {
                if (asset->type == AssetType::Object) {
                    ThumbnailGenerator::getSingleton()->requestThumbnail(
                        ThumbnailRequestType::Mesh, asset->path, asset->path
                    );
                }

                AssetManager::addAsset(asset);
            } else {
                QString warningText = QString("Failed to import file %1. Possible reasons are:\n"
                                              "1. It already exists\n"
                                              "2. The file isn't valid")
                                      .arg(file.fileName());

                QMessageBox::warning(this, "Import failed", warningText, QMessageBox::Ok);
            }
        }
        else {
            auto thumb = ThumbnailManager::createThumbnail(":/app/icons/ic_folder.svg", 128, 128);
            auto asset = new AssetFolder;
            asset->fileName     = file.fileName();
            asset->path         = file.absoluteFilePath();
            asset->thumbnail    = QPixmap::fromImage(*thumb->thumb);

            // if we encounter a folder, create it and import files
            QDir importDir(QDir(path).filePath(file.fileName()));
            if (!importDir.exists()) {
                importDir.mkpath(".");
                AssetManager::addAsset(asset);
                QStringList list;
                foreach (auto item, QDir(file.absoluteFilePath())
                                    .entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs))
                {
                    list << QDir(file.absoluteFilePath()).filePath(item);
                }

                QString targetDir = QDir(path).filePath(file.fileName());
                createDirectoryStructure(list, targetDir);
            }
            else {
                QString warningText = QString("Failed to create folder %1. Aborting import for this folder")
                                      .arg(file.fileName());

                QMessageBox::warning(this,
                                     "Could not create folder",
                                     warningText,
                                     QMessageBox::Ok);
                continue;
            }
        }
    }
}

void AssetWidget::importAsset(const QStringList &path)
{
    QStringList fileNames;
    if (path.isEmpty()) {   // this is hit when we call this function via import menu
        fileNames = QFileDialog::getOpenFileNames(this, "Import Asset");
    } else {
        fileNames = path;
    }

    if (!assetItem.selectedPath.isEmpty()) {
        createDirectoryStructure(fileNames, assetItem.selectedPath);
        populateAssetTree(false);
        updateAssetView(assetItem.selectedPath);
        // TODO - select the last imported directory!
        syncTreeAndView(assetItem.selectedPath);
    }
}

void AssetWidget::onThumbnailResult(ThumbnailResult* result)
{
    for (auto& asset : AssetManager::getAssets()) {
        if (asset->path == result->id) {
            asset->thumbnail = QPixmap::fromImage(result->thumbnail);

            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            asset->thumbnail.save(&buffer, "PNG");
            db->insertThumbnailGlobal("null", asset->fileName, bytes);

            // find item and update its icon
            auto items = ui->assetView->findItems(asset->fileName, Qt::MatchExactly);
            if (items.count() > 0) {
                items[0]->setIcon(asset->thumbnail);
            }
        }
    }

    delete result;
}
