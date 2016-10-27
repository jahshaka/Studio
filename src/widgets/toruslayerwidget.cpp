/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "toruslayerwidget.h"
#include "ui_toruslayerwidget.h"
#include "../scenegraph/scenenodes.h"

TorusLayerWidget::TorusLayerWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TorusLayerWidget)
{
    ui->setupUi(this);

    torus = nullptr;

    //bind callbacks
    //connect(ui->innerRadius,SIGNAL(valueChanged(int)),this,SLOT(sliderTorusMinorRadius(int)));
    connect(ui->rings,SIGNAL(valueChanged(int)),this,SLOT(sliderTorusRings(int)));
    connect(ui->slices,SIGNAL(valueChanged(int)),this,SLOT(sliderTorusSlices(int)));
}

void TorusLayerWidget::initCallbacks()
{
}

TorusLayerWidget::~TorusLayerWidget()
{
    delete ui;
}

void TorusLayerWidget::setTorus(TorusNode* node)
{
    torus = node;
    ui->innerRadius->setValue(torus->torus->minorRadius()*100);
    ui->slices->setValue(torus->torus->slices());
    ui->rings->setValue(torus->torus->rings());
}

void TorusLayerWidget::sliderTorusMinorRadius(int val)
{
    if(torus==nullptr)//just in case
        return;
    torus->torus->setMinorRadius(val/100.0f);
}

void TorusLayerWidget::sliderTorusRings(int val)
{
    if(torus==nullptr)//just in case
        return;
    torus->torus->setRings(val);
}

void TorusLayerWidget::sliderTorusSlices(int val)
{
    if(torus==nullptr)//just in case
        return;
    torus->torus->setSlices(val);
}
