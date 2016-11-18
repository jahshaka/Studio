#include "../accordianbladewidget.h"

#include "materialpropertywidget.h"
#include "../hfloatslider.h"
#include "../colorpickerwidget.h"
#include "../colorvaluewidget.h"
#include <QDebug>

#include "../../jah3d/scenegraph/meshnode.h"
#include "../../jah3d/materials/defaultmaterial.h"


MaterialPropertyWidget::MaterialPropertyWidget(QWidget* parent)
{
    ambientColor = this->addColorPicker("Ambient Color");

    diffuseColor = this->addColorPicker("Diffuse Color");
    diffuseTexture = this->addTexturePicker("Diffuse Texture");

    specularColor =this->addColorPicker("Specular Color");
    specularTexture = this->addTexturePicker("Specular Texture");
    shininess = this->addFloatValueSlider("Shininess",0,100);

    reflectionTexture = this->addTexturePicker("Reflection Texture");
    reflectionInfluence = this->addFloatValueSlider("Reflection Influence",0,1);
    //this->addTexturePicker("Diffuse Texture");

    //this->setMaximumHeight(300);

    // materialPropView->setMaxHeight(materialPropView->minimum_height);


    connect(diffuseColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(onDiffuseColorChanged(QColor)));
    connect(specularColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(onSpecularColorChanged(QColor)));
}


void MaterialPropertyWidget::setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode)
{
    if(!!sceneNode && sceneNode->getSceneNodeType()==jah3d::SceneNodeType::Mesh)
        this->meshNode = sceneNode.staticCast<jah3d::MeshNode>();
    else
    {
        this->meshNode.clear();
        return;
    }

    auto mat = meshNode->getMaterial().staticCast<jah3d::DefaultMaterial>();
    //todo: ensure material isnt null

    ambientColor->setColorValue(mat->getAmbientColor());

    diffuseColor->setColorValue(mat->getDiffuseColor());
    //diffuseTexture->setTexture(mat->getDiffuseColor());

    specularColor->setColorValue(mat->getSpecularColor());


}

void MaterialPropertyWidget::onDiffuseColorChanged(QColor color)
{
    if(!!meshNode && !!meshNode->getMaterial())
    {
        auto mat = meshNode->getMaterial().staticCast<jah3d::DefaultMaterial>();
        mat->setDiffuseColor(color);
    }

}

void MaterialPropertyWidget::onSpecularColorChanged(QColor color)
{
    if(!!meshNode && !!meshNode->getMaterial())
    {
        auto mat = meshNode->getMaterial().staticCast<jah3d::DefaultMaterial>();
        mat->setSpecularColor(color);
    }

}
