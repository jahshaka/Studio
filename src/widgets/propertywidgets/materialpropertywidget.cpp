/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materialpropertywidget.h"

#include <QJsonObject>

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
#include "../../irisgl/src/core/property.h"

#include "../../uimanager.h"
#include "../../commands/changematerialpropertycommand.h"

void MaterialPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        meshNode = sceneNode.staticCast<iris::MeshNode>();
        material = meshNode->getMaterial().staticCast<iris::CustomMaterial>();
    }

    setupShaderSelector();

    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        // TODO - properly update only when requested, and cache these?
        QFileInfo shaderFile;

        QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
        while (it.hasNext()) {
            it.next();
            if (it.key() == material->getGuid()) {
                shaderFile = QFileInfo(IrisUtils::getAbsoluteAssetPath(it.value()));
                break;
            }
        }

        if (shaderFile.exists()) {
            material->generate(shaderFile.absoluteFilePath());
        } else {
            for (auto asset : AssetManager::getAssets()) {
                if (asset->type == ModelTypes::Shader) {
                    if (asset->assetGuid == material->getGuid()) {
                        auto def = asset->getValue().toJsonObject();
                        auto vertexShader = def["vertex_shader"].toString();
                        auto fragmentShader = def["fragment_shader"].toString();
                        for (auto asset : AssetManager::getAssets()) {
                            if (asset->type == ModelTypes::File) {
                                if (vertexShader == asset->assetGuid) vertexShader = asset->path;
                                if (fragmentShader == asset->assetGuid) fragmentShader = asset->path;
                            }
                        }
                        def["vertex_shader"] = vertexShader;
                        def["fragment_shader"] = fragmentShader;
                        material->generate(def);
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
    Q_UNUSED(text)
}

void MaterialPropertyWidget::materialChanged(int index)
{
    Q_UNUSED(index);
    material->purge();
    clearPanel(this->layout());
    material->setName(materialSelector->getCurrentItem());
    material->setGuid(materialSelector->getCurrentItemData());
    setSceneNode(meshNode);
}

void MaterialPropertyWidget::setupShaderSelector()
{
    materialSelector = this->addComboBox("Shader");

    QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
    while (it.hasNext()) {
        it.next();
        materialSelector->addItem(QFileInfo(it.value()).baseName(), it.key());
    }

    for (auto asset : AssetManager::getAssets()) {
        if (asset->type == ModelTypes::Shader) {
            materialSelector->addItem(asset->fileName, asset->assetGuid);
        }
    }

    materialSelector->setCurrentItemData(material->getGuid());

    connect(materialSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(materialChanged(int)));
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

void MaterialPropertyWidget::onPropertyChangeStart(iris::Property* prop)
{
    startValue = prop->getValue();
}

void MaterialPropertyWidget::onPropertyChangeEnd(iris::Property* prop)
{
    UiManager::pushUndoStack(new ChangeMaterialPropertyCommand(material, prop->name, startValue, prop->getValue()));
}
