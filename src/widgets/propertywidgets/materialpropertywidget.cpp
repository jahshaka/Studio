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

#include "../../constants.h"
#include "../../io/assetiobase.h"

#include "../../irisgl/src/graphics/texture2d.h"
#include "../../irisgl/src/scenegraph/meshnode.h"
#include "../../irisgl/src/materials/custommaterial.h"
#include "../../irisgl/src/materials/propertytype.h"

void MaterialPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        meshNode = sceneNode.staticCast<iris::MeshNode>();
        material = meshNode->getMaterial().staticCast<iris::CustomMaterial>();
    }

    setupShaderSelector();

    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        // TODO - properly update only when requested
        auto shaderName = Constants::SHADER_DEFS + material->getName();
        auto shaderFile = QFileInfo(shaderName);
        if (shaderFile.exists()) {
            material->generate(IrisUtils::getAbsoluteAssetPath(shaderName));
        } else {
            for (auto asset : AssetManager::assets) {
                if (asset->type == AssetType::Shader) {
                    if (asset->fileName == material->getName() + ".shader") {
                        material->generate(asset->path, true);
                    }
                }
            }
        }
        setWidgetProperties();
    }

    else {
        meshNode.clear();
        material.clear();
        return;
    }
}

void MaterialPropertyWidget::forceShaderRefresh(const QString &materialName)
{
    emit materialChanged(materialName);
}

void MaterialPropertyWidget::setWidgetProperties()
{
    materialPropWidget = this->addPropertyWidget();
    materialPropWidget->setListener(this);
    materialPropWidget->setProperties(material->properties);
}

void MaterialPropertyWidget::materialChanged(const QString &text)
{
    material->purge();
    clearPanel(this->layout());
    material->setName(text);
    setSceneNode(meshNode);
}

void MaterialPropertyWidget::setupShaderSelector()
{
    materialSelector = this->addComboBox("Shader");

    QDir dir(IrisUtils::getAbsoluteAssetPath(Constants::SHADER_DEFS));
    for (auto shaderName : dir.entryList(QDir::Files)) {
        materialSelector->addItem(QFileInfo(shaderName).baseName());
    }

    for (auto asset : AssetManager::assets) {
        if (asset->type == AssetType::Shader) {
            materialSelector->addItem(QFileInfo(asset->fileName).baseName());
        }
    }

    materialSelector->setCurrentItem(material->getName());

    connect(materialSelector,   SIGNAL(currentIndexChanged(QString)),
            this,               SLOT(materialChanged(QString)));
}

void MaterialPropertyWidget::onPropertyChanged(iris::Property *prop)
{
    for (auto property : material->properties) {
        if (property->name == prop->name) property->setValue(prop->getValue());
    }

    // special case for textures since we have to generate these
    if (prop->type == iris::PropertyType::Texture) {
        material->setTextureWithUniform(prop->uniform, prop->getValue().toString());
    }
}
