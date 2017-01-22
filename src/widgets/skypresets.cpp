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

SkyPresets::SkyPresets(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SkyPresets)
{
    ui->setupUi(this);

    mainWindow = nullptr;

    ui->skyList->setViewMode(QListWidget::IconMode);
    ui->skyList->setIconSize(QSize(110,80));
    ui->skyList->setResizeMode(QListWidget::Adjust);
    ui->skyList->setMovement(QListView::Static);
    ui->skyList->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->skyList->setSelectionMode(QAbstractItemView::SingleSelection);

    /*
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/default.png"),"Default"));
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/vp_sky_v2_002.jpg"),"Sky 1"));
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/vp_sky_v2_032.jpg"),"Sky 2"));
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/vp_sky_v2_033.jpg"),"Sky 3"));
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/vp_sky_v2_002.jpg"),"Sky 1"));
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/vp_sky_v2_032.jpg"),"Sky 2"));
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/vp_sky_v2_033.jpg"),"Sky 3"));
    */

    addSky(":/app/content/skies/default.png","Default");
    addSky(":/app/content/skies/vp_sky_v2_002.jpg","Sky 1");
    addSky(":/app/content/skies/vp_sky_v2_032.jpg","Sky 2");
    addSky(":/app/content/skies/vp_sky_v2_033.jpg","Sky 3");
    addSky(":/app/content/skies/vp_sky_v2_002.jpg","Sky 1");
    addSky(":/app/content/skies/vp_sky_v2_032.jpg","Sky 2");
    addSky(":/app/content/skies/vp_sky_v2_033.jpg","Sky 3");
    addSky(":/app/content/skies/vp_sky_v2_002_test.jpg","Ugly Sky");


    connect(ui->skyList,SIGNAL(itemClicked(QListWidgetItem*)),this,SLOT(applySky(QListWidgetItem*)));
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

void SkyPresets::applySky(QListWidgetItem* item)
{
    if(!mainWindow)
        return;

    auto sky = skies[item->data(Qt::UserRole).toInt()];

    mainWindow->getScene()->setSkyTexture(iris::Texture2D::load(sky,false));
    mainWindow->getScene()->setSkyColor(QColor(255,255,255));
}
