#include "assetwidget.h"
#include "ui_assetwidget.h"

#include "../../src/irisgl/src/core/irisutils.h"
#include "../core/thumbnailmanager.h"
#include "../core/project.h"
#include "../globals.h"
#include "../constants.h"

#include <QDebug>
#include <QDir>
#include <QDrag>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>

AssetWidget::AssetWidget(QWidget *parent) : QWidget(parent), ui(new Ui::AssetWidget)
{
    ui->setupUi(this);
    ui->assetView->viewport()->installEventFilter(this);
    ui->assetTree->viewport()->installEventFilter(this);
    ui->assetTree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->assetTree,  SIGNAL(itemClicked(QTreeWidgetItem*, int)),
            this,           SLOT(treeItemSelected(QTreeWidgetItem*)));

    connect(ui->assetTree,  SIGNAL(itemChanged(QTreeWidgetItem*, int)),
            this,           SLOT(treeItemChanged(QTreeWidgetItem*, int)));

    connect(ui->assetTree,  SIGNAL(customContextMenuRequested(const QPoint&)),
            this,           SLOT(sceneTreeCustomContextMenu(const QPoint&)));

    // assetView section
    ui->assetView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->assetView->setViewMode(QListWidget::IconMode);
//    ui->assetView->setUniformItemSizes(true);
//    ui->assetView->setWordWrap(true);
    ui->assetView->setIconSize(QSize(88, 88));
    ui->assetView->setResizeMode(QListWidget::Adjust);
    ui->assetView->setMovement(QListView::Static);
    ui->assetView->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->assetView->setSelectionMode(QAbstractItemView::SingleSelection);

    ui->assetView->setDragEnabled(true);
    ui->assetView->setDragDropMode(QAbstractItemView::DragDrop);
//    ui->assetView->viewport()->setAcceptDrops(true);
//    ui->assetView->setDropIndicatorShown(true);
//    ui->sceneTree->setDragDropMode(QAbstractItemView::InternalMove);
//    ui->assetView->setFocusPolicy();
//    ui->assetView->setMouseTracking(true);

    connect(ui->assetView,  SIGNAL(itemClicked(QListWidgetItem*)),
            this,           SLOT(assetViewClicked(QListWidgetItem*)));

    connect(ui->assetView,  SIGNAL(customContextMenuRequested(const QPoint&)),
            this,           SLOT(sceneViewCustomContextMenu(const QPoint&)));

    connect(ui->assetView,  SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,           SLOT(assetViewDblClicked(QListWidgetItem*)));

    connect(ui->assetView->itemDelegate(),  &QAbstractItemDelegate::commitData,
            this,                           &AssetWidget::OnLstItemsCommitData);

    // other
    connect(ui->searchBar,  SIGNAL(textChanged(QString)),
            this,           SLOT(searchAssets(QString)));

    connect(ui->importBtn,  SIGNAL(pressed()), SLOT(importAssetB()));

    QDir d(Globals::project->getProjectFolder());
    walkFileSystem("", d.absolutePath());

    // it's important that this get's called after the project dialog has OK'd
    populateAssetTree();
}

AssetWidget::~AssetWidget()
{
    delete ui;
}

void AssetWidget::populateAssetTree()
{
    // TODO - revamp this, the actual project directory is up one level. ok
    auto rootTreeItem = new QTreeWidgetItem();
    rootTreeItem->setText(0, "Assets");
    rootTreeItem->setIcon(0, QIcon(":/app/icons/folder-symbol.svg"));
    rootTreeItem->setData(0, Qt::UserRole, Globals::project->getProjectFolder());
    updateTree(rootTreeItem, Globals::project->getProjectFolder());

    ui->assetTree->clear();
    ui->assetTree->addTopLevelItem(rootTreeItem);
    ui->assetTree->expandItem(rootTreeItem);

//    if (ui->assetTree->selectedItems().size() == 0 && ui->assetTree->topLevelItemCount()) {
//        ui->assetTree->topLevelItem(ui->assetTree->topLevelItemCount() - 1)->setSelected(true);
//    }
    updateAssetView(rootTreeItem->data(0, Qt::UserRole).toString());
    rootTreeItem->setSelected(true);

    assetItem.item = rootTreeItem;
    assetItem.selectedPath = rootTreeItem->data(0, Qt::UserRole).toString();
}

