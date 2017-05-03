/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materialpropertywidget.h"

#include "../accordianbladewidget.h"
#include "../hfloatsliderwidget.h"
#include "../comboboxwidget.h"
#include "../colorpickerwidget.h"
#include "../colorvaluewidget.h"
#include "../checkboxwidget.h"
#include "../texturepickerwidget.h"
#include "../labelwidget.h"
#include "../propertywidget.h"

#include "../../irisgl/src/graphics/texture2d.h"
#include "../../irisgl/src/scenegraph/meshnode.h"
#include "../../irisgl/src/materials/custommaterial.h"
#include "../../irisgl/src/materials/propertytype.h"

#include "../../io/materialreader.hpp"

void MaterialPropertyWidget::setupCustomMaterial()
{
    // textures
//    QSignalMapper *textureMapper = new QSignalMapper(this);
//    auto texIter = textureUniforms.begin();
//    while (texIter != textureUniforms.end()) {
//        connect(texIter->value, SIGNAL(valueChanged(QString)), textureMapper, SLOT(map()));
//        textureMapper->setMapping(texIter->value, texIter->value);
//        ++texIter;
//    }
//    connect(textureMapper, SIGNAL(mapped(QWidget*)), SLOT(onCustomTextureChanged(QWidget*)));

//    // TExTURES
//    texIter = textureUniforms.begin();
//    auto tIter = customMaterial->textureUniforms.begin();
//    while (texIter != textureUniforms.end()) {
//        if (!tIter->value.isEmpty()) {
//            texIter->value->setTexture(tIter->value);
//        } else {
//            texIter->value->setTexture("");
//        }
//        ++texIter;
//        ++tIter;
//    }
}

void MaterialPropertyWidget::forceShaderRefresh(const QString &matName)
{
    emit onMaterialSelectorChanged(matName);
}

void MaterialPropertyWidget::setupShaderSelector()
{
    materialSelector = this->addComboBox("Shader");

    QDir dir(IrisUtils::getAbsoluteAssetPath("app/shader_defs/"));
    for (auto shaderName : dir.entryList(QDir::Files)) {
        materialSelector->addItem(QFileInfo(shaderName).baseName());
    }

    materialSelector->setCurrentItem(this->customMaterial->getMaterialName());

    connect(materialSelector,   SIGNAL(currentIndexChanged(QString)),
            this,               SLOT(onMaterialSelectorChanged(QString)));
}

MaterialPropertyWidget::MaterialPropertyWidget()
{

}

void MaterialPropertyWidget::handleMat()
{
    materialPropWidget = this->addPropertyWidget();
    materialPropWidget->setListener(this);
    materialPropWidget->setProperties(this->customMaterial->properties);
}

void MaterialPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        this->meshNode = sceneNode.staticCast<iris::MeshNode>();
        this->customMaterial = meshNode->getMaterial().staticCast<iris::CustomMaterial>();
    }

    setupShaderSelector();

    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        // TODO - properly update only when requested

        materialReader = new MaterialReader();
        materialReader->readJahShader(
                    IrisUtils::getAbsoluteAssetPath(
                        "app/shader_defs/" + this->customMaterial->getMaterialName() + ".json"));
        this->customMaterial->generate(materialReader->getParsedShader());

        handleMat();
//        setupCustomMaterial();
    } else {
        this->meshNode.clear();
        this->customMaterial.clear();
        return;
    }
}

void MaterialPropertyWidget::onMaterialSelectorChanged(const QString &text)
{
    // only clear when we are switching mats, not on every select
    this->customMaterial->purge();

    this->clearPanel(this->layout());

    this->customMaterial->setMaterialName(text);
    this->setSceneNode(this->meshNode);

//    int finalHeight = this->customMaterial->getCalculatedPropHeight();

//    this->setMinimumHeight(0);
//    this->setMaximumHeight(50);
}

void MaterialPropertyWidget::onPropertyChanged(iris::Property *property)
{
    for (auto prop : customMaterial->properties) {
        if (prop->name == property->name) {
            prop->setValue(property->getValue());
        }

        // special case for textures since we have to generate a new one
        if (property->type == iris::PropertyType::Texture) {
            customMaterial->updateTextureAndToggleUniform(property->uniform, property->getValue().toString());
        }
    }
}
