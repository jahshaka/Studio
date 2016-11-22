#include "../accordianbladewidget.h"

#include "materialpropertywidget.h"
#include "../hfloatslider.h"
#include "../colorpickerwidget.h"
#include "../colorvaluewidget.h"
#include "../texturepicker.h"
#include <QDebug>

#include "../../jah3d/graphics/texture2d.h"
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


    connect(ambientColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(onAmbientColorChanged(QColor)));

    connect(diffuseColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(onDiffuseColorChanged(QColor)));
    connect(diffuseTexture,SIGNAL(valueChanged(QString)),this,SLOT(onDiffuseTextureChanged(QString)));

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
    diffuseTexture->setTexture(mat->getDiffuseTextureSource());

    specularColor->setColorValue(mat->getSpecularColor());


}

void MaterialPropertyWidget::onAmbientColorChanged(QColor color)
{
    if(!!meshNode && !!meshNode->getMaterial())
    {
        auto mat = meshNode->getMaterial().staticCast<jah3d::DefaultMaterial>();
        mat->setAmbientColor(color);
    }
}

void MaterialPropertyWidget::onDiffuseColorChanged(QColor color)
{
    if(!!meshNode && !!meshNode->getMaterial())
    {
        auto mat = meshNode->getMaterial().staticCast<jah3d::DefaultMaterial>();
        mat->setDiffuseColor(color);
    }

}

void MaterialPropertyWidget::onDiffuseTextureChanged(QString texture)
{
    if(!!meshNode && !!meshNode->getMaterial())
    {
        auto mat = meshNode->getMaterial().staticCast<jah3d::DefaultMaterial>();
        mat->setDiffuseTexture(jah3d::Texture2D::load(texture));
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

void MaterialPropertyWidget::onSpecularTextureChanged(QString texture)
{
    if(!!meshNode && !!meshNode->getMaterial())
    {
        auto mat = meshNode->getMaterial().staticCast<jah3d::DefaultMaterial>();
        mat->setSpecularTexture(jah3d::Texture2D::load(texture));
    }
}

void MaterialPropertyWidget::onShininessChanged(float shininess)
{

}

void MaterialPropertyWidget::onNormalTextureChanged(QString texture)
{

}

void MaterialPropertyWidget::onNormalIntensityChanged(float intensity)
{

}

void MaterialPropertyWidget::onReflectionTextureChanged(QString texture)
{

}

void MaterialPropertyWidget::onReflectionInfluenceChanged(float intensity)
{

}
