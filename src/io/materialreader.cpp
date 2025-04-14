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

MaterialReader::MaterialReader(TextureSource texSrc, QString globalSrcFolder)
{
	textureSource = texSrc;
	globalSourceFolder = globalSrcFolder;
}

void MaterialReader::setSource(TextureSource texSrc, QString globalSrcFolder)
{
	textureSource = texSrc;
	globalSourceFolder = globalSrcFolder;
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

iris::CustomMaterialPtr MaterialReader::createMaterialFromShaderGuid(QString shaderGuid, Database* db)
{
	auto shaderObject = getShaderObjectFromId(shaderGuid, db);

	ShaderHandler handler(textureSource, globalSourceFolder);
	auto material = handler.loadMaterialFromShader(shaderObject, nullptr);
	material->setGuid(shaderGuid);

	return material;
}

iris::CustomMaterialPtr MaterialReader::createMaterialFromShaderFile(QString shaderPath, Database* db)
{
	QFile file(shaderPath);
	file.open(QIODevice::ReadOnly);
	auto data = file.readAll();
	auto shaderObj = QJsonDocument::fromJson(data).object();

	ShaderHandler handler(textureSource, globalSourceFolder);
	auto mat = handler.loadMaterialFromShader(shaderObj, db);

	return mat;
}

iris::CustomMaterialPtr MaterialReader::parseMaterial(QJsonObject matObject, Database* db, bool loadTextures)
{
	auto version = getMaterialVersion(matObject);
	if (version == 1) matObject = convertV1MaterialToV2(matObject);

	// get shader object
	auto shaderGuid = matObject["shaderGuid"].toString();
	auto shaderObject = getShaderObjectFromId(shaderGuid, db);
	auto material = createMaterialFromShaderGuid(shaderGuid, db);

	// apply values
	auto valuesObj = matObject["values"].toObject();

	for (const auto prop : material->properties) {
		if (prop->type == iris::PropertyType::Color) {
			QColor col;
			col.setNamedColor(valuesObj.value(prop->name).toString());
			material->setValue(prop->name, col);
		}
		else if (prop->type == iris::PropertyType::Vec2) {
			auto vec = readVector2(valuesObj[prop->name].toObject());
			material->setValue(prop->name, vec);
		}
		else if (prop->type == iris::PropertyType::Vec3) {
			auto vec = readVector3(valuesObj[prop->name].toObject());
			material->setValue(prop->name, vec);
		}
		else if (prop->type == iris::PropertyType::Vec4) {
			auto vec = readVector4(valuesObj[prop->name].toObject());
			material->setValue(prop->name, vec);
		}
		else if (prop->type == iris::PropertyType::Texture && loadTextures) {
			if (db != nullptr) {
				auto texGuid = valuesObj.value(prop->name).toString();
				QString materialName = db->fetchAsset(texGuid).name;
				QString textureStr;

				if (textureSource == TextureSource::Project) textureStr = IrisUtils::join(Globals::project->getProjectFolder(), materialName);
				else textureStr = IrisUtils::join(globalSourceFolder, materialName);

				material->setValue(prop->name, !materialName.isEmpty() ? textureStr : QString());
			}
			else {
				// todo: resolve textures from assets instead
			}
		}
		else {
			// float, int, bool
			material->setValue(prop->name, QVariant::fromValue(valuesObj.value(prop->name)));
		}
	}

	//material->setMaterialDefinition(matObject);

	return material;
}

//todo : use db when possible
QJsonObject MaterialReader::getShaderObjectFromId(QString shaderGuid, Database* db)
{
	QFileInfo shaderFile;

	if (Constants::Reserved::BuiltinShaders.contains(shaderGuid)) {
		auto shaderPath = IrisUtils::getAbsoluteAssetPath(Constants::Reserved::BuiltinShaders[shaderGuid]);
		shaderFile = QFileInfo(shaderPath);
	}

	if (shaderFile.exists()) {
		QFile file(shaderFile.absoluteFilePath());
		file.open(QIODevice::ReadOnly);
		auto data = file.readAll();
		return QJsonDocument::fromJson(data).object();
	}
	else {
		// Stop using asset manager... (iKlsR)
		// TODO remove all usage of such
		auto shader = db->fetchAssetData(shaderGuid);
        QJsonObject shaderDefinition = QJsonDocument::fromJson(shader).object();

		if (textureSource == TextureSource::Project) globalSourceFolder = Globals::project->getProjectFolder();

		if (!shaderDefinition.isEmpty()) {
			auto vAsset = db->fetchAsset(shaderDefinition["vertex_shader"].toString());
			auto fAsset = db->fetchAsset(shaderDefinition["fragment_shader"].toString());

			if (!vAsset.name.isEmpty()) shaderDefinition["vertex_shader"] = QDir(globalSourceFolder).filePath(vAsset.name);
			if (!fAsset.name.isEmpty()) shaderDefinition["fragment_shader"] = QDir(globalSourceFolder).filePath(fAsset.name);

			return shaderDefinition;
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

	newMatObj["values"] = values;

	return newMatObj;
}

// if version code is present then return version
// otherwise return version 1.0
int MaterialReader::getMaterialVersion(QJsonObject matObj)
{
	if (matObj.contains("version")) return matObj["version"].toInt();
	return 1;
}

ShaderHandler::ShaderHandler(TextureSource texSrc, QString globalSrcFolder)
{
	textureSource = texSrc;
	globalSourceFolder = globalSrcFolder;
}

iris::CustomMaterialPtr ShaderHandler::loadMaterialFromShader(QJsonObject shaderObject, Database* db)
{
	if (getShaderVersion(shaderObject) == 1) return loadMaterialFromShaderV1(shaderObject, db);
	return loadMaterialFromShaderV2(shaderObject, db);
}

iris::CustomMaterialPtr ShaderHandler::loadMaterialFromShaderV2(QJsonObject shaderObject, Database* db)
{
	return MaterialHelper::generateMaterialFromMaterialDefinition(shaderObject, false);
}

iris::CustomMaterialPtr ShaderHandler::loadMaterialFromShaderV1(QJsonObject shaderObject, Database* db)
{
	iris::CustomMaterialPtr material = iris::CustomMaterialPtr::create();

	auto vertexShader = shaderObject["vertex_shader"].toString();
	auto fragmentShader = shaderObject["fragment_shader"].toString();

	if (textureSource == TextureSource::GlobalAssets) {
		QString assetPath = IrisUtils::join(
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), Constants::ASSET_FOLDER, globalSourceFolder
		);

		auto vAsset = db->fetchAsset(vertexShader);
		auto fAsset = db->fetchAsset(fragmentShader);

		if (!vAsset.name.isEmpty()) shaderObject["vertex_shader"] = QDir(assetPath).filePath(vAsset.name);
		if (!fAsset.name.isEmpty()) shaderObject["fragment_shader"] = QDir(assetPath).filePath(fAsset.name);
	}
	else {
		for (auto asset : AssetManager::getAssets()) {
			if (asset->type == ModelTypes::File) {
				if (vertexShader == asset->assetGuid) vertexShader = asset->path;
				if (fragmentShader == asset->assetGuid) fragmentShader = asset->path;
			}
		}

		shaderObject["vertex_shader"] = vertexShader;
		shaderObject["fragment_shader"] = fragmentShader;
	}
	
	
	//qDebug() << "shader vertex file: " << vertexShader;
	//qDebug() << "shader fragment file: " << fragmentShader;
	material->setMaterialDefinition(shaderObject);
	material->generate(shaderObject);
	material->setVersion(1);

	return material;
}

int ShaderHandler::getShaderVersion(QJsonObject shaderObj)
{
	if (shaderObj.contains("version"))
		return shaderObj["version"].toInt();

	return 1;
}
