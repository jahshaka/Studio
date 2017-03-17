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
#include "../irisgl/src/core/scene.h"
#include "../irisgl/src/materials/defaultskymaterial.h"
#include "../irisgl/src/core/irisutils.h"
#include "../irisgl/src/graphics/texture2d.h"

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

//    addSky(":/app/content/skies/vp_sky_v2_033.jpg", "Sky 3");
//    addSky(":/app/content/skies/vp_sky_v2_002.jpg", "Sky 1");
//    addSky(":/app/content/skies/vp_sky_v2_032.jpg", "Sky 2");
//    addSky(":/app/content/skies/vp_sky_v2_033.jpg", "Sky 3");
//    addSky(":/app/content/skies/vp_sky_v2_002_test.jpg", "Fading Sky");

    QString cove = IrisUtils::getAbsoluteAssetPath("app/content/textures/front.jpg");
    QString dessert = IrisUtils::getAbsoluteAssetPath("app/content/skies/alternative/ame_desert/front.png");
    QString lake = IrisUtils::getAbsoluteAssetPath("app/content/skies/alternative/yokohama/front.jpg");
    QString field = IrisUtils::getAbsoluteAssetPath("app/content/skies/alternative/field/front.jpg");
    QString creek = IrisUtils::getAbsoluteAssetPath("app/content/skies/alternative/creek/front.jpg");

//    alternativeSkies.append(x1);
    addCubeSky(cove, "Cove");
    addCubeSky(dessert, "Dessert");
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

    mainWindow->getScene()->setSkyTexture(iris::Texture2D::createCubeMap(x1, x2, y1, y2, z1, z2));
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
