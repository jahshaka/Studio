/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/qtinterop.h"
#include "io/materialreader.h"
#include "irisgl/irisgl.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/import/model.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/texture.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/assets/shader.h"
#include "irisgl/document/materials/renderstates.h"
#include "irisgl/document/materials/rasterizerstate.h"
#include "irisgl/import/graphicshelper.h"
#include "irisgl/core/viewport.h"
#include <QMap>
#include "data/constants.h"
#include "io/assetmanager.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include <QSqlDatabase>
#include "services/thumbnailmanager.h"
#include "data/project.h"
#include "data/settingsmanager.h"

#include <QFileInfo>

// iris includes
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/core/irisutils.h"
//#include "irisgl/src/core/property.h"
#include "modules/materials/core/materialhelper.h"

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

QString MaterialReader::resolveTextureGuid(const QString &guid, Database *db)
{
	if (guid.isEmpty()) return QString();
	QSqlDatabase conn = QSqlDatabase::database();
	const QString root = AssetStorePaths::root();

	QString path;
	if (textureSource == TextureSource::Project && project && !project->getProjectGuid().isEmpty())
		path = AssetCas::resolvePinned(conn, root, project->getProjectGuid(), guid);
	else
		path = AssetCas::resolveSource(conn, root, guid);

	if (path.isEmpty() && textureSource == TextureSource::GlobalAssets && db) {
		const QString assetName = db->fetchAsset(guid).name;
		if (!assetName.isEmpty()) {
			const QString candidate = IrisUtils::join(globalSourceFolder, assetName);
			if (QFileInfo::exists(candidate)) path = candidate;
		}
	}

	// The PROJECT-FOLDER half of the same legacy fallback the WRITER still has.
	// SceneWriter::assetGuidForTexturePath resolves a texture path to a guid two
	// ways: through the CAS, and — when the file is not a store object — by
	// looking the catalog up by FILE NAME within the project
	// (Database::fetchAssetGUIDByName). That second branch is still live, and
	// still fires: MainWindow::createDefaultScene copies Tile.png straight into
	// the project folder and registers a bare catalog row, so the default
	// ground's texture is exactly such an asset — a guid the store knows nothing
	// about. The reader's matching branch was deleted when the pin world landed
	// ("the flat join(projectFolder, name) resolution is GONE", materialreader.h)
	// on the premise that project folders no longer hold asset files. They still
	// do, for that one asset, and the asymmetry silently ERASED the texture on
	// every save/reopen: the writer stored a guid, the reader resolved it to an
	// empty path, and the default floor reopened as bare white diffuse
	// (65,65,65 -> 255,255,255 — the "reopen lighting blowout", which was never
	// a lighting bug at all). Writer and reader have to agree; this is the
	// reader's half, and it is last-resort and existence-checked, so nothing in
	// the pin world changes shape because of it.
	if (path.isEmpty() && textureSource == TextureSource::Project && db &&
	    project && !project->getProjectFolder().isEmpty()) {
		const QString assetName = db->fetchAsset(guid).name;
		if (!assetName.isEmpty()) {
			const QString candidate = QDir(project->getProjectFolder()).filePath(assetName);
			if (QFileInfo::exists(candidate)) path = candidate;
		}
	}
	return path;
}

iris::MaterialPtr MaterialReader::parseMaterialTyped(QJsonObject matObject, Database* db, bool loadTextures)
{
	if (matObject["materialType"].toString() == "pbr")
		return parsePbrMaterial(matObject, db, loadTextures);

	// Graph-backed material assets - a shaderGuid whose stored definition
	// carries a shadergraph - load as the shader's baked PbrMaterial
	// (MATERIALS_EVALUATOR phase 5): folded values plus BakedMaps/<guid>/
	// textures, resolved against the open project. The CustomMaterial-from-
	// graph route is gone. A definition predating the evaluator (no
	// "pbrMaterial" object) falls through to the shader-less CustomMaterial
	// fallback; materials.regenerate rebuilds it.
	if (getMaterialVersion(matObject) >= 2) {
		const auto shaderGuid = matObject["shaderGuid"].toString();
		if (!shaderGuid.isEmpty() && db
			&& !Constants::Reserved::BuiltinShaders.contains(shaderGuid)) {
			const auto shaderObject = getShaderObjectFromId(shaderGuid, db);
			if (MaterialHelper::materialHasEffect(shaderObject)) {
				if (auto pbr = shaderDefinitionAsPbr(shaderObject,
				                                    project ? project->getProjectFolder() : QString()))
					return pbr;
			}
		}
	}

	return parseMaterial(matObject, db, loadTextures);
}

iris::MaterialPtr MaterialReader::parseShaderAsPbr(const QString &shaderGuid, Database *db)
{
	if (shaderGuid.isEmpty() || !db) return iris::MaterialPtr();
	// getShaderObjectFromId, not a raw fetch: it also serves the reserved
	// builtin shaders, which live as files (they carry no "pbrMaterial" and
	// therefore come back null — the caller decides what to show instead).
	const QJsonObject definition = getShaderObjectFromId(shaderGuid, db);
	return shaderDefinitionAsPbr(definition, project ? project->getProjectFolder() : QString());
}

