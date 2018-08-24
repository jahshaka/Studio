/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIGHTPROPERTYWIDGET_H
#define LIGHTPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"
#include "../../irisgl/src/scenegraph/lightnode.h"

class ColorValueWidget;
class ColorPickerWidget;
namespace iris
{
    class SceneNode;
    class LightNode;
}


/**
 * This class displays properties of light nodes
 */
class LightPropertyWidget:public AccordianBladeWidget
{
    Q_OBJECT

public:
    LightPropertyWidget(QWidget* parent=nullptr);

    /**
     * Sets the active sceneNode. If the sceneNode is a LightNode it is casted
     * to a LightNode and stored in lightNode, otherwise lightNode is reset to null
     * @param sceneNode
     */
    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);

protected slots:

    /**
     * Sets the light's color
     * @param color
     */
    void lightColorChanged(QColor color);

    /**
     * Sets the light's intensity
     * @param intensity
     */
    void lightIntensityChanged(float intensity);

    /**
     * Sets the light's distance
     * @param distance
     */
    void lightDistanceChanged(float distance);

    /**
     * Sets the light's spot cutoff angle. Only valid for spotlights.
     * @param spotCutOff
     */
    void lightSpotCutoffChanged(float spotCutOff);

    void lightSpotCutoffSoftnessChanged(float spotCutOffSoftness);

    void shadowTypeChanged(QString name);
    void shadowSizeChanged(QString size);
	void shadowBiasChanged(float bias);

	void shadowColorChanged(QColor color);
	void shadowAlphaChanged(float bias);

private:
    QString evalShadowTypeName(iris::ShadowMapType shadowType);
    iris::ShadowMapType evalShadowMapType(QString shadowType);

private:

    QSharedPointer<iris::LightNode> lightNode;

	ColorValueWidget* lightColor;
	HFloatSliderWidget* distance;
    HFloatSliderWidget* spotCutOff;
    HFloatSliderWidget* spotCutOffSoftness;
    HFloatSliderWidget* intensity;
    //EnumPicker* lightTypePicker;

	ColorValueWidget* shadowColor;
	HFloatSliderWidget* shadowAlpha;

    ComboBoxWidget* shadowType;
    ComboBoxWidget* shadowSize;
    HFloatSliderWidget* shadowBias;
};

#endif // LIGHTPROPERTYWIDGET_H
