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
    ui->assetView->setMovement(QListView::Static);
    ui->assetView->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->assetView->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(ui->assetView,  SIGNAL(itemClicked(QListWidgetItem*)),
            this,           SLOT(assetViewClicked(QListWidgetItem*)));

    connect(ui->assetView,  SIGNAL(customContextMenuRequested(const QPoint&)),
            this,           SLOT(sceneViewCustomContextMenu(const QPoint&)));
}

AssetWidget::~AssetWidget()
{
    delete ui;
}

void AssetWidget::populateAssetTree()
{
    // TODO - revamp this, the actual project directory is up one level. ok
//    qDebug() << "PROJECT " << Globals::project->getProjectFolder();

    QDir projDir = Globals::project->getProjectFolder();

    for (auto topLevel : projDir.entryList(QDir::NoDotAndDotDot | QDir::Dirs)) {
        addTreeRoot(topLevel, projDir.path() + '/' + topLevel);
    }
}

void AssetWidget::addTreeRoot(const QString &name, const QString &desc)
{
    auto item = new QTreeWidgetItem(ui->assetTree);
    item->setText(0, name);
    item->setData(0, Qt::UserRole, desc);

//    addTreeChild(item, "HMMM", "uncle!!!!");
}

void AssetWidget::addTreeChild(QTreeWidgetItem *parent, const QString &name, const QString &desc)
{
    auto item = new QTreeWidgetItem();
    item->setData(0, Qt::DisplayRole, name);
    item->setData(0, Qt::UserRole, desc);

    parent->addChild(item);
}

void AssetWidget::addItem(const QString &asset)
{
    auto name = QFileInfo(asset).baseName();
    if (name.length() > 10) {
        name.truncate(8);
        name += "...";
    }

    QFileInfo file(asset);
    QIcon icon;
    QListWidgetItem *item;

    if (file.isDir()) {
        QDir meta(Globals::project->getProjectFolder());
        meta.cdUp();
        QString p = meta.path() + "/.metadata/folder.png";
        // TODO - store these assets in the application folder, these don't need to be per project!
        icon = QIcon(p);
        item = new QListWidgetItem(icon, name);
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
        // ui->assetView->addItem(QFileInfo(file).baseName());
    }
}

bool AssetWidget::eventFilter(QObject *watched, QEvent *event)
{
    return QObject::eventFilter(watched, event);
}

void AssetWidget::treeItemSelected(QTreeWidgetItem *item)
{
    auto data = item->data(0, Qt::DisplayRole).toString();
    auto path = item->data(0, Qt::UserRole).toString();

    updateAssetView(path);

//    ui->assetView->addItem(data);
//    ui->assetView->addItem(path);
}

void AssetWidget::treeItemChanged(QTreeWidgetItem* item, int column)
{

}

void AssetWidget::sceneTreeCustomContextMenu(const QPoint& pos)
{
    QModelIndex index = ui->assetTree->indexAt(pos);

    if (!index.isValid()) return;

    auto item = ui->assetTree->itemAt(pos);

    QMenu menu;
    QAction *action;

    action = new QAction(QIcon(), "Delete", this);
    connect(action, SIGNAL(triggered()), this, SLOT(deleteFolder()));
    menu.addAction(action);

    menu.exec(ui->assetTree->mapToGlobal(pos));
}

void AssetWidget::sceneViewCustomContextMenu(const QPoint& pos)
{
    QModelIndex index = ui->assetView->indexAt(pos);

    if (!index.isValid()) return;

    auto item = ui->assetView->itemAt(pos);

    QMenu menu;
    QAction *action;

    action = new QAction(QIcon(), "Delete", this);
    connect(action, SIGNAL(triggered()), this, SLOT(deleteFolder()));
    menu.addAction(action);

    menu.exec(ui->assetView->mapToGlobal(pos));
}

void AssetWidget::assetViewClicked(QListWidgetItem *item)
{
    qDebug() << item->text();
}

void AssetWidget::deleteFolder()
{

}
