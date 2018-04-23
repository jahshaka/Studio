/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "shaderpropertywidget.h"

#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>

#include "irisgl/src/graphics/shader.h"
#include "irisgl/src/scenegraph/meshnode.h"

#include "constants.h"
#include "globals.h"

#include "../checkboxwidget.h"
#include "../comboboxwidget.h"
#include "../sceneviewwidget.h"
#include "core/database/database.h"
#include "io/assetmanager.h"

ShaderPropertyWidget::ShaderPropertyWidget()
{
    allowBuiltinShaders = addCheckBox("Allow Builtin Shaders");
    autoReloadOnChange  = addCheckBox("Automatically Reload");
	vertexShaderCombo   = addComboBox("Vertex Shader");
	fragmentShaderCombo = addComboBox("Fragment Shader");

	shader = iris::ShaderPtr();

    QDirIterator it(":/assets/shaders", QDirIterator::Subdirectories);
    while (it.hasNext()) builtinShaders.append(it.next());
    builtinShaders.sort();

    onAllowBuiltinShaders(false);

    connect(vertexShaderCombo,	    SIGNAL(currentIndexChanged(int)),
            this,                   SLOT(onVertexShaderFileChanged(int)));

    connect(fragmentShaderCombo,    SIGNAL(currentIndexChanged(int)),
			this,                   SLOT(onFragmentShaderFileChanged(int)));

    connect(allowBuiltinShaders,    SIGNAL(valueChanged(bool)),
            this,                   SLOT(onAllowBuiltinShaders(bool)));
}

ShaderPropertyWidget::~ShaderPropertyWidget()
{

}

void ShaderPropertyWidget::onVertexShaderFileChanged(int index)
{
    auto vertexShader = vertexShaderCombo->getCurrentItemData();
    auto fragmentShader = fragmentShaderCombo->getCurrentItemData();
    
    for (auto asset : AssetManager::getAssets()) {
        if (asset->type == ModelTypes::Shader && asset->assetGuid == shaderGuid) {
            auto shaderObject = asset->getValue().toJsonObject();
            shaderObject["vertex_shader"] = vertexShader;
            shaderObject["fragment_shader"] = fragmentShader;
            asset->setValue(QVariant::fromValue(shaderObject));

            QFile jsonFile(asset->path);
            jsonFile.open(QIODevice::Truncate | QFile::WriteOnly);
            jsonFile.write(QJsonDocument(shaderObject).toJson());

            if (db->checkIfRecordExists("guid", vertexShader, "assets")) {
                if (db->checkIfRecordExists("depender", asset->assetGuid, "dependencies")) {
                    db->createDependency(
                        static_cast<int>(ModelTypes::Shader),
                        static_cast<int>(ModelTypes::File),
                        asset->assetGuid,
                        vertexShader,
                        Globals::project->getProjectGuid()
                    );
                }
                else {
                    db->updateGlobalDependencyDependee(
                        static_cast<int>(ModelTypes::File),
                        asset->assetGuid,
                        vertexShader
                    );
                }
            }
        }
    }
}

void ShaderPropertyWidget::onFragmentShaderFileChanged(int index)
{
    auto vertexShader = vertexShaderCombo->getCurrentItemData();
    auto fragmentShader = fragmentShaderCombo->getCurrentItemData();

    for (auto asset : AssetManager::getAssets()) {
        if (asset->type == ModelTypes::Shader && asset->assetGuid == shaderGuid) {
            auto shaderObject = asset->getValue().toJsonObject();
            shaderObject["vertex_shader"] = vertexShader;
            shaderObject["fragment_shader"] = fragmentShader;
            asset->setValue(QVariant::fromValue(shaderObject));

            QFile jsonFile(asset->path);
            jsonFile.open(QIODevice::Truncate | QFile::WriteOnly);
            jsonFile.write(QJsonDocument(shaderObject).toJson());

            if (db->checkIfRecordExists("guid", fragmentShader, "assets")) {
                if (db->checkIfRecordExists("depender", asset->assetGuid, "dependencies")) {
                    db->createDependency(
                        static_cast<int>(ModelTypes::Shader),
                        static_cast<int>(ModelTypes::File),
                        asset->assetGuid,
                        fragmentShader,
                        Globals::project->getProjectGuid()
                    );
                }
                else {
                    db->updateGlobalDependencyDependee(
                        static_cast<int>(ModelTypes::File),
                        asset->assetGuid,
                        fragmentShader
                    );
                }
            }
        }
    }
}

void ShaderPropertyWidget::onAllowBuiltinShaders(const bool toggle)
{
    if (toggle) {
        for (const auto &shader : builtinShaders) {
            auto shaderInfo = QFileInfo(shader);
            if (shaderInfo.suffix() == "vert") vertexShaderCombo->addItem(shaderInfo.baseName(), shader);
            if (shaderInfo.suffix() == "frag") fragmentShaderCombo->addItem(shaderInfo.baseName(), shader);
        }
    }

    for (auto asset : AssetManager::getAssets()) {
        if (asset->type == ModelTypes::File) {
            auto shaderInfo = QFileInfo(asset->fileName);
            if (shaderInfo.suffix() == "vert") vertexShaderCombo->addItem(shaderInfo.baseName(), asset->assetGuid);
            if (shaderInfo.suffix() == "frag") fragmentShaderCombo->addItem(shaderInfo.baseName(), asset->assetGuid);
        }
    }
}

void ShaderPropertyWidget::setShaderGuid(const QString &guid)
{
    shaderGuid = guid;

    for (auto asset : AssetManager::getAssets()) {
        if (asset->assetGuid == guid && asset->type == ModelTypes::Shader) {
            auto shaderObject = asset->getValue().toJsonObject();
            auto vShader = shaderObject["vertex_shader"].toString();
            auto fShader = shaderObject["fragment_shader"].toString();

            bool usesBuiltinAsset = false;
            for (const auto &asset : builtinShaders) {
                if (vShader == asset || fShader == asset) {
                    usesBuiltinAsset = true;
                    break;
                }
            }

            if (usesBuiltinAsset) {
                allowBuiltinShaders->setValue(true);
                for (const auto &shader : builtinShaders) {
                    auto shaderInfo = QFileInfo(shader);
                    if (shaderInfo.suffix() == "vert") vertexShaderCombo->addItem(shaderInfo.baseName(), shader);
                    if (shaderInfo.suffix() == "frag") fragmentShaderCombo->addItem(shaderInfo.baseName(), shader);
                }
            }

            int vindex = vertexShaderCombo->findData(vShader);
            if (vindex) vertexShaderCombo->setCurrentIndex(vindex);

            int findex = fragmentShaderCombo->findData(fShader);
            if (findex) fragmentShaderCombo->setCurrentIndex(findex);
        }
    }
}

void ShaderPropertyWidget::setDatabase(Database *db)
{
    this->db = db;
}

void ShaderPropertyWidget::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
}
