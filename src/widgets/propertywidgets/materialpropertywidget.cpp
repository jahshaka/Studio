#include "../accordianbladewidget.h"

#include "materialpropertywidget.h"
#include "../hfloatslider.h"
#include "../colorpickerwidget.h"
#include <QDebug>

MaterialPropertyWidget::MaterialPropertyWidget(QWidget* parent)
{
    this->addColorPicker("Diffuse Color");
    this->addTexturePicker("Diffuse Texture");

    this->addColorPicker("Specular Color");
    this->addTexturePicker("Specular Texture");
    this->addFloatValueSlider("Shininess",0,1)->setValue(0.5);

    this->addColorPicker("Ambient Color");

    this->addTexturePicker("Reflection Texture");
    this->addColorPicker("Reflection Influence");
    //this->addTexturePicker("Diffuse Texture");

    //this->setMaximumHeight(300);

    // materialPropView->setMaxHeight(materialPropView->minimum_height);

}


void MaterialPropertyWidget::setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode)
{
    this->sceneNode = sceneNode;
}
