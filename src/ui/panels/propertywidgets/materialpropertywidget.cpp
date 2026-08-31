/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/materialpropertywidget.h"
#include "data/project.h"

#include <QJsonObject>
#include <QDirIterator>

#include "ui/controls/accordionbladewidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/colorvaluewidget.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "ui/controls/labelwidget.h"
#include "ui/panels/propertywidget.h"

#include "data/constants.h"
#include "io/assetiobase.h"

#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/core/properties/property.h"

#include "services/services.h"
#include "services/undoservice.h"
#include "commands/changematerialpropertycommand.h"

#include "io/scenewriter.h"

#include "data/database/database.h"
#include "io/materialreader.h"

iris::MaterialPtr MaterialPropertyWidget::currentMaterial() const
{
    return !!material ? material.staticCast<iris::Material>() : genericMaterial;
}

void MaterialPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        meshNode = sceneNode.staticCast<iris::MeshNode>();

        // dynamicCast, not staticCast: a mesh may carry any Material subclass.
        // A staticCast here would reinterpret e.g. a PbrMaterial at
        // CustomMaterial's layout, which is undefined behaviour.
        material = meshNode->getMaterial().dynamicCast<iris::CustomMaterial>();
        genericMaterial.clear();

        if (!material) {
            // Not a shader-graph material. Render its parameters generically -
            // Material::properties is all these types have in common.
            genericMaterial = meshNode->getMaterial();
            meshNodeGuid    = meshNode->getGUID();
            for (auto prop : genericMaterial->properties) {
                if (prop->type == iris::PropertyType::Texture) {
                    existingTextures.insert(prop->name, prop->getValue().toString());
                }
            }
            setWidgetProperties();
            return;
        }

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
        genericMaterial.clear();
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

    auto mat = currentMaterial();
    if (!!mat)
        materialPropWidget->setProperties(mat->properties);
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
	reader.setProject(project);
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
            project->getProjectGuid()
        );
    }

	
	for (auto prop : material->properties) {
		if (prop->type == (iris::PropertyType::Texture)) {
			auto guid = prop->getValue().toString();
			auto asset = db->fetchAsset(guid).name;
			auto path = QDir(project->getProjectFolder()).filePath(asset);
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
    auto mat = currentMaterial();
    if (!mat) return;

    if (!material) {
        // Generic material (PbrMaterial, ...): Material::setValue is the ONLY
        // bridge onto the real fields the mirror reads (pbrmaterial.cpp:126 -
        // toPbrParams reads pbr->textureScale etc., not the Property list).
        // Writing the Property object alone changes what gets SAVED but not
        // what RENDERS - that was the dead material panel: edits appeared to
        // do nothing live and only showed up after a scene reload rebuilt the
        // material from JSON through setValue.
        mat->setValue(prop->name, prop->getValue());
        if (prop->type == iris::PropertyType::Texture)
            updateTextureDependency(prop);
        return;
    }

    for (auto property : material->properties) {
        if (property->name == prop->name) property->setValue(prop->getValue());
    }

    // special case for textures since we have to generate these
    if (prop->type == iris::PropertyType::Texture) {
        material->setTextureWithUniform(prop->uniform, prop->getValue().toString());
        updateTextureDependency(prop);
    }
}

// Keep the project database's object->texture dependency in step with a texture
// property edit (the packaged .jaf carries the texture because of this row).
// Works for both the shader-graph material and generic materials.
void MaterialPropertyWidget::updateTextureDependency(iris::Property *prop)
{
    if (!db || !project) return;

    // HANDLE CASE where the widget isn't deselected
    QString assetGuid = db->fetchAssetGUIDByName(QFileInfo(prop->getValue().toString()).fileName(), project->getProjectGuid());
    if (assetGuid.isEmpty()) {
        db->deleteDependency(
            meshNodeGuid,
            db->fetchAssetGUIDByName(QFileInfo(existingTextures.value(prop->name)).fileName(), project->getProjectGuid())
        );
    }
    else {
        db->createDependency(
            static_cast<int>(ModelTypes::Object),
            static_cast<int>(ModelTypes::Texture),
            meshNodeGuid, assetGuid,
            project->getProjectGuid()
        );
    }
}

void MaterialPropertyWidget::onPropertyChangeStart(iris::Property* prop)
{
    startValue = prop->getValue();
}

void MaterialPropertyWidget::onPropertyChangeEnd(iris::Property* prop)
{
    // A gesture that ended on its starting value (slider pressed and released
    // in place, colour dialog cancelled, Enter on an unchanged field) is not
    // an edit - don't pollute the undo stack with a no-op command.
    if (startValue == prop->getValue()) return;

    if (services && services->undo)
        // currentMaterial(), not the CustomMaterial member - that one is null
        // whenever the mesh carries a PbrMaterial and the command would crash.
        services->undo->push(new ChangeMaterialPropertyCommand(currentMaterial(), prop->name, startValue, prop->getValue()));
}
