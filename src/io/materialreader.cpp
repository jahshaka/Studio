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
#include "modules/materials/core/materialhelper.h"
#include "modules/materials/core/pieceemitter.h"
#include "modules/materials/graph/nodegraph.h"
#include "services/assethome.h"
#include "irisgl/irisgl.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/texture.h"
#include "irisgl/document/assets/texture2d.h"
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

QString MaterialReader::resolveTextureGuid(const QString &guid)
{
	if (guid.isEmpty()) return QString();
	// Pin first in a project (resolvePinned falls back to the library source
	// itself), the library source for a store preview. THAT IS ALL (plan item
	// 15c). Two by-NAME fallbacks followed until then — `projectFolder +
	// row name` for a Project read and `globalSourceFolder + row name` for a
	// GlobalAssets one — and the project half existed for exactly one asset:
	// the then-default ground's Tile.png, which the new-scene template once
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
			// Stored as the texture's ASSET GUID (TEX-REF-1): the row binds the
			// file the guid resolves to AND the guid, so the identity reaches
			// the next save. A miss keeps the guid with no file; there is no
			// path arm (a definition stores no path).
			const QString stored = val.toString();
			mat->setValue(prop->name, iris::Material::textureRef(
				stored.isEmpty() ? QString() : resolveTextureGuid(stored), stored));
			break;
		}
		default:
			break;
		}
	}
	// THE UV SCROLL (TORNADO-1): graph-owned, not a Property row — the animated
	// UV fold writes it as a two-element array (GraphBaker::runCompiled).
	{
		const QJsonArray velocity = values.value(QStringLiteral("textureVelocity")).toArray();
		if (velocity.size() == 2) {
			mat->setValue(QStringLiteral("textureVelocityU"), velocity[0].toDouble());
			mat->setValue(QStringLiteral("textureVelocityV"), velocity[1].toDouble());
		}
	}
	restoreGeneratedPieces(mat, matObject);

	return mat;
}

// THE GENERATED SHADER PIECES TRAVEL WITH THE DEFINITION (TORNADO-1). A graph
// material whose emitter took sockets says so in its bake record
// (`bake.emittedSockets`, GraphDefinition::buildDefinition) and carries its
// graph (`shadergraph`); emission is deterministic and content-addressed, so
// re-emitting here names the same files the save did — a cache hit in the
// ordinary case, a rewrite on a wiped cache or another machine. Every reader of
// a definition (an apply from the library, a hover preview, a thumbnail, a
// scene's regeneration of a missing piece) therefore gets a LIVE material, by
// this one route. A definition with no emitted sockets costs nothing here.
void MaterialReader::restoreGeneratedPieces(iris::PbrMaterialPtr mat, const QJsonObject& matObject)
{
	if (!mat) return;
	const QJsonArray emitted = matObject.value(QStringLiteral("bake")).toObject()
	                               .value(QStringLiteral("emittedSockets")).toArray();
	if (emitted.isEmpty() || !matObject.contains(QStringLiteral("shadergraph"))) return;
	NodeGraph* graph = MaterialHelper::extractNodeGraphFromMaterialDefinition(matObject);
	if (!graph) return;
	// BUILDING a material is a read: PathOnly, never an import.
	MaterialHelper::resolveAppRelativeTextures(graph, MaterialHelper::TextureBinding::PathOnly,
	                                           assethome::materials());
	const QJsonObject pieces = materials::PieceEmitter::emitAndStore(graph, MaterialHelper::textureResolver());
	mat->setCustomPiecePixel(pieces.value(QStringLiteral("customPiecePixel")).toString());
	mat->setCustomPieceVertex(pieces.value(QStringLiteral("customPieceVertex")).toString());
	delete graph;
}

QJsonObject MaterialReader::getShaderObjectFromId(QString shaderGuid, Database* db)
{
	// The row's stored definition blob — the one reader left is the generated
	// shader piece's regeneration (SceneReader::restoreCustomPieces).
	if (!db) return QJsonObject();
	return QJsonDocument::fromJson(db->fetchAssetData(shaderGuid)).object();
}
