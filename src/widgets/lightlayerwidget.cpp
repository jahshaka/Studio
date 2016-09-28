/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "lightlayerwidget.h"
#include "ui_lightlayerwidget.h"
#include "../scenegraph/scenenodes.h"

LightLayerWidget::LightLayerWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::LightLayerWidget)
{
    ui->setupUi(this);

    connect(ui->colorWidget,SIGNAL(onColorChanged(QColor)),this,SLOT(sliderLightColor(QColor)));

    connect(ui->intensity,SIGNAL(valueChanged(int)),this,SLOT(sliderLightIntensity(int)));
    light = nullptr;
}

LightLayerWidget::~LightLayerWidget()
{
    delete ui;
}

void LightLayerWidget::setLight(LightNode* light)
{
    this->light = light;

    auto color = light->getColor();
    auto intensity = light->getIntensity();

    ui->colorWidget->setColor(color);

    ui->intensity->setValue(intensity*100);
}

void LightLayerWidget::sliderLightRColor(int val)
{
    if(light!=nullptr)
    {
        auto col = light->getColor();
        col.setRed(val);
        light->setColor(col);
    }
}

void LightLayerWidget::sliderLightGColor(int val)
{
    if(light!=nullptr)
    {
        auto col = light->getColor();
        col.setGreen(val);
        light->setColor(col);
    }
}

void LightLayerWidget::sliderLightBColor(int val)
{
    if(light!=nullptr)
    {
        auto col = light->getColor();
        col.setBlue(val);
        light->setColor(col);
    }
}

void LightLayerWidget::sliderLightIntensity(int val)
{
    if(light!=nullptr)
    {
        light->setIntensity(val/100.0f);
    }
}

void LightLayerWidget::sliderLightColor(QColor col)
{
    if(light!=nullptr)
    {
        light->setColor(col);
    }
}
