/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "worldlayerwidget.h"
#include "ui_worldlayerwidget.h"
#include "scenemanager.h"
#include "scenenode.h"
#include "scenenodes/skyboxnode.h"
#include <QFileInfo>
#include <QFileDialog>
#include <QGridLayout>
#include <QListWidgetItem>

WorldLayerWidget::WorldLayerWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::WorldLayerWidget)
{
    ui->setupUi(this);
    scene = nullptr;
    renderer = nullptr;

    connect(ui->chooseSkyButton,SIGNAL(clicked(bool)),this,SLOT(chooseSky()));
    connect(ui->clearSkyButton,SIGNAL(clicked(bool)),this,SLOT(clearSky()));
    //connect(ui->skyBg,SIGNAL(setColor(QColor)),this,SLOT(setColor(QColor)));
    connect(ui->gridCheck,SIGNAL(stateChanged(int)),this,SLOT(toggleGridVisbility(int)));
    connect(ui->skyList,SIGNAL(itemClicked(QListWidgetItem*)),this,SLOT(listItemClicked(QListWidgetItem*)));

    auto listWidget = ui->skyList;
    listWidget->setFlow(QListView::LeftToRight);
    listWidget->setResizeMode(QListView::Adjust);
    listWidget->setIconSize(QSize(170,100));
    listWidget->setDragEnabled(false);
    listWidget->setDragDropMode(QAbstractItemView::NoDragDrop);

    listWidget->setViewMode(QListView::IconMode);
    listWidget->setMovement(QListView::Static);
    listWidget->setSelectionBehavior(QAbstractItemView::SelectItems);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);


    addPresetSky("app/content/skies/default.png");
    addPresetSky("app/content/skies/vp_sky_v2_002.jpg");
    addPresetSky("app/content/skies/vp_sky_v2_032.jpg");
    addPresetSky("app/content/skies/vp_sky_v2_033.jpg");
    addPresetSky("app/content/skies/vp_sky_v3_007.jpg");
    addPresetSky("app/content/skies/vp_sky_v3_015.jpg");
    addPresetSky("app/content/skies/vp_sky_v3_047.jpg");


    ui->fogStart->setValueRange(0,500);
    ui->fogEnd->setValueRange(0,500);

    ui->fogStart->setLabelText("");
    ui->fogEnd->setLabelText("");

    connect(ui->fogColorPicker,SIGNAL(onSetColor(QColor)),this,SLOT(fogColorChanged(QColor)));
    connect(ui->fogColorPicker,SIGNAL(onColorChanged(QColor)),this,SLOT(fogColorChanged(QColor)));
    //connect(ui->fogColorPicker,SIGNAL(onFogColorChanged(QColor)),this,SLOT(fogColorChanged(QColor)));
    connect(ui->fogStart,SIGNAL(valueChanged(int)),this,SLOT(fogStartChanged(int)));
    connect(ui->fogEnd,SIGNAL(valueChanged(int)),this,SLOT(fogEndChanged(int)));
    connect(ui->fogEnabled,SIGNAL(toggled(bool)),this,SLOT(fogEnabledChanged(bool)));
}

WorldLayerWidget::~WorldLayerWidget()
{
    delete ui;
}

void WorldLayerWidget::addPresetSky(QString file)
{
    QImage image;
    image.load(file);
    QImage scaled = image.scaled(500,250);

    auto item = new QListWidgetItem();
    item->setIcon(QIcon(QPixmap::fromImage(scaled)));
    item->setData(100,QVariant(file));

    ui->skyList->addItem(item);
    presets.append(file);
}

void WorldLayerWidget::setScene(SceneManager* scene)
{
    this->scene = scene;

    if(renderer!=nullptr)
        renderer->setClearColor(scene->bgColor);
    auto world = static_cast<WorldNode*>(this->scene->getRootNode());
    this->ui->gridCheck->setChecked(world->isGridVisible());

    ui->fogEnabled->setChecked(scene->getFogEnabled());
    ui->fogColorPicker->setColor(scene->getFogColor());
    ui->fogStart->setValue(scene->getFogStart());
    ui->fogEnd->setValue(scene->getFogEnd());
}

void WorldLayerWidget::setRenderer(Qt3DExtras::QForwardRenderer* renderer)
{
    this->renderer = renderer;
    if(scene!=nullptr)
        renderer->setClearColor(scene->bgColor);
}

void WorldLayerWidget::setBackgroundR(int col)
{
    this->scene->bgColor.setRed(col);
    renderer->setClearColor(scene->bgColor);
}

void WorldLayerWidget::setBackgroundG(int col)
{
    this->scene->bgColor.setGreen(col);
    renderer->setClearColor(scene->bgColor);
}

void WorldLayerWidget::setBackgroundB(int col)
{
    this->scene->bgColor.setBlue(col);
    renderer->setClearColor(scene->bgColor);
}

void WorldLayerWidget::chooseSky()
{
    auto file = QFileDialog::getOpenFileName(this,"Choose Sky Image","","Image Files (*.jpg *.png *.bmp *.tga)");
    if(file.isEmpty())
        return;

    auto fileInfo = QFileInfo(file);
    auto extension = fileInfo.completeSuffix();
    auto folder = fileInfo.absolutePath();

    setSky(file);
}

void WorldLayerWidget::setSky(QString file)
{
    static_cast<WorldNode*>(this->scene->getRootNode())->getSky()->setTexture(file);

    QImage image;
    image.load(file);
    auto scaled = image.scaled(ui->skyLabel->width(),ui->skyLabel->height());
    ui->skyLabel->setPixmap(QPixmap::fromImage(scaled));
}

void WorldLayerWidget::clearSky()
{
    this->scene->clearSky();
    ui->skyLabel->clear();

    static_cast<WorldNode*>(this->scene->getRootNode())->getSky()->clearTexture();
}

void WorldLayerWidget::setColor(QColor color)
{
    this->scene->bgColor = color;
    renderer->setClearColor(color);
}

void WorldLayerWidget::toggleGridVisbility(int state)
{
    Q_UNUSED(state);

    if(ui->gridCheck->isChecked())
        static_cast<WorldNode*>(this->scene->getRootNode())->showGrid();
    else
        static_cast<WorldNode*>(this->scene->getRootNode())->hideGrid();
}

void WorldLayerWidget::listItemClicked(QListWidgetItem* item)
{
    QString file = item->data(100).toString();
    this->setSky(file);
}

void WorldLayerWidget::fogColorChanged(QColor color)
{
    scene->setFogColor(color);
    //ui->fogColorPicker->setColor(color);
}

void WorldLayerWidget::fogStartChanged(int fogStart)
{
    scene->setFogStart(fogStart);
}

void WorldLayerWidget::fogEndChanged(int fogEnd)
{
    scene->setFogEnd(fogEnd);
}

void WorldLayerWidget::fogEnabledChanged(bool enabled)
{
    scene->setFogEnabled(enabled);
}
