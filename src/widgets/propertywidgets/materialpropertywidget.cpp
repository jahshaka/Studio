#include "../accordianbladewidget.h"

#include "materialpropertywidget.h"
#include "../hfloatslider.h"
#include "../colorpickerwidget.h"


MaterialPropertyWidget::MaterialPropertyWidget(QWidget* parent)
{
    this->addColorPicker("Diffuse Color");
    this->addTexturePicker("Diffuse Texture");

    this->addColorPicker("Specular Color");
    this->addTexturePicker("Specular Texture");
    this->addFloatValueSlider("Shininess",0,100);

    this->addColorPicker("Ambient Color");

    this->addTexturePicker("Reflection Texture");
    this->addColorPicker("Reflection Influence");
    //this->addTexturePicker("Diffuse Texture");

    //this->setMaximumHeight(this->minimum_height);
}


void MaterialPropertyWidget::setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode)
{
    this->sceneNode = sceneNode;
}
