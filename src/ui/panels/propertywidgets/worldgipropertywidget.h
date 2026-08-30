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

#include <QWidget>
#include <QVector3D>
#include "ui/controls/accordionbladewidget.h"
#include "irisgl/irisglfwd.h"

class ComboBoxWidget;
class HFloatSliderWidget;
class CheckBoxWidget;

/**
 * World-panel "Global Illumination" section: how light bounces around the
 * scene. A mode dropdown plus only the controls the chosen mode actually uses
 * (rebuilt on change, like the sky panel). All three modes are live in the
 * engine viewport: Bounced Light (Instant Radiosity), Voxel Lighting (voxel
 * cone tracing over the bounds) and Voxel + Reflections (VCT plus a
 * parallax-corrected reflection-probe grid, blended by distance).
 */
class WorldGiPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldGiPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);

protected slots:
    void modeChanged(int row);
    void onQualityChanged(int row);
    void onLightChanged(int row);
    void onBouncesChanged(float value);
    void onBoundsMinChanged(QVector3D value);
    void onBoundsMaxChanged(QVector3D value);
    void onPccGridChanged(QVector3D value);
    void onAutoRefreshChanged(bool value);

private:
    void rebuild();

    QSharedPointer<iris::Scene> scene;
    ComboBoxWidget *modeSelector = nullptr;
    ComboBoxWidget *quality = nullptr;
    ComboBoxWidget *lightSelector = nullptr;
    HFloatSliderWidget *bounces = nullptr;
    Widget3D *boundsMin = nullptr;
    Widget3D *boundsMax = nullptr;
    Widget3D *pccGrid = nullptr;
    CheckBoxWidget *autoRefresh = nullptr;
};

#endif // WORLDGIPROPERTYWIDGET_H
