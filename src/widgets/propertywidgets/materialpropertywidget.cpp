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
#include "../comboboxwidget.h"
#include "../colorpickerwidget.h"
#include "../colorvaluewidget.h"
#include "../texturepickerwidget.h"
#include "../labelwidget.h"

#include "../../irisgl/src/graphics/texture2d.h"
#include "../../irisgl/src/scenegraph/meshnode.h"
#include "../../irisgl/src/materials/defaultmaterial.h"
#include "../../irisgl/src/materials/custommaterial.h"

#include "../../io/materialreader.hpp"

void MaterialPropertyWidget::parseJahShader(const QJsonObject &jahShader)
{
    // @TODO: add some check here to escape early

    auto shaderName = jahShader["name"].toString();
    auto uniforms = jahShader["uniforms"].toObject();

    allocated = 0;

    for (auto childObj : uniforms) {
        if (childObj.toObject()["type"] == "slider") {
            auto text   = childObj.toObject()["name"].toString();
            auto start  = (float) childObj.toObject()["start"].toDouble();
            auto end    = (float) childObj.toObject()["end"].toDouble();
            auto value  = (float) childObj.toObject()["value"].toDouble();
            customSliders[allocated] = this->addFloatValueSlider(text, start, end);

            customSliders[allocated]->index = allocated;
            allocated++;
        }
    }
}

void MaterialPropertyWidget::setupDefaultMaterial()
{
    ambientColor        = this->addColorPicker("Ambient Color");
    diffuseColor        = this->addColorPicker("Diffuse Color");
    diffuseTexture      = this->addTexturePicker("Diffuse Texture");
    specularColor       = this->addColorPicker("Specular Color");
    specularTexture     = this->addTexturePicker("Specular Texture");
    shininess           = this->addFloatValueSlider("Shininess", 0, 100.f);
    normalTexture       = this->addTexturePicker("Normal Texture");
    normalIntensity     = this->addFloatValueSlider("Normal Intensity", -1.f, 1.f);
    reflectionTexture   = this->addTexturePicker("Reflection Texture");
    reflectionInfluence = this->addFloatValueSlider("Reflection Influence", 0, 1.f);
    textureScale        = this->addFloatValueSlider("Texture Scale", 0, 10.f);

    connect(ambientColor->getPicker(),  SIGNAL(onColorChanged(QColor)),
            this,                       SLOT(onAmbientColorChanged(QColor)));

    connect(diffuseColor->getPicker(),  SIGNAL(onColorChanged(QColor)),
            this,                       SLOT(onDiffuseColorChanged(QColor)));

    connect(diffuseTexture,             SIGNAL(valueChanged(QString)),
            this,                       SLOT(onDiffuseTextureChanged(QString)));

    connect(specularColor->getPicker(), SIGNAL(onColorChanged(QColor)),
            this,                       SLOT(onSpecularColorChanged(QColor)));

    connect(specularTexture,            SIGNAL(valueChanged(QString)),
            this,                       SLOT(onSpecularTextureChanged(QString)));

    connect(shininess,                  SIGNAL(valueChanged(float)),
            this,                       SLOT(onShininessChanged(float)));

    connect(normalTexture,              SIGNAL(valueChanged(QString)),
            this,                       SLOT(onNormalTextureChanged(QString)));

    connect(normalIntensity,            SIGNAL(valueChanged(float)),
            this,                       SLOT(onNormalIntensityChanged(float)));

    connect(reflectionTexture,          SIGNAL(valueChanged(QString)),
            this,                       SLOT(onReflectionTextureChanged(QString)));

    connect(reflectionInfluence,        SIGNAL(valueChanged(float)),
            this,                       SLOT(onReflectionInfluenceChanged(float)));

    connect(textureScale,               SIGNAL(valueChanged(float)),
            this,                       SLOT(onTextureScaleChanged(float)));
}

void MaterialPropertyWidget::setupCustomMaterial()
{
    materialReader->readJahShader(IrisUtils::getAbsoluteAssetPath("app/Glass.json"));
    parseJahShader(materialReader->getParsedShader());

    QSignalMapper *signalMapper = new QSignalMapper (this);
    for (int i = 0; i < allocated; i++) {
        // @TODO, somewhat fine as it is built in but there is
        // surely a cleaner way to go about this...
        connect(customSliders[i],       SIGNAL(valueChanged(float)), signalMapper, SLOT(map()));
        signalMapper->setMapping(customSliders[i], customSliders[i]);
    }

    connect(signalMapper, SIGNAL(mapped(QWidget*)), SLOT(onCustomSliderChanged(QWidget *)));
}

void MaterialPropertyWidget::setupShaderSelector()
{
    // load these from a directory in the future...
    materialSelector = this->addComboBox("Shader");
    materialSelector->addItem("Default Shader");
    materialSelector->addItem("Environment Surface Shader");

    if (meshNode->getMaterialType() == 1) {
        materialSelector->setCurrentItem("Default Shader");
    } else {
        materialSelector->setCurrentItem("Environment Surface Shader");
    }

    connect(materialSelector,   SIGNAL(currentIndexChanged(QString)),
            this,               SLOT(onMaterialSelectorChanged(QString)));
}

