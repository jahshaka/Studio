/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "endlessplanelayerwidget.h"
#include "ui_endlessplanelayerwidget.h"
#include "../scenegraph/scenenodes.h"
#include <QFileDialog>

EndlessPlaneLayerWidget::EndlessPlaneLayerWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::EndlessPlaneLayerWidget)
{
    ui->setupUi(this);

    plane = nullptr;
    world = nullptr;
    connect(ui->pushButton,SIGNAL(clicked(bool)),this,SLOT(chooseTexture()));
    connect(ui->scaleSlider,SIGNAL(valueChanged(int)),this,SLOT(setTextureScale(int)));
    connect(ui->showCheckBox,SIGNAL(pressed()),this,SLOT(changeVisibility()));
    //connect(ui->showCheckBox,SIGNAL(toggled(bool)),this,SLOT(changeVisibility()));
}

EndlessPlaneLayerWidget::~EndlessPlaneLayerWidget()
{
    delete ui;
}

void EndlessPlaneLayerWidget::setPlane(EndlessPlaneNode* node)
{
    plane = node;
    ui->lineEdit->setText(plane->getTexture());
    ui->scaleSlider->setValue(plane->getTextureScale()*100);

}

void EndlessPlaneLayerWidget::setWorld(WorldNode* node)
{
    world = node;
    ui->showCheckBox->setChecked(world->isGroundVisible());
}

void EndlessPlaneLayerWidget::setTextureScale(int value)
{
    if(plane!=nullptr)
    {
        plane->setTextureScale(value/100.0f);
    }
}

void EndlessPlaneLayerWidget::changeVisibility()
{
    if(ui->showCheckBox->isChecked())
        world->hideGround();
    else
        world->showGround();
}

void EndlessPlaneLayerWidget::chooseTexture()
{
    auto path = QFileDialog::getOpenFileName(this,"Select Texture",QDir::currentPath(),"Image Files (*.jpg *.png *.bmp *.tga)");
    if(path.isEmpty())
        return;

    plane->setTexture(path);
    ui->lineEdit->setText(path);
}
