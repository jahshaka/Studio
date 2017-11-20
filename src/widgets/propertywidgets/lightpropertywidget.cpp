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
#include "../comboboxwidget.h"

#include "../../irisgl/src/scenegraph/scene.h"
#include "../../irisgl/src/scenegraph/scenenode.h"
#include "../../irisgl/src/scenegraph/lightnode.h"


LightPropertyWidget::LightPropertyWidget(QWidget* parent):
    AccordianBladeWidget(parent)
{
    lightColor = this->addColorPicker("Color");
    intensity = this->addFloatValueSlider("Intensity",0,10);
    distance = this->addFloatValueSlider("Distance",0,100);
    spotCutOff = this->addFloatValueSlider("Spotlight CutOff",0,180);
    spotCutOffSoftness = this->addFloatValueSlider("Spotlight Softness",0.1f,90);

    shadowType = this->addComboBox("Shadow Type");
    shadowType->addItem("None");
    shadowType->addItem("Hard");
    shadowType->addItem("Soft");
    //shadowType->addItem("Softer");
    shadowSize = this->addComboBox("Shadow Size");
    shadowSize->addItem("512");
    shadowSize->addItem("1024");
    shadowSize->addItem("2048");
    shadowSize->addItem("4096");
    shadowBias = this->addFloatValueSlider("Shadow Bias",0,1);

    connect(lightColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(lightColorChanged(QColor)));
    connect(lightColor->getPicker(),SIGNAL(onSetColor(QColor)),this,SLOT(lightColorChanged(QColor)));

    connect(intensity,SIGNAL(valueChanged(float)),this,SLOT(lightIntensityChanged(float)));
    connect(distance,SIGNAL(valueChanged(float)),this,SLOT(lightDistanceChanged(float)));
    connect(spotCutOff,SIGNAL(valueChanged(float)),this,SLOT(lightSpotCutoffChanged(float)));
    connect(spotCutOffSoftness,SIGNAL(valueChanged(float)),this,SLOT(lightSpotCutoffSoftnessChanged(float)));

    connect(shadowType, SIGNAL(currentIndexChanged(QString)), this, SLOT(shadowTypeChanged(QString)));
    connect(shadowSize, SIGNAL(currentIndexChanged(QString)), this, SLOT(shadowSizeChanged(QString)));
    connect(shadowBias, SIGNAL(valueChanged(float)), this, SLOT(shadowBiasChanged(float)));
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
        spotCutOffSoftness->setValue(lightNode->spotCutOffSoftness);

        if (lightNode->getLightType()==iris::LightType::Spot) {
            spotCutOff->show();
            spotCutOffSoftness->show();
        } else {
            spotCutOff->hide();
            spotCutOffSoftness->hide();
        }

        shadowSize->setCurrentItem(QString("%1").arg(lightNode->shadowMap->resolution));
        shadowType->setCurrentItem(evalShadowTypeName(lightNode->shadowMap->shadowType));
        shadowBias->setValue(lightNode->shadowMap->bias);

        // hide shadow params for point lights
        if (lightNode->getLightType()==iris::LightType::Point) {
            shadowSize->hide();
            shadowType->hide();
            shadowBias->hide();
        } else {
            shadowSize->show();
            shadowType->show();
            shadowBias->show();
        }
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

void LightPropertyWidget::shadowTypeChanged(QString name)
{
    auto shadowType = evalShadowMapType(name);
    lightNode->shadowMap->shadowType = shadowType;
}

void LightPropertyWidget::shadowSizeChanged(QString size)
{
    int res = size.toInt();
    lightNode->shadowMap->setResolution(res);
}

void LightPropertyWidget::shadowBiasChanged(float bias)
{
    lightNode->shadowMap->bias = bias;
}

QString LightPropertyWidget::evalShadowTypeName(iris::ShadowMapType shadowType)
{
    switch(shadowType){
    case iris::ShadowMapType::None:
        return "None";
    case iris::ShadowMapType::Hard:
        return "Hard";
    case iris::ShadowMapType::Soft:
        return "Soft";
    case iris::ShadowMapType::Softer:
        return "Softer";
    }

    return "None";
}

iris::ShadowMapType LightPropertyWidget::evalShadowMapType(QString shadowType)
{
    if (shadowType=="Hard")
        return iris::ShadowMapType::Hard;
    if (shadowType=="Soft")
        return iris::ShadowMapType::Soft;
    if (shadowType=="Softer")
        return iris::ShadowMapType::Softer;

    return iris::ShadowMapType::None;
}
