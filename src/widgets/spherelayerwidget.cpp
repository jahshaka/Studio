/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "spherelayerwidget.h"
#include "ui_spherelayerwidget.h"
#include "../scenegraph/scenenodes.h"

SphereLayerWidget::SphereLayerWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SphereLayerWidget)
{
    ui->setupUi(this);

    connect(ui->rings,SIGNAL(valueChanged(int)),this,SLOT(sliderRings(int)));
    connect(ui->slices,SIGNAL(valueChanged(int)),this,SLOT(sliderSlices(int)));
}

SphereLayerWidget::~SphereLayerWidget()
{
    delete ui;
}

void SphereLayerWidget::setSphere(SphereNode* node)
{
    sphere = node;
    ui->slices->setValue(sphere->sphere->slices());
    ui->rings->setValue(sphere->sphere->rings());
}

void SphereLayerWidget::sliderRings(int val)
{
    if(sphere==nullptr)
        return;

    //todo: ensure value is at minimum
    sphere->sphere->setRings(val);
}

void SphereLayerWidget::sliderSlices(int val)
{
    if(sphere==nullptr)
        return;
    sphere->sphere->setSlices(val);
}
