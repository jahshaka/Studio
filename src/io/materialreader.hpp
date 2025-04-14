/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALREADER_HPP
#define MATERIALREADER_HPP

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonValueRef>
#include <QJsonDocument>
#include <QSharedPointer>

#include "../irisgl/src/irisglfwd.h"
#include "assetiobase.h"
#include "irisglfwd.h"

class Database;
enum class TextureSource
{
	Project,
	GlobalAssets
};

class MaterialReader : public AssetIOBase
{
	TextureSource textureSource;
	QString globalSourceFolder;
public:
    MaterialReader(TextureSource texSrc = TextureSource::Project, QString globalSourceFolder = "");
	void setSource(TextureSource texSrc, QString globalSrcFolder);

    void readJahShader(const QString &filePath);
    QJsonObject getParsedShader();

	// if handle is null then it will try to fetch the assets
	// from the asset manager
	iris::CustomMaterialPtr parseMaterial(QJsonObject matObject, Database* handle, bool loadTextures = true);
	iris::CustomMaterialPtr createMaterialFromShaderGuid(QString shaderGuid, Database* db);
	iris::CustomMaterialPtr createMaterialFromShaderFile(QString shaderPath, Database* db);
	QJsonObject getShaderObjectFromId(QString shaderGuid, Database* db);
	iris::CustomMaterialPtr loadMaterialV2(QJsonObject matObject, Database* handle);
	iris::CustomMaterialPtr loadMaterialV1(QJsonObject matObject, Database* handle);
	QJsonObject convertV1MaterialToV2(QJsonObject mat);

	int getMaterialVersion(QJsonObject oldMatObj);

private:
    QJsonObject parsedShader;
};

// used by material reader to create a material from a shader
class ShaderHandler : public AssetIOBase
{
	TextureSource textureSource;
	QString globalSourceFolder;
public:
	ShaderHandler(TextureSource texSrc = TextureSource::Project, QString globalSourceFolder = "");

	iris::CustomMaterialPtr loadMaterialFromShader(QJsonObject shaderObject, Database* handle);
	iris::CustomMaterialPtr loadMaterialFromShaderV2(QJsonObject shaderObject, Database* handle);
	iris::CustomMaterialPtr loadMaterialFromShaderV1(QJsonObject shaderObject, Database* handle);

	int getShaderVersion(QJsonObject shaderObj);

private:
	QJsonObject parsedShader;
};

#endif // MATERIALREADER_HPP
