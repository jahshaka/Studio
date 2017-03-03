/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "worldpropertywidget.h"
#include "../texturepicker.h"
#include "../../irisgl/src/core/scene.h"
#include "../../irisgl/src/materials/defaultskymaterial.h"

#include "../colorvaluewidget.h"
#include "../colorpickerwidget.h"
#include "../hfloatslider.h"
#include "../checkboxproperty.h"
#include "../colorwidget.hpp"

WorldPropertyWidget::WorldPropertyWidget()
{
    this->setContentTitle("Sky and Lighting");

    skyColor = this->addColorPicker("Sky Color");
    ambientColor = this->addColorPicker("Ambient Color");
    skyTexture = this->addTexturePicker("Sky Texture");

    fogEnabled = this->addCheckBox("Enabled",false);
    fogColor = this->addColorPicker("Fog Color");
    fogStart = this->addFloatValueSlider("Fog Start",0,1000);
    fogEnd = this->addFloatValueSlider("Fog End",0,1000);
    shadowEnabled = this->addCheckBox("Enable Shadows", true);


    connect(skyTexture,SIGNAL(valueChanged(QString)),SLOT(onSkyTextureChanged(QString)));
    connect(skyColor->getPicker(),SIGNAL(onColorChanged(QColor)),SLOT(onSkyColorChanged(QColor)));
    connect(ambientColor->getPicker(),SIGNAL(onColorChanged(QColor)),SLOT(onAmbientColorChanged(QColor)));

    connect(fogColor->getPicker(),SIGNAL(onColorChanged(QColor)),SLOT(onFogColorChanged(QColor)));
    connect(fogStart,SIGNAL(valueChanged(float)),SLOT(onFogStartChanged(float)));
    connect(fogEnd,SIGNAL(valueChanged(float)),SLOT(onFogEndChanged(float)));
    connect(fogEnabled,SIGNAL(valueChanged(bool)),SLOT(onFogEnabledChanged(bool)));
    connect(shadowEnabled,SIGNAL(valueChanged(bool)),SLOT(onShadowEnabledChanged(bool)));
}

void WorldPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if(!!scene)
    {
        this->scene = scene;
        auto skyTex = scene->skyTexture;
        if(!!skyTex)
            skyTexture->setTexture(skyTex->getSource());

        skyColor->setColorValue(scene->skyColor);
        ambientColor->setColorValue(scene->ambientColor);

        fogColor->setColorValue(scene->fogColor);
        fogStart->setValue(scene->fogStart);
        fogEnd->setValue(scene->fogEnd);
        fogEnabled->setValue(scene->fogEnabled);
        shadowEnabled->setValue(scene->shadowEnabled);

    }
    else
    {
        this->scene.clear();
        //return;
        //todo: clear ui
    }


}

void WorldPropertyWidget::onSkyTextureChanged(QString texPath)
{
    if(texPath.isEmpty())
    {
        scene->setSkyTexture(iris::Texture2DPtr());

    }
    else
    {
        scene->setSkyTexture(iris::Texture2D::load(texPath,false));
    }
}

void WorldPropertyWidget::onSkyColorChanged(QColor color)
{
    scene->setSkyColor(color);
}

void WorldPropertyWidget::onAmbientColorChanged(QColor color)
{
    //scene->set
    scene->setAmbientColor(color);
}

void WorldPropertyWidget::onFogColorChanged(QColor color)
{
    if(!!scene)
        scene->fogColor = color;
}

void WorldPropertyWidget::onFogStartChanged(float val)
{
    if(!!scene)
        scene->fogStart = val;
}

void WorldPropertyWidget::onFogEndChanged(float val)
{
    if(!!scene)
        scene->fogEnd = val;
}

void WorldPropertyWidget::onFogEnabledChanged(bool val)
{
    if(!!scene)
        scene->fogEnabled = val;
}

void WorldPropertyWidget::onShadowEnabledChanged(bool val)
{
    if(!!scene)
        scene->shadowEnabled = val;
}

