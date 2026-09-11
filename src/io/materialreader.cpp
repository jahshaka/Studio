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
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/texture.h"
#include "irisgl/document/assets/texture2d.h"
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
#include "irisgl/document/materials/pbrmaterial.h"
#include "io/builtinmaterials.h"
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

// THE BUILTIN RETIREMENT (HLMS_ADOPTION P4b). A reserved guid used to name a
// `.shader` file that ShaderHandler turned into an iris::CustomMaterial; it now
// names a PbrMaterial PRESET. The guid survives — it is on disk in every scene
// that ever used a builtin — and only what it means has changed.
iris::PbrMaterialPtr MaterialReader::createMaterialFromShaderGuid(QString shaderGuid, Database* db,
                                                                  const QJsonObject &values)
{
	// THE SLOT TRAVELS WITH THE VALUE (hygiene lane, 2026-09-09). A legacy
	// material's texture rows go through the same repair the typed reader and
	// SceneReader already run: a slot naming the OBJECT a texture was imported
	// inside (the 2026-09-03 save defect — every slot on a model collapsed onto
	// one guid) is resolved to the member texture the slot's role words name.
	// Without the slot name this resolver could not be given the repair at all,
	// which is why the resolver signature carries it now.
	auto resolve = [this, db](const QString &ref, const QString &slot) {
		return resolveTextureGuid(repairTextureSlot(ref, slot), db);
	};
	if (BuiltinMaterials::isBuiltin(shaderGuid))
		return BuiltinMaterials::fromBuiltin(shaderGuid, values, resolve);

	// Not a builtin: a legacy shader material whose GLSL is long gone. Carry
	// across every uniform name that still has a meaning and drop the rest —
	// there is no other material class left to fall back to, and refusing to
	// open the scene would be the worse answer.
	auto mat = BuiltinMaterials::fromLegacyValues(values, resolve);
	mat->setGuid(shaderGuid);
	return mat;
}

QString MaterialReader::repairTextureSlot(const QString &stored, const QString &slotName)
{
	return AssetCas::repairTextureSlot(stored, slotName, "material reader");
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
	// textures, resolved against the open project. A definition predating the
	// evaluator (no "pbrMaterial" object) falls through to parseMaterial's
	// legacy-uniform conversion; materials.regenerate rebuilds it properly.
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

	// Reserved builtins, and any legacy shader material: parseMaterial converts.
	return parseMaterial(matObject, db, loadTextures);
}

iris::MaterialPtr MaterialReader::parseShaderAsPbr(const QString &shaderGuid, Database *db)
{
	if (shaderGuid.isEmpty() || !db) return iris::MaterialPtr();
	// A reserved builtin is now a PbrMaterial PRESET, so a shader-asset preview
	// or thumbnail of one has something real to show. Before HLMS_ADOPTION P4b
	// this returned null for every builtin (they carry no "pbrMaterial" block)
	// and every caller fell back to a placeholder.
	if (BuiltinMaterials::isBuiltin(shaderGuid))
		return BuiltinMaterials::fromBuiltin(shaderGuid, QJsonObject(), {});

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
		// An enum row stores (and serializes) a plain int — see SceneWriter.
		case iris::PropertyType::List:
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
			// The stored guid is REPAIRED first when it names the model a
			// texture was imported inside rather than the texture itself —
			// the same defect SceneReader::repairTextureSlot documents, on the
			// other reader. A material ASSET definition written by the import
			// pipeline was never wrong (the importer substitutes member guids
			// itself); one re-saved through SceneWriter between 2026-09-03 and
			// 2026-09-09 was.
			const QString stored = repairTextureSlot(val.toString(), prop->name);
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

iris::PbrMaterialPtr MaterialReader::parseMaterial(QJsonObject matObject, Database* db, bool loadTextures)
{
	auto version = getMaterialVersion(matObject);
	if (version == 1) matObject = convertV1MaterialToV2(matObject);

	const QString shaderGuid = matObject["shaderGuid"].toString();
	const QJsonObject values = matObject["values"].toObject();

	// ONE call, and the values go IN rather than being applied after: a builtin
	// preset and its saved values are not two independent things — Flat's
	// `color` IS its base colour, Default's `shininess` IS its roughness — so
	// the conversion needs both at once (io/builtinmaterials.h).
	auto material = createMaterialFromShaderGuid(shaderGuid,
	                                             loadTextures ? db : nullptr,
	                                             loadTextures ? values : QJsonObject());
	if (!loadTextures) {
		// Textures deliberately skipped, but everything else still applies.
		auto novalues = values;
		for (const char *key : { "diffuseTexture", "baseColorMap", "albedoMap",
		                         "normalTexture", "normalMap", "emissiveMap" })
			novalues.remove(QLatin1String(key));
		material = createMaterialFromShaderGuid(shaderGuid, nullptr, novalues);
	}
	const QString name = matObject["name"].toString();
	if (!name.isEmpty()) material->setName(name);
	material->setGuid(shaderGuid);
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
