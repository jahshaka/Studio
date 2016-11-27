#include "../accordianbladewidget.h"
#include <QWidget>

#include "lightpropertywidget.h"
#include "../hfloatslider.h"
#include "../colorpickerwidget.h"
#include "../colorvaluewidget.h"

#include "../../jah3d/core/scene.h"
#include "../../jah3d/core/scenenode.h"
#include "../../jah3d/scenegraph/lightnode.h"


LightPropertyWidget::LightPropertyWidget(QWidget* parent):
    AccordianBladeWidget(parent)
{
    lightColor = this->addColorPicker("Color");
    intensity = this->addFloatValueSlider("Intensity",0,10);
    radius = this->addFloatValueSlider("Radius",0,100);
    spotCutOff = this->addFloatValueSlider("Spotlight CutOff",0,90);


    connect(lightColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(lightColorChanged(QColor)));
    connect(lightColor->getPicker(),SIGNAL(onSetColor(QColor)),this,SLOT(lightColorChanged(QColor)));

    connect(intensity,SIGNAL(valueChanged(float)),this,SLOT(lightIntensityChanged(float)));
    connect(radius,SIGNAL(valueChanged(float)),this,SLOT(lightRadiusChanged(float)));
    connect(spotCutOff,SIGNAL(valueChanged(float)),this,SLOT(lightSpotCutoffChanged(float)));
}

void LightPropertyWidget::setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode)
{
    //this->sceneNode = sceneNode;
    if(!!sceneNode && sceneNode->getSceneNodeType()==jah3d::SceneNodeType::Light)
    {
        lightNode = sceneNode.staticCast<jah3d::LightNode>();

        //apply properties to ui
        lightColor->setColorValue(lightNode->color);
        intensity->setValue(lightNode->intensity);
        radius->setValue(lightNode->radius);
        spotCutOff->setValue(lightNode->spotCutOff);
    }
    else
    {
        lightNode.clear();
    }
}

void LightPropertyWidget::lightColorChanged(QColor color)
{
    if(!!lightNode)
        lightNode->color = color;
}

void LightPropertyWidget::lightIntensityChanged(float intensity)
{
    if(!!lightNode)
        lightNode->intensity = intensity;
}

void LightPropertyWidget::lightRadiusChanged(float radius)
{
    if(!!lightNode)
        lightNode->radius = radius;
}

void LightPropertyWidget::lightSpotCutoffChanged(float spotCutOff)
{
    if(!!lightNode)
        lightNode->spotCutOff = spotCutOff;
}



