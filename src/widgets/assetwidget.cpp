#include "assetwidget.h"
#include "ui_assetwidget.h"

#include "../core/project.h"
#include "../globals.h"

#include <QDebug>
#include <QDir>

AssetWidget::AssetWidget(QWidget *parent) : QWidget(parent), ui(new Ui::AssetWidget)
{
    ui->setupUi(this);

    populateAssetTree();
}

AssetWidget::~AssetWidget()
{
    delete ui;
}

void AssetWidget::populateAssetTree()
{
    qDebug() << "PROJECT " << Globals::project->getProjectFolder();

    QDir projDir = Globals::project->getProjectFolder();

//    ui->assetTree->setColumnCount(1);

    for (auto topLevel : projDir.entryList(QDir::NoDotAndDotDot | QDir::Dirs)) {
        addTreeRoot(topLevel);
    }

//    addTreeChild(matRoot, "FX Mats");
}

void AssetWidget::addTreeRoot(const QString &name, const QString &desc)
{
    auto item = new QTreeWidgetItem(ui->assetTree);
    item->setText(0, name);

//    addTreeChild(item, "HMMM");
}

void AssetWidget::addTreeChild(QTreeWidgetItem *parent, const QString &name, const QString &desc)
{
    auto item = new QTreeWidgetItem();
    item->setText(0, name);

    parent->addChild(item);
}