MaterialPropertyWidget::MaterialPropertyWidget(QSharedPointer<iris::SceneNode> sceneNode, QWidget *parent)
{
    materialReader = new MaterialReader();

    if (!!sceneNode) {
        this->meshNode = sceneNode.staticCast<iris::MeshNode>();
    }
}

void MaterialPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        this->meshNode = sceneNode.staticCast<iris::MeshNode>();
    }

    setupShaderSelector();

    if (this->meshNode->getMaterialType() == 1) {
        if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            this->meshNode = sceneNode.staticCast<iris::MeshNode>();
            this->material = meshNode->getMaterial().staticCast<iris::DefaultMaterial>();

            setupDefaultMaterial();

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
        } else {
            this->meshNode.clear();
            this->material.clear();
            return;
        }
    } else if (this->meshNode->getMaterialType() == 2) {

        if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            this->meshNode = sceneNode.staticCast<iris::MeshNode>();
            this->customMaterial = meshNode->getCustomMaterial().staticCast<iris::CustomMaterial>();

            setupCustomMaterial();

            for (int i = 0; i < allocated; i++) {
                customSliders[i]->setValue(customMaterial->sliderValues[i]);
            }

            this->customMaterial->initializeDefaultValues(materialReader->getParsedShader());
        } else {
            this->meshNode.clear();
            this->customMaterial.clear();
            return;
        }
    }
}

void MaterialPropertyWidget::onCustomSliderChanged(QWidget *t)
{
    auto changedIndex = dynamic_cast<HFloatSliderWidget*>(t);
    if (!!customMaterial) {
        customMaterial->sliderValues[changedIndex->index] = changedIndex->getValue();
    }
}

void MaterialPropertyWidget::onMaterialSelectorChanged(const QString &text)
{
    if (!!this->meshNode) {
        if (text == "Default Shader") {
            this->meshNode->setMaterialType(1);
            this->meshNode->setActiveMaterial(1);

        } else {
            this->meshNode->setMaterialType(2);
            this->meshNode->setActiveMaterial(2);
        }
    }

    if (text == "Default Shader") {
        this->clearPanel(this->layout());

        resetHeight();

        int finalHeight = 30 + (8 * 28) /* narrows */ + (4 * 108) /* pickers */ + (12 * 6) + 9 + 9;

        setHeight(finalHeight - 30);

        this->setMinimumHeight(finalHeight);
        this->setMaximumHeight(finalHeight);

        this->setSceneNode(this->meshNode);

    } else if (text == "Environment Surface Shader") {

        this->clearPanel(this->layout());

        resetHeight();

        // this isn't magic, there are values but it's not possible to get them yet
        // finalHeight = minimum_height + (widgets * height) +
        //              (widgetCount * heights) + topMargin + bottomMargin;
        int finalHeight = 30 + (3 * 28) + (3 * 6) + 9 + 9;

        setHeight(finalHeight - 30);

        this->setMinimumHeight(finalHeight);
        this->setMaximumHeight(finalHeight);

        this->setSceneNode(this->meshNode);
    }
}

void MaterialPropertyWidget::onAmbientColorChanged(QColor color)
{
    if (!!material) {
        material->setAmbientColor(color);
    }
}

void MaterialPropertyWidget::onDiffuseColorChanged(QColor color)
{
    if (!!material) {
        material->setDiffuseColor(color);
    }
}

void MaterialPropertyWidget::onDiffuseTextureChanged(QString texture)
{
    if (!!material) {
        if (texture.isEmpty() || texture.isNull()) {
            material->setDiffuseTexture(iris::Texture2D::null());
        } else {
            material->setDiffuseTexture(iris::Texture2D::load(texture));
        }
    }
}

void MaterialPropertyWidget::onSpecularColorChanged(QColor color)
{
    if (!!material) {
        material->setSpecularColor(color);
    }
}

void MaterialPropertyWidget::onSpecularTextureChanged(QString texture)
{
    if (!!material) {
        material->setSpecularTexture(iris::Texture2D::load(texture));
    }
}

void MaterialPropertyWidget::onShininessChanged(float shininess)
{
    if (!!material) {
        material->setShininess(shininess);
    }
}

void MaterialPropertyWidget::onNormalTextureChanged(QString texture)
{
    if (!!material) {
        material->setNormalTexture(iris::Texture2D::load(texture));
    }
}

void MaterialPropertyWidget::onNormalIntensityChanged(float intensity)
{
    if (!!material) {
        material->setNormalIntensity(intensity);
    }
}

// @TODO -rework or remove
void MaterialPropertyWidget::onReflectionTextureChanged(QString texture)
{
    if (!!material) {
        material->setReflectionTexture(iris::Texture2D::load(texture));
    }
}

// @TODO -rework or remove
void MaterialPropertyWidget::onReflectionInfluenceChanged(float influence)
{
    if (!!material) {
        material->setReflectionInfluence(influence);
    }
}

void MaterialPropertyWidget::onTextureScaleChanged(float scale)
{
    if (!!material) {
        material->setTextureScale(scale);
    }
}
