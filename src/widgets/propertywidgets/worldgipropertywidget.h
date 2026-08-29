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
#include "../accordianbladewidget.h"
#include "../../irisgl/src/irisglfwd.h"

class ComboBoxWidget;
class HFloatSliderWidget;

/**
 * World-panel "Global Illumination" section: how light bounces around the
 * scene. A mode dropdown plus only the controls the chosen mode actually uses
 * (rebuilt on change, like the sky panel). Bounced Light (Instant Radiosity)
 * is live; the voxel modes are announced but honestly marked coming soon —
 * only their bounces + bounds settings are editable (they serialize today).
 * Rendered by the engine viewport; the legacy viewport ignores it.
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

private:
    void rebuild();

    QSharedPointer<iris::Scene> scene;
    ComboBoxWidget *modeSelector = nullptr;
    ComboBoxWidget *quality = nullptr;
    ComboBoxWidget *lightSelector = nullptr;
    HFloatSliderWidget *bounces = nullptr;
    Widget3D *boundsMin = nullptr;
    Widget3D *boundsMax = nullptr;
};

#endif // WORLDGIPROPERTYWIDGET_H
