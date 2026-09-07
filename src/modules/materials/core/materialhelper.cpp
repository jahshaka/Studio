/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "irisgl/core/math/qtinterop.h"
#include "irisgl/core/math/vec.h"
#include "materialhelper.h"
#include "../graph/graphnodescene.h"
#include "../graph/nodegraph.h"
#include "graphbaker.h"
#include "pbrgraphevaluator.h"
#include "pieceemitter.h"
#include "texturemanager.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include <QFileInfo>
#include <QJsonObject>
#include "irisgl/document/materials/pbrmaterial.h"
#include "../models/libraryv1.h"
#include "../nodes/test.h"

/*
EFFECT SHADER FORMAT (v2, post MATERIALS_EVALUATOR phase 5)
===============
{
	version:2,
	name:"",
	type:"effect",

	// the graph itself
	shadergraph:{ },

	// CPU-evaluated engine material: folded values + baked map paths
	pbrMaterial:{ values, bakedMaps, ... },

	properties : { },   // legacy graph-global uniforms — readable forever
	states : { }

	// Old files also carry vertexShaderSource/fragmentShaderSource — the GLSL
	// pipeline died in phase 5; readers TOLERATE the keys, writers never emit
	// them again.
}
*/

bool MaterialHelper::materialHasEffect(QJsonObject matObj)
{
	if (matObj.contains("shadergraph"))
		return true;
	return false;
}

// provides asset path for shadergraph assets
QString MaterialHelper::assetPath(QString relPath)
{
#ifdef EFFECT_BUILD_AS_LIB
	return IrisUtils::getAbsoluteAssetPath(QString("app") + QDir::separator() + QString("shadergraph") + QDir::separator() + relPath);
#else
	return QDir::cleanPath(QDir::currentPath() + QDir::separator() + "assets" + QDir::separator() + relPath);
#endif
}

int MaterialHelper::resolveAppRelativeTextures(NodeGraph* graph)
{
	if (!graph) return 0;
	int resolved = 0;
	for (auto node : graph->nodes.values()) {
		if (node->typeName != "texture") continue;
		auto texNode = static_cast<TextureNode*>(node);
		if (!texNode->getTexturePath().isEmpty()) continue;   // already resolved
		const auto rel = texNode->getTextureGuid();
		if (rel.isEmpty()) continue;
		const auto abs = assetPath(rel);
		if (!QFileInfo::exists(abs)) continue;
		GraphTexture* graphTexture = TextureManager::getSingleton()->importTexture(abs);
		if (!graphTexture) continue;
		texNode->setTextureGuid(graphTexture->guid);
		resolved++;
	}
	return resolved;
}

QJsonObject MaterialHelper::serialize(NodeGraph* graph)
{
	QJsonObject matObj;
	matObj["name"] = graph->settings.name;
	matObj["version"] = 2.0;
	matObj["type"] = "effect";
	matObj["shaderGuid"] = "";

	matObj["shadergraph"] = graph->serialize();

	// (vertexShaderSource/fragmentShaderSource are gone — phase 5. Readers
	// stay tolerant of old files that carry them.)

	// properties are the same as the ones in the shadergraph, they're
	// just placed here for convenience
	matObj["properties"] = matObj["shadergraph"].toObject()["properties"];

	matObj["states"] = matObj["shadergraph"].toObject()["settings"];

	// "pbrMaterial" is the engine-facing output: the CPU-evaluated
	// iris::PbrMaterial inputs of the graph (see pbrgraphevaluator.h).
	auto evaluated = PbrGraphEvaluator::evaluate(graph, textureResolver());
	QJsonObject pbrObj;
	pbrObj["values"] = evaluated.values;
	pbrObj["unsupportedNodes"] = QJsonArray::fromStringList(evaluated.unsupportedNodes);
	pbrObj["surfaceType"] = evaluated.hasPbrMaster ? "pbr" : "surface";
	matObj["pbrMaterial"] = pbrObj;

	return matObj;
}

QJsonObject MaterialHelper::serializeWithBake(NodeGraph* graph, const QString& bakeGuid)
{
	QJsonObject matObj = serialize(graph);
	if (!graph || bakeGuid.isEmpty() || projectRoot.isEmpty())
		return matObj;

	// THE EMITTER RUNS FIRST (HLMS_ADOPTION P5), because what it takes the
	// baker must not spend time on. Both backends are driven from the same
	// compiled graph, and a socket lands on exactly one of them.
	materials::PieceEmitter::Result emitted;
	const QJsonObject pieces = materials::PieceEmitter::emitAndStore(graph, textureResolver(),
	                                                                &emitted);

	materials::GraphBaker::Options opts;
	opts.resolution = graph->settings.bakeResolution;
	opts.outputDir = projectRoot + "/BakedMaps/" + bakeGuid;
	opts.relativePrefix = "BakedMaps/" + bakeGuid + "/";
	opts.emittedSockets = emitted.emittedSockets;
	const auto baked = materials::GraphBaker::run(graph, opts, textureResolver());

	QJsonObject pbrObj = matObj["pbrMaterial"].toObject();
	pbrObj["values"] = baked.eval.values;
	pbrObj["unsupportedNodes"] = QJsonArray::fromStringList(baked.eval.unsupportedNodes);
	pbrObj["approximatedNodes"] = QJsonArray::fromStringList(baked.eval.approximatedNodes);
	pbrObj["animated"] = baked.eval.animated || emitted.animated;
	pbrObj["bakedMaps"] = baked.maps;
	// The piece record is a FLAG plus the sockets it owns — never the machine's
	// file paths. Pieces live in a per-USER cache, so a stored absolute path
	// would be wrong on the next machine to open the project; the definition
	// carries the graph, and the graph re-emits byte-identical source (and
	// therefore the identical content-addressed name) wherever it is opened.
	if (emitted.accepted) {
		QJsonObject pieceObj;
		pieceObj["emittedSockets"] = QJsonArray::fromStringList(emitted.emittedSockets);
		pieceObj["animated"] = emitted.animated;
		if (pieces.contains("customPiecePixel")) pieceObj["pixel"] = true;
		if (pieces.contains("customPieceVertex")) pieceObj["vertex"] = true;
		pbrObj["customPiece"] = pieceObj;
	}
	matObj["pbrMaterial"] = pbrObj;
	return matObj;
}

