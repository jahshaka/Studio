/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDGIPROPERTYWIDGET_H
#define WORLDGIPROPERTYWIDGET_H

#include "irisgl/core/math/vec.h"
#include <QWidget>
#include "ui/controls/accordionbladewidget.h"
#include "irisgl/irisglfwd.h"

class ComboBoxWidget;
class HFloatSliderWidget;
class CheckBoxWidget;
class DragVector3Widget;
class QPushButton;

/**
 * World-panel "Global Illumination" section — RAYON (GI_UNIFIED_SPEC.md §2).
 *
 * THREE VISIBLE ROWS and nothing else: the Rayon switch, the quality tier, and
 * the GI update budget. Everything the tier consumes — the technique picker,
 * the voxel/probe quality, bounces, the lit volume, the probe grid, the
 * irradiance field and its intensity — moves under an "Advanced" disclosure,
 * where each row still works exactly as it did and each edit still PINS itself
 * (worldmodes' override model: a pin survives tier switches, and the tier row
 * says "Custom" while one deviates).
 *
 * Nothing was deleted to get here. The tier is a registry row like every other
 * quality row, so this panel writes through worldmodes and the same state is
 * reachable from world.rayon, world.gi, world.settings and world.override.
 */
class WorldGiPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldGiPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);

protected slots:
    void onRayonToggled(bool on);
    void onTierChanged(int row);
    void onAdvancedToggled(bool on);
    void modeChanged(int row);
    void onQualityChanged(int row);
    void onLightChanged(int row);
    void onBouncesChanged(float value);
    void onBoundsMinChanged(iris::Vec3 value);
    void onBoundsMaxChanged(iris::Vec3 value);
    void onPccGridChanged(iris::Vec3 value);
    void onUpdateBudgetChanged(float value);
    void onDdgiToggled(bool on);
    void onDdgiIntensityChanged(float value);
    void onDdgiAmbientChanged(float value);
    void onDdgiSourceChanged(int index);
    void onFitBoundsClicked();
    void onResetAdvancedClicked();

private:
    void rebuild();

    QSharedPointer<iris::Scene> scene;
    CheckBoxWidget *rayonSwitch = nullptr;
    ComboBoxWidget *tierSelector = nullptr;
    ComboBoxWidget *modeSelector = nullptr;
    ComboBoxWidget *quality = nullptr;
    ComboBoxWidget *lightSelector = nullptr;
    HFloatSliderWidget *bounces = nullptr;
    DragVector3Widget *boundsMin = nullptr;
    DragVector3Widget *boundsMax = nullptr;
    DragVector3Widget *pccGrid = nullptr;
    HFloatSliderWidget *updateBudget = nullptr;
    CheckBoxWidget *ddgiToggle = nullptr;
    HFloatSliderWidget *ddgiIntensity = nullptr;
    HFloatSliderWidget *ddgiAmbient = nullptr;
    ComboBoxWidget *ddgiSource = nullptr;
    QPushButton *fitBoundsButton = nullptr;
    QPushButton *advancedButton = nullptr;
    QPushButton *resetAdvancedButton = nullptr;
    /// Disclosure state, per session (the panel is rebuilt on every edit).
    bool advancedOpen = false;
};

#endif // WORLDGIPROPERTYWIDGET_H
