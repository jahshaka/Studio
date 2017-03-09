/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "../accordianbladewidget.h"

#include "materialpropertywidget.h"
#include "../hfloatsliderwidget.h"
#include "../colorpickerwidget.h"
#include "../colorvaluewidget.h"
#include "../texturepickerwidget.h"
#include "../labelwidget.h"

#include "../../irisgl/src/graphics/texture2d.h"
#include "../../irisgl/src/scenegraph/meshnode.h"
#include "../../irisgl/src/materials/defaultmaterial.h"


MaterialPropertyWidget::MaterialPropertyWidget(int materialType, QWidget* parent)
{
    this->materialType = materialType;

    if (materialType == 1) {
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

        textureScale = this->addFloatValueSlider("Texture Scale",0,10);

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

        connect(textureScale,SIGNAL(valueChanged(float)),this,SLOT(onTextureScaleChanged(float)));
    } else {
        fuNick = this->addLabel("Smh", "Fuck you");
    }

}


void MaterialPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (materialType == 1) {
        if(!!sceneNode && sceneNode->getSceneNodeType()==iris::SceneNodeType::Mesh)
        {
            this->meshNode = sceneNode.staticCast<iris::MeshNode>();
            this->material = meshNode->getMaterial().staticCast<iris::DefaultMaterial>();
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

        textureScale->setValue(mat->getTextureScale());
    }

}

void MaterialPropertyWidget::onAmbientColorChanged(QColor color)
{
    if (materialType == 1) {
        if(!!material)
        {
            material->setAmbientColor(color);
        }
    }
}

void MaterialPropertyWidget::onDiffuseColorChanged(QColor color)
{
    if (materialType == 1) {
        if(!!material)
        {
            material->setDiffuseColor(color);
        }
    }
}

void MaterialPropertyWidget::onDiffuseTextureChanged(QString texture)
{
    if (materialType == 1) {
        if(!!material)
        {
            if(texture.isEmpty() || texture.isNull())
            {
                material->setDiffuseTexture(iris::Texture2D::null());
            }
            else
            {
                material->setDiffuseTexture(iris::Texture2D::load(texture));
            }
        }
    }
}

void MaterialPropertyWidget::onSpecularColorChanged(QColor color)
{
    if (materialType == 1) {
    if(!!material)
    {
        material->setSpecularColor(color);
    }}
}

void MaterialPropertyWidget::onSpecularTextureChanged(QString texture)
{
    if (materialType == 1) {
    if(!!material)
    {
        material->setSpecularTexture(iris::Texture2D::load(texture));
    }}
}

void MaterialPropertyWidget::onShininessChanged(float shininess)
{
    if (materialType == 1) {
    if(!!material)
    {
        material->setShininess(shininess);
    }}
}

void MaterialPropertyWidget::onNormalTextureChanged(QString texture)
{
    if (materialType == 1) {
    if(!!material)
    {
        material->setNormalTexture(iris::Texture2D::load(texture));
    }}
}

void MaterialPropertyWidget::onNormalIntensityChanged(float intensity)
{
    if (materialType == 1) {
    if(!!material)
    {
        material->setNormalIntensity(intensity);
    }}
}

void MaterialPropertyWidget::onReflectionTextureChanged(QString texture)
{
    if (materialType == 1) {
        if(!!material)
        {
            material->setReflectionTexture(iris::Texture2D::load(texture));
        }
    }
}

void MaterialPropertyWidget::onReflectionInfluenceChanged(float influence)
{
    if (materialType == 1) {
        if(!!material)
        {
            material->setReflectionInfluence(influence);
        }
    }
}

void MaterialPropertyWidget::onTextureScaleChanged(float scale)
{
    if (materialType == 1) {
        if(!!material)
        {
            material->setTextureScale(scale);
        }
    }
}
