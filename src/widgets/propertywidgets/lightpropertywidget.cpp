/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "../accordianbladewidget.h"
#include <QWidget>
#include <QDebug>

#include "lightpropertywidget.h"
#include "../hfloatsliderwidget.h"
#include "../colorpickerwidget.h"
#include "../colorvaluewidget.h"
#include "../comboboxwidget.h"
#include "../checkboxwidget.h"

#include "../../irisgl/src/scenegraph/scene.h"
#include "../../irisgl/src/scenegraph/scenenode.h"
#include "../../irisgl/src/scenegraph/lightnode.h"

#include "../../engine/enginehost.h"


LightPropertyWidget::LightPropertyWidget(QWidget* parent):
    AccordianBladeWidget(parent)
{
    lightColor = this->addColorPicker("Color");
    intensity = this->addFloatValueSlider("Intensity", 0, 10.f);
    distance = this->addFloatValueSlider("Distance", 0, 100.f);
    spotCutOff = this->addFloatValueSlider("Spotlight CutOff", 0, 90.f);
    spotCutOffSoftness = this->addFloatValueSlider("Spotlight Softness", .1f, 90.f);

    // Area lights only (engine viewport): the emitting rectangle and its modes.
    rectWidth = this->addFloatValueSlider("Rect Width", 0.05f, 20.f);
    rectHeight = this->addFloatValueSlider("Rect Height", 0.05f, 20.f);
    doubleSided = this->addCheckBox("Double Sided");
    accurate = this->addCheckBox("Accurate (LTC)");

    shadowType = this->addComboBox("Shadow Type");
    shadowType->addItem("None");
    shadowType->addItem("Hard");
	shadowType->addItem("Soft");
	shadowType->addItem("Very Soft");
    //shadowType->addItem("Softer");
    shadowSize = this->addComboBox("Shadow Size");
    shadowSize->addItem("512");
    shadowSize->addItem("1024");
    shadowSize->addItem("2048");
    shadowSize->addItem("4096");
    //shadowBias = this->addFloatValueSlider("Shadow Bias",0,1);

	shadowAlpha = this->addFloatValueSlider("Shadow Transparency", 0, 1.f);
	shadowColor = this->addColorPicker("Shadow Color");

	// Per-light shadow colour/transparency have no engine equivalent (Ogre-Next's
	// PBR pipeline has no per-light shadow tint — and legacy's own PBR shader
	// ignored the colour too). Hide the controls in engine mode; legacy keeps them.
	mShadowTintSupported = false;  // engine viewport: HlmsPbs has no shadow tint
	if (!mShadowTintSupported) {
		shadowAlpha->hide();
		shadowColor->hide();
	}
	// The legacy renderer never shadowed point lights, so the panel hid their
	// shadow controls. The engine renders point shadows (focused/DPSM maps), so
	// in engine mode Shadow Type and Size stay available for point lights too.
	mPointShadowsSupported = true;

    connect(lightColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(lightColorChanged(QColor)));
    connect(lightColor->getPicker(),SIGNAL(onSetColor(QColor)),this,SLOT(lightColorChanged(QColor)));

    connect(intensity,SIGNAL(valueChanged(float)),this,SLOT(lightIntensityChanged(float)));
    connect(distance,SIGNAL(valueChanged(float)),this,SLOT(lightDistanceChanged(float)));
    connect(spotCutOff,SIGNAL(valueChanged(float)),this,SLOT(lightSpotCutoffChanged(float)));
    connect(spotCutOffSoftness,SIGNAL(valueChanged(float)),this,SLOT(lightSpotCutoffSoftnessChanged(float)));

    connect(rectWidth,SIGNAL(valueChanged(float)),this,SLOT(lightRectWidthChanged(float)));
    connect(rectHeight,SIGNAL(valueChanged(float)),this,SLOT(lightRectHeightChanged(float)));
    connect(doubleSided,SIGNAL(valueChanged(bool)),this,SLOT(lightDoubleSidedChanged(bool)));
    connect(accurate,SIGNAL(valueChanged(bool)),this,SLOT(lightAccurateChanged(bool)));

	connect(shadowAlpha, SIGNAL(valueChanged(float)), this, SLOT(shadowAlphaChanged(float)));
	connect(shadowColor->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(shadowColorChanged(QColor)));
	connect(shadowColor->getPicker(), SIGNAL(onSetColor(QColor)), this, SLOT(shadowColorChanged(QColor)));

    connect(shadowType, SIGNAL(currentIndexChanged(QString)), this, SLOT(shadowTypeChanged(QString)));
    connect(shadowSize, SIGNAL(currentIndexChanged(QString)), this, SLOT(shadowSizeChanged(QString)));
    //connect(shadowBias, SIGNAL(valueChanged(float)), this, SLOT(shadowBiasChanged(float)));
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

		shadowColor->setColorValue(lightNode->shadowColor);
		shadowAlpha->setValue(lightNode->shadowAlpha);

        if (lightNode->getLightType()==iris::LightType::Spot) {
            spotCutOff->show();
            spotCutOffSoftness->show();
        } else {
            spotCutOff->hide();
            spotCutOffSoftness->hide();
        }

        // Area lights: the emitting rectangle replaces the cone controls.
        if (lightNode->getLightType()==iris::LightType::Area) {
            rectWidth->setValue(lightNode->rectWidth);
            rectHeight->setValue(lightNode->rectHeight);
            doubleSided->setValue(lightNode->doubleSided);
            accurate->setValue(lightNode->accurate);
            rectWidth->show();
            rectHeight->show();
            doubleSided->show();
            accurate->show();
        } else {
            rectWidth->hide();
            rectHeight->hide();
            doubleSided->hide();
            accurate->hide();
        }

        shadowSize->setCurrentItem(QString("%1").arg(lightNode->shadowMap->resolution));
        shadowType->setCurrentItem(evalShadowTypeName(lightNode->shadowMap->shadowType));
        //shadowBias->setValue(lightNode->shadowMap->bias);

        // Point lights: legacy never shadowed them (controls hidden); the engine
        // does, so engine mode keeps Shadow Type/Size. Tint stays per-backend.
        if (lightNode->getLightType()==iris::LightType::Area) {
            // Ogre-Next cannot shadow area lights: hide every shadow control.
            shadowSize->hide();
            shadowType->hide();
            shadowColor->hide();
            shadowAlpha->hide();
        } else if (lightNode->getLightType()==iris::LightType::Point) {
            if (mPointShadowsSupported) {
                shadowSize->show();
                shadowType->show();
            } else {
                shadowSize->hide();
                shadowType->hide();
            }
			shadowColor->hide();
			shadowAlpha->hide();
            //shadowBias->hide();
        } else {
            shadowSize->show();
            shadowType->show();
			if (mShadowTintSupported) {
				shadowColor->show();
				shadowAlpha->show();
			}
            //shadowBias->show();
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

void LightPropertyWidget::lightRectWidthChanged(float width)
{
    if(!!lightNode)
        lightNode->rectWidth = width;
}

void LightPropertyWidget::lightRectHeightChanged(float height)
{
    if(!!lightNode)
        lightNode->rectHeight = height;
}

void LightPropertyWidget::lightDoubleSidedChanged(bool doubleSided)
{
    if(!!lightNode)
        lightNode->doubleSided = doubleSided;
}

void LightPropertyWidget::lightAccurateChanged(bool accurate)
{
    if(!!lightNode)
        lightNode->accurate = accurate;
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

void LightPropertyWidget::shadowColorChanged(QColor color)
{
	if (!!lightNode)
		lightNode->shadowColor = color;
}

void LightPropertyWidget::shadowAlphaChanged(float alpha)
{
	if (!!lightNode)
		lightNode->shadowAlpha = alpha;
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
    case iris::ShadowMapType::VerySoft:
        return "Very Soft";
    }

    return "None";
}

iris::ShadowMapType LightPropertyWidget::evalShadowMapType(QString shadowType)
{
    if (shadowType=="Hard")
        return iris::ShadowMapType::Hard;
    if (shadowType=="Soft")
        return iris::ShadowMapType::Soft;
    if (shadowType=="Very Soft")
        return iris::ShadowMapType::VerySoft;

    return iris::ShadowMapType::None;
}