QString MaterialHelper::projectRoot;

void MaterialHelper::setProjectRoot(const QString& folder)
{
	projectRoot = folder;
}

PbrGraphEvaluator::TextureResolver MaterialHelper::textureResolver()
{
	return [](const QString& value) -> QString {
		if (value.isEmpty() || QFileInfo::exists(value))
			return value;
		// project-relative baked-map cache paths (MATERIALS_EVALUATOR_SPEC
		// section 1.6) resolve against the open project's folder
		if (value.startsWith(QStringLiteral("BakedMaps/")) && !projectRoot.isEmpty()) {
			const QString abs = projectRoot + "/" + value;
			if (QFileInfo::exists(abs)) return abs;
		}
		// treat as an asset GUID already loaded by the graph's TextureManager
		for (auto tex : TextureManager::getSingleton()->textures) {
			if (tex->guid == value)
				return tex->path;
		}
		// a texture guid the TextureManager never loaded (library material
		// referencing store textures): resolve through the CAS rather than
		// handing the raw guid to a path-based loader
		{
			QSqlDatabase conn = QSqlDatabase::database();
			const QString path = AssetCas::resolveSource(conn, AssetStorePaths::root(), value);
			if (!path.isEmpty()) return path;
		}
		return value;
	};
}

iris::PbrMaterialPtr MaterialHelper::createPbrMaterialFromShaderGraph(NodeGraph* graph)
{
	auto material = PbrGraphEvaluator::createMaterial(graph, textureResolver());
	applyEmittedPieces(graph, material);
	return material;
}

materials::PieceEmitter::Result MaterialHelper::applyEmittedPieces(NodeGraph* graph,
                                                                   iris::PbrMaterialPtr material)
{
	materials::PieceEmitter::Result result;
	if (!graph || !material) return result;
	const QJsonObject pieces = materials::PieceEmitter::emitAndStore(graph, textureResolver(),
	                                                                &result);
	// Set BOTH, always, including to empty: a material being re-evaluated after
	// an edit that made its graph un-emittable has to LOSE the piece it had, or
	// the renderer would keep drawing the previous surface.
	material->setCustomPiecePixel(pieces["customPiecePixel"].toString());
	material->setCustomPieceVertex(pieces["customPieceVertex"].toString());
	return result;
}

iris::PbrMaterialPtr MaterialHelper::createPbrMaterialFromDefinition(QJsonObject matObj)
{
	if (!matObj.contains("pbrMaterial"))
		return iris::PbrMaterialPtr();

	const QJsonObject pbrObj = matObj["pbrMaterial"].toObject();
	auto values = pbrObj["values"].toObject();
	auto material = PbrGraphEvaluator::materialFromValues(values, textureResolver());

	// RE-EMIT (HLMS_ADOPTION P5). The definition says a piece exists but never
	// where: the file lives in a per-user cache under a name that is a hash of
	// its own bytes, so re-emitting from the stored GRAPH reproduces the same
	// name and either finds the file already there (the ordinary case, free) or
	// writes it back (a wiped cache, a fresh machine, a first open). That is
	// what makes the cache genuinely disposable.
	//
	// Only for definitions that HAVE a piece: deserializing a graph is not
	// free, and an ordinary baked material must not pay for a feature it does
	// not use.
	if (material && pbrObj.contains("customPiece")) {
		if (NodeGraph* graph = extractNodeGraphFromMaterialDefinition(matObj)) {
			resolveAppRelativeTextures(graph);
			const QJsonObject pieces =
			    materials::PieceEmitter::emitAndStore(graph, textureResolver());
			material->setCustomPiecePixel(pieces["customPiecePixel"].toString());
			material->setCustomPieceVertex(pieces["customPieceVertex"].toString());
			delete graph;
		}
	}
	return material;
}

NodeGraph* MaterialHelper::extractNodeGraphFromMaterialDefinition(QJsonObject matObj)
{
	auto graphObj = matObj["shadergraph"].toObject();
	auto graph = NodeGraph::deserialize(graphObj, new LibraryV1());

	return graph;
}


