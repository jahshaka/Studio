/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "fogpropertywidget.h"
#include "../texturepickerwidget.h"
#include "../../irisgl/src/core/scene.h"
#include "../../irisgl/src/materials/defaultskymaterial.h"

#include "../colorvaluewidget.h"
#include "../colorpickerwidget.h"
#include "../hfloatsliderwidget.h"

#include "../checkboxwidget.h"

FogPropertyWidget::FogPropertyWidget()
{
    fogEnabled      = this->addCheckBox("Fog Enabled", false);
    fogColor        = this->addColorPicker("Fog Color");
    fogStart        = this->addFloatValueSlider("Fog Start", 0, 1000.f);
    fogEnd          = this->addFloatValueSlider("Fog End", 0, 1000.f);
    shadowEnabled   = this->addCheckBox("Enable Shadows", true);

    connect(fogColor->getPicker(),  SIGNAL(onColorChanged(QColor)), SLOT(onFogColorChanged(QColor)));
    connect(fogStart,               SIGNAL(valueChanged(float)),    SLOT(onFogStartChanged(float)));
    connect(fogEnd,                 SIGNAL(valueChanged(float)),    SLOT(onFogEndChanged(float)));
    connect(fogEnabled,             SIGNAL(valueChanged(bool)),     SLOT(onFogEnabledChanged(bool)));
    connect(shadowEnabled,          SIGNAL(valueChanged(bool)),     SLOT(onShadowEnabledChanged(bool)));
}

void FogPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;

        fogColor->setColorValue(scene->fogColor);
        fogStart->setValue(scene->fogStart);
        fogEnd->setValue(scene->fogEnd);
        fogEnabled->setValue(scene->fogEnabled);
        shadowEnabled->setValue(scene->shadowEnabled);
    } else {
        this->scene.clear();
    }
}

void FogPropertyWidget::onFogColorChanged(QColor color)
{
    if (!!scene) {
        scene->fogColor = color;
    }
}

void FogPropertyWidget::onFogStartChanged(float val)
{
    if (!!scene) {
        scene->fogStart = val;
    }
}

void FogPropertyWidget::onFogEndChanged(float val)
{
    if (!!scene) {
        scene->fogEnd = val;
    }
}

void FogPropertyWidget::onFogEnabledChanged(bool val)
{
    if (!!scene) {
        scene->fogEnabled = val;
    }
}

void FogPropertyWidget::onShadowEnabledChanged(bool val)
{
    if (!!scene) {
        scene->shadowEnabled = val;
    }
}
