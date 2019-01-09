/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materialreader.hpp"
#include "irisgl.h"
#include "irisgl/Graphics.h"
#include <QMap>
#include "../constants.h"
#include "../io/assetmanager.h"
#include "../core/database/database.h"
#include "../core/guidmanager.h"
#include "../core/thumbnailmanager.h"
#include "../core/project.h"
#include "../core/settingsmanager.h"

// iris includes
#include "irisgl/src/materials/custommaterial.h"
//#include "irisgl/src/core/property.h"
#include "shadergraph/core/materialhelper.h"

/*
V1 Material Spec:
{
	"name":"Material", // material name
	"id":"12b12bbcf33g4", // shader asset guid

	// everything else are parameters
	"alpha":1.0,
	...
}

V2 Material Spec:
{
	"name":"Material", // material name
	"shaderGuid":"12b12bbcf33g4", // shader asset guid
	"version":2,

	// a dicionary is used because the value names should be unique
	values:{
		"alpha":1.0,
		...
	}
}

*/


MaterialReader::MaterialReader()
{

}

void MaterialReader::readJahShader(const QString &filePath)
{
    dir = AssetIOBase::getDirFromFileName(filePath);
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);

    auto data = file.readAll();
    file.close();
    auto doc = QJsonDocument::fromJson(data);

    parsedShader = doc.object();
}

QJsonObject MaterialReader::getParsedShader()
{
    return parsedShader;
}

iris::CustomMaterialPtr MaterialReader::parseMaterial(QJsonObject matObject, Database* db)
{
	auto version = getMaterialVersion(matObject);
	if (version == 1)
		matObject = convertV1MaterialToV2(matObject);

	// get shader object
	auto shaderObject = getShaderObjectFromId(matObject["shaderGuid"].toString());

	ShaderHandler handler;
	auto material = handler.loadMaterialFromShader(matObject, db);

	for (const auto &prop : material->properties) {
		if (prop->type == iris::PropertyType::Color) {
			QColor col;
			col.setNamedColor(matObject.value(prop->name).toString());
			material->setValue(prop->name, col);
		}
		else if (prop->type == iris::PropertyType::Texture) {
			QString materialName = db->fetchAsset(matObject.value(prop->name).toString()).name;
			QString textureStr = IrisUtils::join(Globals::project->getProjectFolder(), materialName);
			material->setValue(prop->name, !materialName.isEmpty() ? textureStr : QString());
		}
		else {
			material->setValue(prop->name, QVariant::fromValue(matObject.value(prop->name)));
		}
	}

	return material;
}

QJsonObject MaterialReader::getShaderObjectFromId(QString shaderGuid)
{
	QFileInfo shaderFile;

	if (Constants::Reserved::BuiltinShaders.contains(shaderGuid))
		shaderFile = QFileInfo(IrisUtils::getAbsoluteAssetPath(Constants::Reserved::BuiltinShaders[shaderGuid]));

	if (shaderFile.exists()) {
		QFile file(shaderFile.absoluteFilePath());
		auto data = file.readAll();
		return QJsonDocument::fromJson(data).object();
	}
	else {
		for (auto asset : AssetManager::getAssets()) {
			if (asset->type == ModelTypes::Shader) {
				if (asset->assetGuid == shaderGuid) {
					auto def = asset->getValue().toJsonObject();
					return def;
				}
			}
		}
	}

	return QJsonObject();

	
}

QJsonObject MaterialReader::convertV1MaterialToV2(QJsonObject oldMatObj)
{
	QJsonObject newMatObj;
	newMatObj["name"] = oldMatObj["name"];
	newMatObj["shaderGuid"] = oldMatObj["guid"];
	newMatObj["version"] = 2;

	QJsonObject values;
	for (auto key : oldMatObj.keys()) {
		if (key != "name" || key != "guid") {
			values[key] = oldMatObj[key];
		}
	}

	return newMatObj;

}

// if version code is present then return version
// otherwise return version 1.0
int MaterialReader::getMaterialVersion(QJsonObject matObj)
{
	if (matObj.contains("version"))
		return matObj["version"].toInt();

	return 1;
}

ShaderHandler::ShaderHandler()
{

}

iris::CustomMaterialPtr ShaderHandler::loadMaterialFromShader(QJsonObject shaderObject, Database* handle)
{
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
	material->setMaterialDefinition(def);
	material->generate(def);
}

iris::CustomMaterialPtr ShaderHandler::loadMaterialFromShaderV2(QJsonObject shaderObject, Database* db)
{
	MaterialHelper::generateMaterialFromMaterialDefinition(shaderObject, false);
}

iris::CustomMaterialPtr ShaderHandler::loadMaterialFromShaderV1(QJsonObject shaderObject, Database* db)
{
	iris::CustomMaterialPtr material = iris::CustomMaterialPtr::create();
	QFileInfo shaderFile;

	QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
	while (it.hasNext()) {
		it.next();
		if (it.key() == shaderObject["guid"].toString()) {
			shaderFile = QFileInfo(IrisUtils::getAbsoluteAssetPath(it.value()));
			break;
		}
	}

	if (shaderFile.exists()) {
		material->generate(shaderFile.absoluteFilePath());
	}
	else {
		for (auto asset : AssetManager::getAssets()) {
			if (asset->type == ModelTypes::Shader) {
				if (asset->assetGuid == shaderObject["guid"].toString()) {
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
					material->setMaterialDefinition(def);
					material->generate(def);
				}
			}
		}
	}

	return material;
}

int ShaderHandler::getShaderVersion(QJsonObject shaderObj)
{
	if (shaderObj.contains("version"))
		return shaderObj["version"].toInt();

	return 1;
}