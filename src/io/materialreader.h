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

#include "irisgl/irisglfwd.h"
#include "io/assetiobase.h"
#include "irisgl/irisglfwd.h"

class Database;
class Project;
enum class TextureSource
{
	Project,
	GlobalAssets
};

class MaterialReader : public AssetIOBase
{
	TextureSource textureSource;
	QString globalSourceFolder;

	// The live Project, injected by every construction site (Phase 4: was the
	// Globals::project static). Only read when textureSource == Project.
	Project *project = nullptr;
public:
    MaterialReader(TextureSource texSrc = TextureSource::Project, QString globalSourceFolder = "");
	void setSource(TextureSource texSrc, QString globalSrcFolder);
	void setProject(Project *p) { project = p; }

	/// Pin-world texture resolution (phase 4): guid → project pin → library
	/// source → the explicit global folder by recorded name (preview loads).
	/// The flat join(projectFolder, name) resolution is GONE.
	QString resolveTextureGuid(const QString &guid, Database *db);

    void readJahShader(const QString &filePath);
    QJsonObject getParsedShader();

	// if handle is null then it will try to fetch the assets
	// from the asset manager
	iris::CustomMaterialPtr parseMaterial(QJsonObject matObject, Database* handle, bool loadTextures = true);

	// Dispatches on the "materialType" tag SceneWriter stamps on every saved
	// material: "pbr" rebuilds a PbrMaterial (parseMaterial would force it
	// through the shader-guid CustomMaterial path — a pbr definition has no
	// shaderGuid, so it came back as a broken default material and the PBR
	// values were silently dropped); anything else takes the legacy
	// CustomMaterial path unchanged.
	iris::MaterialPtr parseMaterialTyped(QJsonObject matObject, Database* handle, bool loadTextures = true);
	iris::PbrMaterialPtr parsePbrMaterial(QJsonObject matObject, Database* handle, bool loadTextures = true);
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
