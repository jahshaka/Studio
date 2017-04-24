#include "assetwidget.h"
#include "ui_assetwidget.h"

#include "../../src/irisgl/src/core/irisutils.h"
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

    // it's important that this get's called after the project dialog has OK'd
    populateAssetTree();

    connect(ui->assetTree,  SIGNAL(itemClicked(QTreeWidgetItem*, int)),
            this,           SLOT(treeItemSelected(QTreeWidgetItem*)));

    connect(ui->assetTree,  SIGNAL(itemChanged(QTreeWidgetItem*, int)),
            this,           SLOT(treeItemChanged(QTreeWidgetItem*, int)));

    connect(ui->assetTree,  SIGNAL(customContextMenuRequested(const QPoint&)),
            this,           SLOT(sceneTreeCustomContextMenu(const QPoint&)));

    // assetView section
    ui->assetView->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->assetView->setViewMode(QListWidget::IconMode);
    ui->assetView->setIconSize(QSize(88, 88));
    ui->assetView->setResizeMode(QListWidget::Adjust);
//    ui->assetView->setSpacing(4);
    ui->assetView->setMovement(QListView::Static);
    ui->assetView->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->assetView->setSelectionMode(QAbstractItemView::MultiSelection);

    connect(ui->assetView,  SIGNAL(itemClicked(QListWidgetItem*)),
            this,           SLOT(assetViewClicked(QListWidgetItem*)));

    connect(ui->assetView,  SIGNAL(customContextMenuRequested(const QPoint&)),
            this,           SLOT(sceneViewCustomContextMenu(const QPoint&)));

    connect(ui->assetView,  SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,           SLOT(assetViewDblClicked(QListWidgetItem*)));
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
    updateTree(rootTreeItem, Globals::project->getProjectFolder());

    ui->assetTree->clear();
    ui->assetTree->addTopLevelItem(rootTreeItem);
    ui->assetTree->expandItem(rootTreeItem);
}

void AssetWidget::updateTree(QTreeWidgetItem *parent, QString path)
{
    QDir dir(path);
    QFileInfoList files = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs);
    foreach (const QFileInfo &file, files) {
        if (!file.fileName().startsWith('.')) {
            auto item = new QTreeWidgetItem();
            item->setData(0, Qt::DisplayRole, file.fileName());
            item->setData(0, Qt::UserRole, file.absoluteFilePath());
            parent->addChild(item);
            updateTree(item, file.absoluteFilePath());
        }
    }
}

void AssetWidget::addItem(const QString &asset)
{
    QFileInfo file(asset);
    auto name = file.baseName();
    if (name.length() > 10) {
        name.truncate(8);
        name += "...";
    }

    QIcon icon;
    QListWidgetItem *item;

    if (file.isDir()) {
        icon = QIcon(":/app/icons/folder-symbol.svg");
        item = new QListWidgetItem(icon, name);
        item->setData(Qt::UserRole, file.absolutePath());
    } else {
        QImage img(asset);
        QPixmap pixmap;
        pixmap = pixmap.fromImage(img.scaled(88, 88, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        icon = QIcon(pixmap);
        item = new QListWidgetItem(icon, name);
    }

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
    updateAssetView(item->data(0, Qt::UserRole).toString());
}

void AssetWidget::treeItemChanged(QTreeWidgetItem* item, int column)
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

    action = new QAction(QIcon(), "Open in Explorer", this);
    connect(action, SIGNAL(triggered()), this, SLOT(openAtFolder()));
    menu.addAction(action);

    action = new QAction(QIcon(), "Delete", this);
    connect(action, SIGNAL(triggered()), this, SLOT(deleteFolder()));
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
        assetItem.selectedPath = item->data(Qt::UserRole).toString();

        action = new QAction(QIcon(), "Delete", this);
        connect(action, SIGNAL(triggered()), this, SLOT(deleteFolder()));
        menu.addAction(action);
    } else {
        QMenu *createMenu = menu.addMenu("Create");
        action = new QAction(QIcon(), "New Folder", this);
        connect(action, SIGNAL(triggered()), this, SLOT(createFolder()));
        createMenu->addAction(action);

        action = new QAction(QIcon(), "Open in Explorer", this);
        connect(action, SIGNAL(triggered()), this, SLOT(openAtFolder()));
        menu.addAction(action);
    }

    menu.exec(ui->assetView->mapToGlobal(pos));
}

void AssetWidget::assetViewClicked(QListWidgetItem *item)
{
    qDebug() << item->text();
}

void AssetWidget::assetViewDblClicked(QListWidgetItem *item)
{
    QFileInfo path(item->data(Qt::UserRole).toString());
    if (path.isDir()) {
        // do something
    }
}

void AssetWidget::deleteFolder()
{
    QDir dir(assetItem.selectedPath);
    if (dir.removeRecursively()) {
        auto item = assetItem.item;
        delete item->parent()->takeChild(item->parent()->indexOfChild(item));
    }
}

void AssetWidget::openAtFolder()
{

}

void AssetWidget::createFolder()
{

}

void AssetWidget::importAsset()
{

}
