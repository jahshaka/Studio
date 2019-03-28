/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SKYPROPERTYWIDGET_H
#define SKYPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

#include "irisgl/src/scenegraph/scene.h"

class Database;

class ColorValueWidget;
class ColorPickerWidget;
class TexturePicker;

namespace iris {
    class Scene;
    class SceneNode;
    class LightNode;
}

class SkyPropertyWidget: public AccordianBladeWidget
{
    Q_OBJECT

public:
    SkyPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);
    void setDatabase(Database *);

	void setSky(const QString &guid, iris::SkyType skyType);

protected slots:
    void setEquiMap(const QString &guid);
    void setSkyMap(const QString &guid);
    void skyTypeChanged(int index);

    void onSingleSkyColorChanged(QColor color);
    void onSkyCubeMapChanged(int index);
    void onEquiTextureChanged(QString tex);

    void onReileighChanged(float val);
    void onLuminanceChanged(float val);
    void onTurbidityChanged(float val);
    void onMieCoeffGChanged(float val);
    void onMieDireChanged(float val);
    void onSunPosXChanged(float val);
    void onSunPosYChanged(float val);
    void onSunPosZChanged(float val);

private:
    Database *db;
    QSharedPointer<iris::Scene> scene;
    iris::SkyType currentSky;
	QString skyGuid;

    ComboBoxWidget *skySelector;

    ColorValueWidget *singleColor;

    ComboBoxWidget *cubeSelector;

    TexturePickerWidget *equiTexture;

    ComboBoxWidget *gradientType;
    ColorValueWidget *color1;
    ColorValueWidget *color2;
    HFloatSliderWidget *scale;
    HFloatSliderWidget *angle;

    ComboBoxWidget *shaderSelector;

    HFloatSliderWidget *luminance;
    HFloatSliderWidget *reileigh;
    HFloatSliderWidget *mieCoefficient;
    HFloatSliderWidget *mieDirectionalG;
    HFloatSliderWidget *turbidity;
    HFloatSliderWidget *sunPosX;
    HFloatSliderWidget *sunPosY;
    HFloatSliderWidget *sunPosZ;
};

#endif // SKYPROPERTYWIDGET_H
