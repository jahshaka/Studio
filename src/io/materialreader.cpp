/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/qtinterop.h"
#include <QJsonArray>

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

MaterialReader::MaterialReader(TextureSource texSrc)
{
	textureSource = texSrc;
}

void MaterialReader::setSource(TextureSource texSrc)
{
	textureSource = texSrc;
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
	auto resolve = [this](const QString &ref, const QString &slot) {
		return resolveTextureGuid(repairTextureSlot(ref, slot));
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

QString MaterialReader::resolveTextureGuid(const QString &guid)
{
	if (guid.isEmpty()) return QString();
	// Pin first in a project (resolvePinned falls back to the library source
	// itself), the library source for a store preview. THAT IS ALL (plan item
	// 15c). Two by-NAME fallbacks followed until then — `projectFolder +
	// row name` for a Project read and `globalSourceFolder + row name` for a
	// GlobalAssets one — and the project half existed for exactly one asset:
	// the default ground's Tile.png, which MainWindow::createDefaultScene
	// copied into the project folder under a bare catalog row the store knew
	// nothing about (the "reopen lighting blowout", 65,65,65 -> 255,255,255,
	// was that asymmetry between this reader and the writer). The tile is a
	// pinned store object now, like every texture, and both fallbacks went
	// with it; so did the folder parameter the GlobalAssets half carried.
	QSqlDatabase conn = QSqlDatabase::database();
	const QString root = AssetStorePaths::root();
	if (textureSource == TextureSource::Project && project && !project->getProjectGuid().isEmpty())
		return AssetCas::resolvePinned(conn, root, project->getProjectGuid(), guid);
	return AssetCas::resolveSource(conn, root, guid);
}

iris::MaterialPtr MaterialReader::parseMaterialTyped(QJsonObject matObject, Database* db, bool loadTextures)
{
	if (matObject["materialType"].toString() == "pbr")
		return parsePbrMaterial(matObject, db, loadTextures);

	// (THE SHADER-STUB BRANCH IS GONE — MATERIAL_BUNDLE_SPEC phase 2's Deletes
	// column. It served shape S2: a Material row holding `{shaderGuid,
	// values:{}}` and pointing at a separate ModelTypes::Shader row that held
	// the graph. Phase 1 collapsed the three shapes into ONE row whose
	// definition carries the graph as a payload and stamps materialType "pbr",
	// so a graph material takes the branch above; nothing mints a stub or a
	// Shader row any more, and the editor's last two minting sites — the asset
	// browser's "New Shader" and the `.shader` file importer — are deleted with
	// this branch rather than left as the reason to keep it.)

	// Reserved builtins, and any legacy material: parseMaterial converts.
	return parseMaterial(matObject, db, loadTextures);
}

iris::PbrMaterialPtr MaterialReader::parsePbrMaterial(QJsonObject matObject, Database* db, bool loadTextures)
{
	auto mat    = iris::PbrMaterial::create();
	auto values = matObject["values"].toObject();

	// THE EVALUATOR'S ARRAY SPELLINGS, SPLIT BEFORE ANYTHING READS THEM
	// (PRESET-UNIFY-1 fix round 2). `MaterialBundle::normaliseUv` does this at
	// the one WRITER, so nothing stores them any more — but definitions
	// written before it hold `textureScale: [u, v]` and `textureOffset:
	// [u, v]`, and the loop below visits PROPERTIES, keyed by the document's
	// own row names. `textureScale` at least matched a row and read as ZERO
	// (QJsonValue::toDouble() of an array); `textureOffset` matches no row at
	// all — the document's are `textureOffsetU`/`textureOffsetV` — so the
	// offset was silently DROPPED on read until the material was next
	// written. One split here, and both are the document's rows by the time
	// anything looks.
	{
		const auto split = [&values](const QString &key, const QString &uKey,
		                             const QString &vKey, double identity) {
			const QJsonValue value = values.value(key);
			if (!value.isArray()) return;
			const QJsonArray pair = value.toArray();
			const double u = pair.size() > 0 ? pair.at(0).toDouble(identity) : identity;
			if (!values.contains(uKey) || uKey == key) values[uKey] = u;
			if (!values.contains(vKey))
				values[vKey] = pair.size() > 1 ? pair.at(1).toDouble(u) : u;
			if (key != uKey) values.remove(key);
		};
		split(QStringLiteral("textureScale"), QStringLiteral("textureScale"),
		      QStringLiteral("textureScaleV"), 1.0);
		split(QStringLiteral("textureOffset"), QStringLiteral("textureOffsetU"),
		      QStringLiteral("textureOffsetV"), 0.0);
	}

	// Drive everything through setValue so both the shader-facing field and the
	// editor-facing Property object update (same contract as
	// SceneReader::readPbrMaterial, which reads these out of the scene blob).
	for (auto prop : mat->properties) {
		if (!values.contains(prop->name)) continue;
		const auto val = values.value(prop->name);

		switch (prop->type) {
		case iris::PropertyType::Float:
			// (No array branch here: the split above means a Float row's value
			// is a number by the time this loop sees it — ONE place, rather
			// than two that can disagree about what an array means.)
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
					path = resolveTextureGuid(stored);
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
		// No library = no stored definition (every caller guards `db` today;
		// this keeps the function honest on its own — lane DBPTR-1).
		if (!db) return QJsonObject();
		auto shader = db->fetchAssetData(shaderGuid);
        QJsonObject shaderDefinition = QJsonDocument::fromJson(shader).object();

		if (!shaderDefinition.isEmpty()) {
			const QString vPath = resolveTextureGuid(shaderDefinition["vertex_shader"].toString());
			const QString fPath = resolveTextureGuid(shaderDefinition["fragment_shader"].toString());

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
