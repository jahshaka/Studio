/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef FOGPROPERTYWIDGET_H
#define FOGPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "ui/controls/accordionbladewidget.h"

class ColorValueWidget;
class ColorPickerWidget;
class TexturePicker;

namespace iris {
    class Scene;
    class SceneNode;
    class LightNode;
}

/**
 * The World panel's Fog blade. Fog is EXPONENTIAL (see iris::Scene and
 * jahshaka::engine::FogDesc): a density per world unit, optionally a second
 * height-varying layer, plus the brightness "breakthrough" that keeps bright
 * pixels from dissolving. The old linear Fog Start row survives as a disabled
 * row — the value is still stored and round-tripped, it simply has no meaning
 * for exponential fog, and a silently vanished control reads as a bug.
 */
class FogPropertyWidget: public AccordianBladeWidget
{
    Q_OBJECT

public:
    FogPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);

protected slots:
    void onFogColorChanged(QColor color);
    void onFogDensityChanged(float val);
    void onFogHeightDensityChanged(float val);
    void onFogHeightFalloffChanged(float val);
    void onFogHeightLevelChanged(float val);
    void onFogBreakBrightnessChanged(float val);
    void onFogBreakFalloffChanged(float val);
    void onFogEnabledChanged(bool val);
    void onShadowEnabledChanged(bool val);

private:
    QSharedPointer<iris::Scene> scene;

    CheckBoxWidget* fogEnabled;
    CheckBoxWidget* shadowEnabled;
    HFloatSliderWidget* fogDensity;
    HFloatSliderWidget* fogStart;            // disabled: linear-fog leftover
    HFloatSliderWidget* fogHeightDensity;
    HFloatSliderWidget* fogHeightFalloff;
    HFloatSliderWidget* fogHeightLevel;
    HFloatSliderWidget* fogBreakBrightness;
    HFloatSliderWidget* fogBreakFalloff;
    ColorValueWidget* fogColor;
};

#endif // FOGPROPERTYWIDGET_H
