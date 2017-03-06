/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef FOGPROPERTYWIDGET_H
#define FOGPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

class ColorValueWidget;
class ColorPickerWidget;
class TexturePicker;

namespace iris {
    class Scene;
    class SceneNode;
    class LightNode;
}

/**
 * This widget displays the properties of the scene.
 */
class FogPropertyWidget: public AccordianBladeWidget
{
    Q_OBJECT

public:
    FogPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);

protected slots:
    void onFogColorChanged(QColor color);
    void onFogStartChanged(float val);
    void onFogEndChanged(float val);
    void onFogEnabledChanged(bool val);
    void onShadowEnabledChanged(bool val);

private:
    QSharedPointer<iris::Scene> scene;

    CheckBoxWidget* fogEnabled;
    CheckBoxWidget* shadowEnabled;
    HFloatSliderWidget* fogStart;
    HFloatSliderWidget* fogEnd;
    ColorValueWidget* fogColor;
};

#endif // FOGPROPERTYWIDGET_H
