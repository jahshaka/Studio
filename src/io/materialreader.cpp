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
#include "irisgl/core/logger.h"
#include "irisgl/core/irisutils.h"
#include "modules/materials/core/materialhelper.h"

MaterialReader::MaterialReader(TextureSource texSrc)
{
	textureSource = texSrc;
}

void MaterialReader::setSource(TextureSource texSrc)
{
	textureSource = texSrc;
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

	// FORWARD-ONLY-1: a definition that is not stamped "pbr" is from before
	// the one material class (a reserved builtin guid, a Blinn `values{}`
	// block, a v1 top-level material) and is REFUSED, never converted: the
	// caller gets the default PbrMaterial and the log names the refusal.
	Q_UNUSED(db);
	Q_UNUSED(loadTextures);
	irisLog(QStringLiteral("material reader: refused a material with materialType '%1' "
	                       "(only \"pbr\" is read) — it loads as the default material")
	            .arg(matObject.value(QStringLiteral("materialType")).toString()));
	auto mat = iris::PbrMaterial::create();
	const QString name = matObject.value(QStringLiteral("name")).toString();
	if (!name.isEmpty()) mat->setName(name);
	return mat;
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

QJsonObject MaterialReader::getShaderObjectFromId(QString shaderGuid, Database* db)
{
	// The row's stored definition blob — the one reader left is the generated
	// shader piece's regeneration (SceneReader::restoreCustomPieces).
	if (!db) return QJsonObject();
	return QJsonDocument::fromJson(db->fetchAssetData(shaderGuid)).object();
}