iris::MaterialPtr MaterialReader::shaderDefinitionAsPbr(const QJsonObject &definition,
                                                        const QString &projectFolder)
{
	if (definition.isEmpty() || !definition.contains("pbrMaterial"))
		return iris::MaterialPtr();

	// BakedMaps/<guid>/*.png paths are project-relative: without a project root
	// they would reach the loader as literal relative strings and render as an
	// untextured half-material. Refuse instead (VISUAL_PARITY_SPEC §5.5 risk c).
	const QJsonObject pbrObj = definition["pbrMaterial"].toObject();
	const bool hasBakedMaps = !pbrObj["bakedMaps"].toObject().isEmpty();
	if (hasBakedMaps && projectFolder.isEmpty()) return iris::MaterialPtr();

	// MaterialHelper::projectRoot is process-wide state the resolver reads;
	// only write it when we actually have a project, so a project-less preview
	// never clears the open project's root.
	if (!projectFolder.isEmpty()) MaterialHelper::setProjectRoot(projectFolder);

	auto pbr = MaterialHelper::createPbrMaterialFromDefinition(definition);
	if (!pbr) return iris::MaterialPtr();
	return pbr.staticCast<iris::Material>();
}

iris::PbrMaterialPtr MaterialReader::parsePbrMaterial(QJsonObject matObject, Database* db, bool loadTextures)
{
	auto mat    = iris::PbrMaterial::create();
	auto values = matObject["values"].toObject();

	// Drive everything through setValue so both the shader-facing field and the
	// editor-facing Property object update (same contract as
	// SceneReader::readPbrMaterial, which reads these out of the scene blob).
	for (auto prop : mat->properties) {
		if (!values.contains(prop->name)) continue;
		const auto val = values.value(prop->name);

		switch (prop->type) {
		case iris::PropertyType::Float:
			mat->setValue(prop->name, static_cast<float>(val.toDouble()));
			break;
		case iris::PropertyType::Int:
			mat->setValue(prop->name, val.toInt());
			break;
		case iris::PropertyType::Color:
			mat->setValue(prop->name, QColor(val.toString()));
			break;
		case iris::PropertyType::Bool:
			mat->setValue(prop->name, val.toBool());
			break;
		case iris::PropertyType::Texture: {
			if (!loadTextures) break;
			// Stored as an asset guid (saved against the project database) or
			// as a path. Resolve the guid to the project/global file the same
			// way parseMaterial does; fall back to treating it as a path.
			const QString stored = val.toString();
				QString path;
				if (!stored.isEmpty()) {
					path = resolveTextureGuid(stored, db);
					if (path.isEmpty() && QFileInfo::exists(stored)) path = stored;
				}
			mat->setValue(prop->name, path);
			break;
		}
		default:
			break;
		}
	}

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
			material->setValue(prop->name, iris::toQt(vec));
		}
		else if (prop->type == iris::PropertyType::Vec3) {
			auto vec = readVector3(valuesObj[prop->name].toObject());
			material->setValue(prop->name, iris::toQt(vec));
		}
		else if (prop->type == iris::PropertyType::Vec4) {
			auto vec = readVector4(valuesObj[prop->name].toObject());
			material->setValue(prop->name, iris::toQt(vec));
		}
		else if (prop->type == iris::PropertyType::Texture && loadTextures) {
			auto texGuid = valuesObj.value(prop->name).toString();
			material->setValue(prop->name, resolveTextureGuid(texGuid, db));
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

		if (!shaderDefinition.isEmpty()) {
			const QString vPath = resolveTextureGuid(shaderDefinition["vertex_shader"].toString(), db);
			const QString fPath = resolveTextureGuid(shaderDefinition["fragment_shader"].toString(), db);

			if (!vPath.isEmpty()) shaderDefinition["vertex_shader"] = vPath;
			if (!fPath.isEmpty()) shaderDefinition["fragment_shader"] = fPath;

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
	// The GLSL pipeline died in MATERIALS_EVALUATOR phase 5: stored
	// vertexShaderSource/fragmentShaderSource keys are IGNORED (readers stay
	// tolerant of old files carrying them). The engine renders CustomMaterials
	// from their editable properties alone; graph-backed material assets load
	// as the shader's baked PbrMaterial via parseMaterialTyped's dispatch.
	auto mat = iris::CustomMaterial::create();
	mat->setMaterialDefinition(shaderObject);
	mat->setVersion(2);
	MaterialHelper::parseMaterialProperties(mat, shaderObject["properties"].toArray());
	MaterialHelper::parseMaterialStates(mat, shaderObject);
	return mat;
}

iris::CustomMaterialPtr ShaderHandler::loadMaterialFromShaderV1(QJsonObject shaderObject, Database* db)
{
	iris::CustomMaterialPtr material = iris::CustomMaterialPtr::create();

	auto vertexShader = shaderObject["vertex_shader"].toString();
	auto fragmentShader = shaderObject["fragment_shader"].toString();

	if (textureSource == TextureSource::GlobalAssets) {
		// Pin world: shader source files resolve by guid through the CAS
		// (globalSourceFolder used to be a guid joined onto the store root).
		QSqlDatabase conn = QSqlDatabase::database();
		const QString root = AssetStorePaths::root();
		const QString vPath = AssetCas::resolveSource(conn, root, vertexShader);
		const QString fPath = AssetCas::resolveSource(conn, root, fragmentShader);
		if (!vPath.isEmpty()) shaderObject["vertex_shader"] = vPath;
		if (!fPath.isEmpty()) shaderObject["fragment_shader"] = fPath;
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