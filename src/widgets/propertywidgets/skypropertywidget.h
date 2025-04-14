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

#include <QJsonDocument>
#include <QJsonObject>
#include <QHideEvent>

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

	void setSkyAlongWithProperties(const QString &guid, iris::SkyType skyType);

	void hideEvent(QHideEvent *event);

protected slots:
    void setEquiMap(const QString &guid);
    void setSkyMap(const QJsonObject& definition);
    void setSkyFromCustomMaterial(const QJsonObject& definition);
    void skyTypeChanged(int index);
	void onSlotChanged(QString value, QString guid, int index);
	void onMaterialChanged(int index);
    void onSingleSkyColorChanged(QColor color);
    void onEquiTextureChanged(QString guid);

    void onReileighChanged(float val);
    void onLuminanceChanged(float val);
    void onTurbidityChanged(float val);
    void onMieCoeffGChanged(float val);
    void onMieDireChanged(float val);
    void onSunPosXChanged(float val);
    void onSunPosYChanged(float val);
    void onSunPosZChanged(float val);

	void onGradientTopColorChanged(QColor color);
	void onGradientMidColorChanged(QColor color);
	void onGradientBotColorChanged(QColor color);
	void onGradientOffsetChanged(float offset);

private:
    Database *db;
    QSharedPointer<iris::Scene> scene;
    iris::SkyType currentSky;
	QString skyGuid;

	void updateAssetAndKeys();

	QJsonObject skyProperties;

    ComboBoxWidget *skySelector;

    ColorValueWidget *singleColor;

    ComboBoxWidget *cubeSelector;

    TexturePickerWidget *equiTexture;

    ColorValueWidget *colorTop;
    ColorValueWidget *colorMid;
    ColorValueWidget *colorBot;
    HFloatSliderWidget *offset;

    ComboBoxWidget *shaderSelector;

    HFloatSliderWidget *luminance;
    HFloatSliderWidget *reileigh;
    HFloatSliderWidget *mieCoefficient;
    HFloatSliderWidget *mieDirectionalG;
    HFloatSliderWidget *turbidity;
    HFloatSliderWidget *sunPosX;
    HFloatSliderWidget *sunPosY;
    HFloatSliderWidget *sunPosZ;

	QJsonObject singleColorDefinition;
	QJsonObject cubeMapDefinition;
	QJsonObject equiSkyDefinition;
	QJsonObject gradientDefinition;
	QJsonObject materialDefinition;
	QJsonObject realisticDefinition;

	class CubeMapWidget *skyMapWidget;
};

#endif // SKYPROPERTYWIDGET_H
