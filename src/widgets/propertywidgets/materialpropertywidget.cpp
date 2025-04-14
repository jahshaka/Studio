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
#include <QDirIterator>

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

#include "io/scenewriter.h"

#include "globals.h"
#include "core/database/database.h"
#include "io/materialreader.hpp"

void MaterialPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        meshNode = sceneNode.staticCast<iris::MeshNode>();
        material = meshNode->getMaterial().staticCast<iris::CustomMaterial>();
        meshNodeGuid = meshNode->getGUID();

        for (auto prop : material->properties) {
            if (prop->type == iris::PropertyType::Texture) {
                existingTextures.insert(prop->name, prop->getValue().toString());
            }
        }
    }

    setupShaderSelector();

    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        /*
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
		*/
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

	MaterialReader reader;
	material = reader.createMaterialFromShaderGuid(materialSelector->getCurrentItemData(), db);
    material->setName(materialSelector->getCurrentItem());
    material->setGuid(materialSelector->getCurrentItemData());
	meshNode->setMaterial(material);
	setupShaderSelector();
	
    //setSceneNode(meshNode);


    QJsonObject node;
    SceneWriter::writeSceneNode(node, meshNode, false);

    db->updateAssetAsset(meshNode->getGUID(), QJsonDocument(node).toJson());
    db->removeDependenciesByType(meshNode->getGUID(), ModelTypes::Shader);

    bool usesDefaultShader = false;
    QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
    while (it.hasNext()) {
        it.next();
        if (it.key() == materialSelector->getCurrentItemData()) {
            usesDefaultShader = true;
            break;
        }
    }

    // Don't create dependencies to builtin shaders
    if (!usesDefaultShader) {
        db->createDependency(
            static_cast<int>(ModelTypes::Object),
            static_cast<int>(ModelTypes::Shader),
            meshNodeGuid, materialSelector->getCurrentItemData(),
            Globals::project->getProjectGuid()
        );
    }

	
	for (auto prop : material->properties) {
		if (prop->type == (iris::PropertyType::Texture)) {
			auto guid = prop->getValue().toString();
			auto asset = db->fetchAsset(guid).name;
			auto path = QDir(Globals::project->getProjectFolder()).filePath(asset);
			if(QFile::exists(path))
				material->setValue(prop->name, path);
		}
	}

	setWidgetProperties();
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
            materialSelector->addItem(QFileInfo(asset->fileName).baseName(), asset->assetGuid);
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
        
        // HANDLE CASE where the widget isn't deselected

        QString assetGuid = db->fetchAssetGUIDByName(QFileInfo(prop->getValue().toString()).fileName());
        if (assetGuid.isEmpty()) {
            db->deleteDependency(
                meshNodeGuid,
                db->fetchAssetGUIDByName(QFileInfo(existingTextures.value(prop->name)).fileName())
            );
        }
        else {
            db->createDependency(
                static_cast<int>(ModelTypes::Object),
                static_cast<int>(ModelTypes::Texture),
                meshNodeGuid, assetGuid,
                Globals::project->getProjectGuid()
            );
        }
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
