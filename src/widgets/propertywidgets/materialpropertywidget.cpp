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

    normalTexture = this->addTexturePicker("Normal Texture");
    normalIntensity = this->addFloatValueSlider("Normal Intensity",-1,1);

    reflectionTexture = this->addTexturePicker("Reflection Texture");
    reflectionInfluence = this->addFloatValueSlider("Reflection Influence",0,1);
    //this->addTexturePicker("Diffuse Texture");

    //this->setMaximumHeight(300);

    // materialPropView->setMaxHeight(materialPropView->minimum_height);


    connect(ambientColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(onAmbientColorChanged(QColor)));

    connect(diffuseColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(onDiffuseColorChanged(QColor)));
    connect(diffuseTexture,SIGNAL(valueChanged(QString)),this,SLOT(onDiffuseTextureChanged(QString)));

    connect(specularColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(onSpecularColorChanged(QColor)));
    connect(specularTexture,SIGNAL(valueChanged(QString)),this,SLOT(onSpecularTextureChanged(QString)));
    connect(shininess,SIGNAL(valueChanged(float)),this,SLOT(onShininessChanged(float)));

    connect(normalTexture,SIGNAL(valueChanged(QString)),this,SLOT(onNormalTextureChanged(QString)));
    connect(normalIntensity,SIGNAL(valueChanged(float)),this,SLOT(onNormalIntensityChanged(float)));

    connect(reflectionTexture,SIGNAL(valueChanged(QString)),this,SLOT(onReflectionTextureChanged(QString)));
    connect(reflectionInfluence,SIGNAL(valueChanged(float)),this,SLOT(onReflectionInfluenceChanged(float)));

}


void MaterialPropertyWidget::setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode)
{
    if(!!sceneNode && sceneNode->getSceneNodeType()==jah3d::SceneNodeType::Mesh)
    {
        this->meshNode = sceneNode.staticCast<jah3d::MeshNode>();
        this->material = meshNode->getMaterial().staticCast<jah3d::DefaultMaterial>();
    }
    else
    {
        this->meshNode.clear();
        this->material.clear();
        return;
    }

    auto mat = this->material;
    //todo: ensure material isnt null

    ambientColor->setColorValue(mat->getAmbientColor());

    diffuseColor->setColorValue(mat->getDiffuseColor());
    diffuseTexture->setTexture(mat->getDiffuseTextureSource());

    specularColor->setColorValue(mat->getSpecularColor());
    specularTexture->setTexture(mat->getSpecularTextureSource());
    shininess->setValue(mat->getShininess());

    normalTexture->setTexture(mat->getNormalTextureSource());
    normalIntensity->setValue(mat->getNormalIntensity());

    reflectionTexture->setTexture(mat->getReflectionTextureSource());
    reflectionInfluence->setValue(mat->getReflectionInfluence());

}

void MaterialPropertyWidget::onAmbientColorChanged(QColor color)
{
    if(!!material)
    {
        material->setAmbientColor(color);
    }
}

void MaterialPropertyWidget::onDiffuseColorChanged(QColor color)
{
    if(!!material)
    {
        material->setDiffuseColor(color);
    }

}

void MaterialPropertyWidget::onDiffuseTextureChanged(QString texture)
{
    if(!!material)
    {
        material->setDiffuseTexture(jah3d::Texture2D::load(texture));
    }
}

void MaterialPropertyWidget::onSpecularColorChanged(QColor color)
{
    if(!!material)
    {
        material->setSpecularColor(color);
    }
}

void MaterialPropertyWidget::onSpecularTextureChanged(QString texture)
{
    if(!!material)
    {
        material->setSpecularTexture(jah3d::Texture2D::load(texture));
    }
}

void MaterialPropertyWidget::onShininessChanged(float shininess)
{
    if(!!material)
    {
        material->setShininess(shininess);
    }
}

void MaterialPropertyWidget::onNormalTextureChanged(QString texture)
{
    if(!!material)
    {
        material->setNormalTexture(jah3d::Texture2D::load(texture));
    }
}

void MaterialPropertyWidget::onNormalIntensityChanged(float intensity)
{
    if(!!material)
    {
        material->setNormalIntensity(intensity);
    }
}

void MaterialPropertyWidget::onReflectionTextureChanged(QString texture)
{
    if(!!material)
    {
        material->setReflectionTexture(jah3d::Texture2D::load(texture));
    }
}

void MaterialPropertyWidget::onReflectionInfluenceChanged(float influence)
{
    if(!!material)
    {
        material->setReflectionInfluence(influence);
    }
}
