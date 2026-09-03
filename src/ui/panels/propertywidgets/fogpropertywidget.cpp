/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/fogpropertywidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "irisgl/document/scenegraph/scene.h"

#include "ui/controls/colorvaluewidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/hfloatsliderwidget.h"

#include "ui/controls/checkboxwidget.h"

FogPropertyWidget::FogPropertyWidget()
{
    fogEnabled      = this->addCheckBox("Fog Enabled", false);
    fogColor        = this->addColorPicker("Fog Color");

    // Density is small by nature (the default 100..180 linear fog maps to 0.0071),
    // so the row needs real decimals — Qt's spinbox default of 2 would round it
    // to a two-step control.
    fogDensity      = this->addFloatValueSlider("Fog Density", 0.f, 0.5f);
    fogDensity->setDecimals(4);
    fogDensity->setToolTip(QStringLiteral(
        "How much of a surface's colour is lost per world unit. A surface 1/density units away "
        "keeps half its colour, and by 4.32/density it has all but disappeared. 0.01 = half gone "
        "at 100 units."));

    // The linear pair's near distance. Exponential fog has no start: it begins at
    // the camera and never stops. Kept visible, and disabled, so the row's
    // disappearance is not mistaken for a lost setting — and SAY SO in the label,
    // because the app's stylesheet paints row labels at a fixed colour and a
    // disabled row is otherwise indistinguishable from a live one.
    fogStart        = this->addFloatValueSlider("Fog Start (unused)", 0, 1000.f);
    fogStart->setEnabled(false);
    fogStart->setToolTip(QStringLiteral(
        "Not used any more. Fog is exponential now — it starts at the camera and thickens with "
        "distance, so there is no start distance. The value is still saved with the scene."));

    fogHeightDensity = this->addFloatValueSlider("Height Fog Density", 0.f, 0.5f);
    fogHeightDensity->setDecimals(4);
    fogHeightDensity->setToolTip(QStringLiteral(
        "A second layer of fog, the same colour, whose density falls off with height — ground "
        "mist and valley fog. 0 turns it off entirely."));
    fogHeightFalloff = this->addFloatValueSlider("Height Falloff", 0.f, 2.f);
    fogHeightFalloff->setDecimals(3);
    fogHeightFalloff->setToolTip(QStringLiteral(
        "How fast the height layer thins out as you rise. Larger = a shallower, sharper-edged "
        "layer; the density halves every 1/falloff units above the level below."));
    fogHeightLevel   = this->addFloatValueSlider("Height Level", -100.f, 100.f);
    fogHeightLevel->setDecimals(2);
    fogHeightLevel->setToolTip(QStringLiteral(
        "The world height at which the height layer has its full density — the surface of the "
        "mist, usually the ground."));

    fogBreakBrightness = this->addFloatValueSlider("Breakthrough Brightness", 0.f, 4.f);
    fogBreakBrightness->setDecimals(3);
    fogBreakBrightness->setToolTip(QStringLiteral(
        "How bright a pixel has to be before it starts cutting through the fog instead of "
        "dissolving into it — the sun, a lamp, an emissive sign."));
    fogBreakFalloff    = this->addFloatValueSlider("Breakthrough Falloff", 0.f, 2.f);
    fogBreakFalloff->setDecimals(3);
    fogBreakFalloff->setToolTip(QStringLiteral(
        "How sharply bright pixels break through. 0 switches breakthrough off, leaving plain "
        "exponential fog."));

    shadowEnabled   = this->addCheckBox("Enable Shadows", true);

    connect(fogColor->getPicker(),  SIGNAL(onColorChanged(QColor)), SLOT(onFogColorChanged(QColor)));
    connect(fogDensity,             SIGNAL(valueChanged(float)),    SLOT(onFogDensityChanged(float)));
    connect(fogHeightDensity,       SIGNAL(valueChanged(float)),    SLOT(onFogHeightDensityChanged(float)));
    connect(fogHeightFalloff,       SIGNAL(valueChanged(float)),    SLOT(onFogHeightFalloffChanged(float)));
    connect(fogHeightLevel,         SIGNAL(valueChanged(float)),    SLOT(onFogHeightLevelChanged(float)));
    connect(fogBreakBrightness,     SIGNAL(valueChanged(float)),    SLOT(onFogBreakBrightnessChanged(float)));
    connect(fogBreakFalloff,        SIGNAL(valueChanged(float)),    SLOT(onFogBreakFalloffChanged(float)));
    connect(fogEnabled,             SIGNAL(valueChanged(bool)),     SLOT(onFogEnabledChanged(bool)));
    connect(shadowEnabled,          SIGNAL(valueChanged(bool)),     SLOT(onShadowEnabledChanged(bool)));
}

void FogPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;

        fogColor->setColorValue(scene->fogColor);
        fogDensity->setValue(scene->fogDensity);
        fogStart->setValue(scene->fogStart);
        fogHeightDensity->setValue(scene->fogHeightDensity);
        fogHeightFalloff->setValue(scene->fogHeightFalloff);
        fogHeightLevel->setValue(scene->fogHeightLevel);
        fogBreakBrightness->setValue(scene->fogBreakMinBrightness);
        fogBreakFalloff->setValue(scene->fogBreakFalloff);
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

void FogPropertyWidget::onFogDensityChanged(float val)
{
    if (!!scene) {
        scene->fogDensity = val;
    }
}

void FogPropertyWidget::onFogHeightDensityChanged(float val)
{
    if (!!scene) {
        scene->fogHeightDensity = val;
    }
}

void FogPropertyWidget::onFogHeightFalloffChanged(float val)
{
    if (!!scene) {
        scene->fogHeightFalloff = val;
    }
}

void FogPropertyWidget::onFogHeightLevelChanged(float val)
{
    if (!!scene) {
        scene->fogHeightLevel = val;
    }
}

void FogPropertyWidget::onFogBreakBrightnessChanged(float val)
{
    if (!!scene) {
        scene->fogBreakMinBrightness = val;
    }
}

void FogPropertyWidget::onFogBreakFalloffChanged(float val)
{
    if (!!scene) {
        scene->fogBreakFalloff = val;
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