void AssetWidget::updateTree(QTreeWidgetItem *parent, QString path)
{
    QDir dir(path);
    QFileInfoList files = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs);
    foreach (const QFileInfo &file, files) {
        if (file.baseName() != "Metadata") {
            auto item = new QTreeWidgetItem();
            item->setIcon(0, QIcon(":/app/icons/folder-symbol.svg"));
            item->setData(0, Qt::DisplayRole, file.fileName());
            item->setData(0, Qt::UserRole, file.absoluteFilePath());
            parent->addChild(item);
            updateTree(item, file.absoluteFilePath());
        }
    }
}

void AssetWidget::walkFileSystem(QString folder, QString path)
{
    QDir dir(path);
    QFileInfoList files = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);
    foreach (const QFileInfo &file, files) {
        // TODO - maybe add some OS centric code to check for hidden folder
        // TODO -- get type from extension if is a file
        if (file.isFile() && file.suffix() != Constants::PROJ_EXT) {
            AssetType type;
            QPixmap pixmap;

            if (file.suffix() == "jpg" || file.suffix() == "png" || file.suffix() == "bmp") {
                auto thumb = ThumbnailManager::createThumbnail(file.absoluteFilePath(), 256, 256);
                pixmap = QPixmap::fromImage(*thumb->thumb);
                type = AssetType::Texture;
            } else if (file.suffix() == "obj" || file.suffix() == "fbx") {
                auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                type = AssetType::Object;
                pixmap = QPixmap::fromImage(*thumb->thumb);
            } else if (file.suffix() == "shader") {
                auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                type = AssetType::Shader;
                pixmap = QPixmap::fromImage(*thumb->thumb);
            } else {
                auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                type = AssetType::File;
                pixmap = QPixmap::fromImage(*thumb->thumb);
            }

            auto asset = new AssetVariant;
            asset->type = type;
            asset->fileName = file.fileName();
            asset->path = file.absoluteFilePath();
            asset->thumbnail = pixmap;

            AssetManager::assets.append(asset);
        } else {           
            auto thumb = ThumbnailManager::createThumbnail(":/app/icons/folder-symbol.svg", 128, 128);
            QPixmap pixmap = QPixmap::fromImage(*thumb->thumb);

            auto asset = new AssetFolder;
            asset->fileName = file.fileName();
            asset->path = file.absoluteFilePath();
            asset->thumbnail = pixmap;

            AssetManager::assets.append(asset);
        }

        if (file.isDir()) {
            walkFileSystem("", file.absoluteFilePath());
        }
    }
}

void AssetWidget::addItem(const QString &asset)
{
    QFileInfo file(asset);
    auto name = file.fileName();

    QIcon icon;
    QListWidgetItem *item;

    if (file.isDir()) {
        item = new QListWidgetItem(QIcon(":/app/icons/folder-symbol.svg"), name);
        item->setData(Qt::UserRole, file.absolutePath());
    } else {
        //AssetType type = AssetType::Invalid;
        QPixmap pixmap;

        item = new QListWidgetItem(name);

        if (file.suffix() == "jpg" || file.suffix() == "png" || file.suffix() == "bmp") {
            //type = AssetType::Texture;
            auto thumb = ThumbnailManager::createThumbnail(file.absoluteFilePath(), 256, 256);
            pixmap = QPixmap::fromImage(*thumb->thumb);
            item->setIcon(QIcon(pixmap));
        } else if (file.suffix() == "obj" || file.suffix() == "fbx") {
            //type = AssetType::Object;
            item->setIcon(QIcon(":/app/icons/google-drive-file.svg"));
        } else if (file.suffix() == "shader") {
            //type = AssetType::Shader;
            item->setIcon(QIcon(":/app/icons/google-drive-file.svg"));
        } else {
            //type = AssetType::File;
            item->setIcon(QIcon(":/app/icons/google-drive-file.svg"));
        }

        item->setData(Qt::UserRole, file.absolutePath());
    }

    item->setSizeHint(QSize(128, 128));
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(item->flags() | Qt::ItemIsEditable);

    QString str = Constants::PROJ_EXT;
    if (file.suffix() != str.remove(0, 1)) {
        ui->assetView->addItem(item);
    }
}

