#pragma once

#include <QJsonObject>
#include "irisgl/document/materials/custommaterial.h"
#include "pbrgraphevaluator.h"

class GraphNodeScene;
class NodeGraph;

class MaterialHelper
{
public:
	static bool materialHasEffect(QJsonObject matObj);

	static QString assetPath(QString relPath);

	// Converts a NodeGraph to the Material json format.
	// Since Option B phase 1 the result also carries "pbrMaterial", the
	// CPU-evaluated iris::PbrMaterial inputs of the graph (values + the list
	// of unsupported inputs) - see PbrGraphEvaluator.
	static QJsonObject serialize(NodeGraph* graph);

	// serialize + a FINAL-quality per-texel bake of the UV-varying chains
	// (MATERIALS_EVALUATOR_SPEC section 2): maps land in
	// <projectRoot>/BakedMaps/<bakeGuid>/ at the graph's bakeResolution and
	// "pbrMaterial" carries their project-relative paths. Falls back to the
	// plain serialize when no project root is set or bakeGuid is empty.
	static QJsonObject serializeWithBake(NodeGraph* graph, const QString& bakeGuid);

	// Option B phase 1: the graph evaluated to the document's PBR material
	// (which SceneMirror already mirrors into the engine). Texture-property
	// GUIDs are resolved through TextureManager.
	static iris::PbrMaterialPtr createPbrMaterialFromShaderGraph(NodeGraph* graph);

	// Rebuilds the evaluated PBR material from a stored material definition
	// (the "pbrMaterial" object written by serialize). Returns null when the
	// definition predates Option B and carries no evaluated output.
	static iris::PbrMaterialPtr createPbrMaterialFromDefinition(QJsonObject matObj);

	// Maps a texture property's stored asset GUID to an image path via
	// TextureManager; passes real file paths through untouched; resolves
	// project-relative baked-map paths (BakedMaps/...) against the project
	// root set below.
	static PbrGraphEvaluator::TextureResolver textureResolver();

	// The open project's folder, for resolving BakedMaps/... cache paths.
	// Set on project open / by the bake verb; empty when no project.
	static void setProjectRoot(const QString& folder);
	static QString projectRoot;

	static NodeGraph* extractNodeGraphFromMaterialDefinition(QJsonObject matObj);

	// (generateShader/createMaterialFromShaderGraph/generateMaterialFrom-
	// MaterialDefinition died in MATERIALS_EVALUATOR phase 5 — the GLSL
	// pipeline is gone. Graph-backed definitions load through
	// createPbrMaterialFromDefinition; the shader-less CustomMaterial fallback
	// lives in ShaderHandler::loadMaterialFromShaderV2.)

	static void parseMaterialProperties(iris::CustomMaterialPtr material, QJsonArray propList);

	static void parseMaterialStates(iris::CustomMaterialPtr material, QJsonObject matObj);
};