/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "skypresets.h"
#include "ui_skypresets.h"

#include "../mainwindow.h"
#include "../irisgl/src/scenegraph/scene.h"
#include "../irisgl/src/materials/defaultskymaterial.h"
#include "../irisgl/src/core/irisutils.h"
#include "../irisgl/src/graphics/texture2d.h"

#include <QResource>

SkyPresets::SkyPresets(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SkyPresets)
{
    ui->setupUi(this);

    mainWindow = nullptr;

    ui->skyList->setViewMode(QListWidget::IconMode);
    ui->skyList->setIconSize(QSize(64, 64));
    ui->skyList->setResizeMode(QListWidget::Adjust);
    ui->skyList->setMovement(QListView::Static);
    ui->skyList->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->skyList->setSelectionMode(QAbstractItemView::SingleSelection);

    QString cove = ":/app/content/skies/alternative/cove/front.jpg";
    QString dessert = ":/app/content/skies/alternative/ame_desert/front.png";
    QString lake = ":/app/content/skies/alternative/yokohama/front.jpg";
    QString field = ":/app/content/skies/alternative/field/front.jpg";
    QString creek = ":/app/content/skies/alternative/creek/front.jpg";

    addCubeSky(cove, "Cove");
    addCubeSky(dessert, "Hamarikyu");
    addCubeSky(lake, "Bay");
    addCubeSky(field, "Field");
    addCubeSky(creek, "Creek");

    connect(ui->skyList,    SIGNAL(itemClicked(QListWidgetItem*)),
            this,           SLOT(applyCubeSky(QListWidgetItem*)));
}

SkyPresets::~SkyPresets()
{
    delete ui;
}

void SkyPresets::addSky(QString path, QString name)
{
    skies.append(path);

    auto item = new QListWidgetItem(QIcon(path), name);
    item->setData(Qt::UserRole, skies.count() - 1);
    ui->skyList->addItem(item);
}

void SkyPresets::addCubeSky(QString path, QString name)
{
    alternativeSkies.append(path);

    auto item = new QListWidgetItem(QIcon(path), name);
    item->setData(Qt::UserRole, alternativeSkies.count() - 1);
    ui->skyList->addItem(item);
}

void SkyPresets::applyCubeSky(QListWidgetItem* item)
{
    if (!mainWindow) return;

    auto sky = alternativeSkies[item->data(Qt::UserRole).toInt()];

    auto fInfo = QFileInfo(sky);
    auto path = fInfo.path();
    auto ext = fInfo.suffix();

    auto x1 = path + "/left." + ext;
    auto x2 = path + "/right." + ext;
    auto y1 = path + "/top." + ext;
    auto y2 = path + "/bottom." + ext;
    auto z1 = path + "/front." + ext;
    auto z2 = path + "/back." + ext;

    mainWindow->getScene()->skyBoxTextures[0] = z1;
    mainWindow->getScene()->skyBoxTextures[1] = z2;
    mainWindow->getScene()->skyBoxTextures[2] = y1;
    mainWindow->getScene()->skyBoxTextures[3] = y2;
    mainWindow->getScene()->skyBoxTextures[4] = x1;
    mainWindow->getScene()->skyBoxTextures[5] = x2;

    auto img = new QImage(z1);

    mainWindow->getScene()->setSkyTexture(iris::Texture2D::createCubeMap(z1, z2, y1, y2, x1, x2, img));
    mainWindow->getScene()->setSkyTextureSource(z1);
    mainWindow->getScene()->setSkyColor(QColor(255, 255, 255));
}

void SkyPresets::applySky(QListWidgetItem* item)
{
    if (!mainWindow) return;

    auto sky = skies[item->data(Qt::UserRole).toInt()];

    mainWindow->getScene()->setSkyTexture(iris::Texture2D::load(sky, false));
    mainWindow->getScene()->setSkyColor(QColor(255, 255, 255));
}
