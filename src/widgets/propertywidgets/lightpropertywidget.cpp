/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "../accordianbladewidget.h"
#include <QWidget>

#include "lightpropertywidget.h"
#include "../hfloatsliderwidget.h"
#include "../colorpickerwidget.h"
#include "../colorvaluewidget.h"

#include "../../irisgl/src/core/scene.h"
#include "../../irisgl/src/core/scenenode.h"
#include "../../irisgl/src/scenegraph/lightnode.h"


LightPropertyWidget::LightPropertyWidget(QWidget* parent):
    AccordianBladeWidget(parent)
{
    lightColor = this->addColorPicker("Color");
    intensity = this->addFloatValueSlider("Intensity",0,10);
    distance = this->addFloatValueSlider("Distance",0,100);
    spotCutOff = this->addFloatValueSlider("Spotlight CutOff",0,180);
    spotCutOffSoftness = this->addFloatValueSlider("Spotlight Softness",0.1f,90);


    connect(lightColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(lightColorChanged(QColor)));
    connect(lightColor->getPicker(),SIGNAL(onSetColor(QColor)),this,SLOT(lightColorChanged(QColor)));

    connect(intensity,SIGNAL(valueChanged(float)),this,SLOT(lightIntensityChanged(float)));
    connect(distance,SIGNAL(valueChanged(float)),this,SLOT(lightDistanceChanged(float)));
    connect(spotCutOff,SIGNAL(valueChanged(float)),this,SLOT(lightSpotCutoffChanged(float)));
    connect(spotCutOffSoftness,SIGNAL(valueChanged(float)),this,SLOT(lightSpotCutoffSoftnessChanged(float)));
}

void LightPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    //this->sceneNode = sceneNode;
    if(!!sceneNode && sceneNode->getSceneNodeType()==iris::SceneNodeType::Light)
    {
        lightNode = sceneNode.staticCast<iris::LightNode>();

        //apply properties to ui
        lightColor->setColorValue(lightNode->color);
        intensity->setValue(lightNode->intensity);
        distance->setValue(lightNode->distance);
        spotCutOff->setValue(lightNode->spotCutOff);
    }
    else
    {
        lightNode.clear();
    }
}

void LightPropertyWidget::lightColorChanged(QColor color)
{
    if(!!lightNode)
        lightNode->color = color;
}

void LightPropertyWidget::lightIntensityChanged(float intensity)
{
    if(!!lightNode)
        lightNode->intensity = intensity;
}

void LightPropertyWidget::lightDistanceChanged(float distance)
{
    if(!!lightNode)
        lightNode->distance = distance;
}

void LightPropertyWidget::lightSpotCutoffChanged(float spotCutOff)
{
    if(!!lightNode)
        lightNode->spotCutOff = spotCutOff;
}

void LightPropertyWidget::lightSpotCutoffSoftnessChanged(float spotCutOffSoftness)
{
    if(!!lightNode)
        lightNode->spotCutOffSoftness = spotCutOffSoftness;
}



