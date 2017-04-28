#include "assetwidget.h"
#include "ui_assetwidget.h"

#include "../../src/irisgl/src/core/irisutils.h"
#include "../core/thumbnailmanager.h"
#include "../core/project.h"
#include "../globals.h"

#include <QDebug>
#include <QDir>
#include <QMenu>

AssetWidget::AssetWidget(QWidget *parent) : QWidget(parent), ui(new Ui::AssetWidget)
{
    ui->setupUi(this);

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
}

void AssetWidget::updateTree(QTreeWidgetItem *parent, QString path)
{
    QDir dir(path);
    QFileInfoList files = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs);
    foreach (const QFileInfo &file, files) {
        if (!file.fileName().startsWith('.')) {
            auto thumb = ThumbnailManager::createThumbnail(":/app/icons/folder-symbol.svg", 128, 128);
            QPixmap pixmap = QPixmap::fromImage(*thumb->thumb);
            auto item = new QTreeWidgetItem();
            item->setIcon(0, QIcon(pixmap));
            item->setData(0, Qt::DisplayRole, file.fileName());
            item->setData(0, Qt::UserRole, file.absoluteFilePath());
//            item->setData(0, ID_ROLE, );
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
        if (!file.fileName().startsWith('.')) {
            // TODO -- get type from extension if is a file
            if (file.isFile()) {
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
                } else {
                    auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                    type = AssetType::File;
                    pixmap = QPixmap::fromImage(*thumb->thumb);
                }

                AssetManager::assets.append(new Asset(type,
                                        file.absoluteFilePath(),
                                        file.fileName(),
                                        pixmap));
            } else {
                auto thumb = ThumbnailManager::createThumbnail(":/app/icons/folder-symbol.svg", 128, 128);
                QPixmap pixmap = QPixmap::fromImage(*thumb->thumb);
                AssetManager::assets.append(new Asset(AssetType::Folder,
                                        file.absoluteFilePath(),
                                        file.fileName(),
                                        pixmap));
            }

            if (file.isDir()) {
                walkFileSystem("", file.absoluteFilePath());
            }
        }
    }
}

void AssetWidget::addItem(const QString &asset)
{
    QFileInfo file(asset);
    auto name = file.baseName();

//    if (name.length() > 10) {
//        name.truncate(8);
//        name += "...";
//    }

    QIcon icon;
    QListWidgetItem *item;

    if (file.isDir()) {
        // QImage img(asset);
        // icon = QIcon(pixmap);
        auto t = ThumbnailManager::createThumbnail(":/app/icons/folder-symbol.svg", 128, 128);
        QPixmap pixmap;
        pixmap = pixmap.fromImage(*t->thumb);
        item = new QListWidgetItem(QIcon(pixmap), name);
        item->setData(Qt::UserRole, file.absolutePath());
    } else {
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
        } else {
            auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
            type = AssetType::File;
            pixmap = QPixmap::fromImage(*thumb->thumb);
        }

        item = new QListWidgetItem(QIcon(pixmap), name);
        item->setData(Qt::UserRole, file.absolutePath());
    }

    item->setSizeHint(QSize(128, 128));
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    ui->assetView->addItem(item);
}

void AssetWidget::updateAssetView(const QString &path)
{
    // clear the old view
    ui->assetView->clear();

    // set to new view path by itering path dir
    QDir dir(path);
    for (auto file : dir.entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs)) {
        addItem(path + '/' + file);
    }
}

bool AssetWidget::eventFilter(QObject *watched, QEvent *event)
{
    return QObject::eventFilter(watched, event);
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
        connect(action, SIGNAL(triggered()), this, SLOT(importAsset()));
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

    QString str = item->data(Qt::UserRole).toString() + '/' + item->text();

    if (path.isDir()) {
        updateAssetView(item->data(Qt::UserRole).toString() + '/' + item->text());

        QTreeWidgetItemIterator it(ui->assetTree);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole) == str) {
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

void AssetWidget::importAsset()
{
    QString dir = QApplication::applicationDirPath();
    auto fileNames = QFileDialog::getOpenFileNames(this, "Import Asset");

    if (!assetItem.selectedPath.isEmpty()) {
        foreach (const QFileInfo &file, fileNames) {
            QFile::copy(file.absoluteFilePath(), assetItem.selectedPath + '/' + file.fileName());

            if (file.isFile()) {
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
                } else {
                    auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                    type = AssetType::File;
                    pixmap = QPixmap::fromImage(*thumb->thumb);
                }

                AssetManager::assets.append(new Asset(type,
                                            file.absoluteFilePath(),
                                            file.fileName(),
                                            pixmap));
            }
        }

        updateAssetView(assetItem.selectedPath);
    }
}
