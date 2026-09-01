/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDSKYPROPERTYWIDGET_H
#define WORLDSKYPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "ui/controls/accordionbladewidget.h"

#include "irisgl/document/scenegraph/scene.h"

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

class IEditorViewport;

class WorldSkyPropertyWidget: public AccordianBladeWidget
{
    Q_OBJECT

public:
	void wireViewportEvents(IEditorViewport *viewport);
    WorldSkyPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);
    void setDatabase(Database *);

public slots:
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
    void onSunAzimuthChanged(float val);
    void onSunElevationChanged(float val);
    void onSkyDetailChanged(int row);
    void onAmbientFromSkyChanged(bool on);

	void onGradientTopColorChanged(QColor color);
	void onGradientMidColorChanged(QColor color);
	void onGradientBotColorChanged(QColor color);
	void onGradientOffsetChanged(float offset);

private:
    Database *db;
    QSharedPointer<iris::Scene> scene;

	void updateAssetAndKeys();
	/// Pushes the two angle sliders into the document's sun vector and the
	/// serialized blob (they are one and the same three floats).
	void writeSunAngles();
	/// Adds the "Ambient From Sky" row for sky types that have something to
	/// integrate; single-colour skies always use the flat Ambient Color.
	void addAmbientFromSkyRow();


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
    // The sun is a polar control (VISUAL_PARITY_SPEC item 1): the document
    // still stores sunPosX/Y/Z, these two are the readable view of them.
    HFloatSliderWidget *sunAzimuth = nullptr;
    HFloatSliderWidget *sunElevation = nullptr;
    ComboBoxWidget *skyDetail = nullptr;          // realistic-sky bake width
    CheckBoxWidget *ambientFromSky = nullptr;     // sky-driven ambient (item 3b)

	QJsonObject singleColorDefinition;
	QJsonObject cubeMapDefinition;
	QJsonObject equiSkyDefinition;
	QJsonObject gradientDefinition;
	QJsonObject materialDefinition;
	QJsonObject realisticDefinition;

	class CubeMapWidget *cubeMapWidget;
};

#endif // WORLDSKYPROPERTYWIDGET_H