void AssetWidget::updateAssetView(const QString &path)
{
    // clear the old view
    ui->assetView->clear();

    // set to new view path by itering path dir
    QDir dir(path);
    for (auto file : dir.entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs)) {
        if (file != "Metadata") {
            addItem(path + '/' + file);
        }
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
                        if (item) {
                            QDrag *drag = new QDrag(this);
                            QMimeData *mimeData = new QMimeData;
                            mimeData->setText(item->data(Qt::UserRole).toString() + '/' +
                                              item->data(Qt::DisplayRole).toString());
                            drag->setMimeData(mimeData);

                            // only hide for object models
//                            drag->setPixmap(QPixmap());
                            drag->start(Qt::CopyAction | Qt::MoveAction);
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

void AssetWidget::assetViewDblClicked(QListWidgetItem *item)
{
    // TODO - depending on file type, we can open mini dialogs for texture preview
    // Or we can directly add model files to the scene
    QFileInfo path(item->data(Qt::UserRole).toString() + '/' + item->text());
    auto pathStr = path.absoluteFilePath();

    if (path.isDir()) {
        updateAssetView(pathStr);
        assetItem.wItem = item;
        assetItem.selectedPath = pathStr;

        QTreeWidgetItemIterator it(ui->assetTree);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole) == pathStr) {
                ui->assetTree->clearSelection();
                (*it)->setSelected(true);
                ui->assetTree->expandItem((*it));
                break;
            }

            ++it;
        }
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
        for (auto item : AssetManager::assets) {
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
        branch->setIcon(0, QIcon(":/app/icons/folder-symbol.svg"));
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
    QIcon icon = QIcon(":/app/icons/folder-symbol.svg");
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

void AssetWidget::importAsset(const QStringList &path)
{
    QStringList fileNames;
    if (path.isEmpty()) {
        fileNames = QFileDialog::getOpenFileNames(this, "Import Asset");
    } else {
        fileNames = path;
    }

    if (!assetItem.selectedPath.isEmpty()) {
        foreach (const QFileInfo &file, fileNames) {
            QFile::copy(file.absoluteFilePath(), assetItem.selectedPath + '/' + file.fileName());

            if (file.isFile() && file.suffix() != Constants::PROJ_EXT) {
                AssetType type;
                QPixmap pixmap;

                if (file.suffix() == "jpg" || file.suffix() == "png" || file.suffix() == "bmp") {
                    auto thumb = ThumbnailManager::createThumbnail(file.absoluteFilePath(), 256, 256);
                    pixmap = QPixmap::fromImage(*thumb->thumb);
                    type = AssetType::Texture;
                } else if (file.suffix() == "obj" || file.suffix() == "fbx") {
                    auto thumb = ThumbnailManager::createThumbnail(":/app/icons/user-account-box.svg", 128, 128);
                    type = AssetType::Object;
                    pixmap = QPixmap::fromImage(*thumb->thumb);
                }  else if (file.suffix() == "shader") {
                    auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                    type = AssetType::Shader;
                    pixmap = QPixmap::fromImage(*thumb->thumb);
                } else {
                    auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                    type = AssetType::File;
                    pixmap = QPixmap::fromImage(*thumb->thumb);
                }

                auto asset = new AssetVariant;
                asset->type = type;
                asset->fileName = file.fileName();
                asset->path = file.absoluteFilePath();
                asset->thumbnail = pixmap;

                AssetManager::assets.append(asset);
            }
        }

        updateAssetView(assetItem.selectedPath);
    }
}
