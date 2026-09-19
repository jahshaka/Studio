#pragma once

#include <QJsonObject>
#include "pbrgraphevaluator.h"
#include "pieceemitter.h"

class GraphNodeScene;
class NodeGraph;

class MaterialHelper
{
public:
	static bool materialHasEffect(QJsonObject matObj);

	static QString assetPath(QString relPath);

	// Resolves a just-deserialized graph's APP-RELATIVE texture references
	// ("wood.jpg", "materials_to_graph/brick diff.jpg") into TextureManager
	// guids, importing the image on first use. New-format templates (the
	// shipped .effect presets, re-saved through the evaluator migration) carry
	// their images that way and are unusable until this runs.
	//
	// It used to live inline in EffectsPage::loadGraphFromTemplate, so only the
	// UI's "instantiate a template" path got it: materials.loadGraph on the
	// same file left every preset texture unconnected (samples audit,
	// 2026-09-04). Idempotent — a node whose path already resolved is skipped.
	// Returns the number of textures resolved.
	static int resolveAppRelativeTextures(NodeGraph* graph);

	// Converts a NodeGraph to the Material json format.
	// Since Option B phase 1 the result also carries "pbrMaterial", the
	// CPU-evaluated iris::PbrMaterial inputs of the graph (values + the list
	// of unsupported inputs) - see PbrGraphEvaluator.
	static QJsonObject serialize(NodeGraph* graph);

	// (serializeWithBake is DELETED — MATERIAL_BUNDLE_SPEC phase 1's Deletes
	// column. It baked into `<projectRoot>/BakedMaps/<guid>/` and wrote those
	// project-relative paths into the definition, which is why a graph
	// material could not be read with no project open and why its maps never
	// travelled in an exported project. A graph becomes a definition in
	// materials::buildDefinition now — core/graphdefinition.h — and its maps
	// are member textures in the store.)

	// Option B phase 1: the graph evaluated to the document's PBR material
	// (which SceneMirror already mirrors into the engine). Texture-property
	// GUIDs are resolved through TextureManager.
	static iris::PbrMaterialPtr createPbrMaterialFromShaderGraph(NodeGraph* graph);

	// Runs the shader-piece emitter over `graph` and lands the result on
	// `material` (HLMS_ADOPTION P5). Sets both piece paths unconditionally —
	// including to EMPTY — so a graph edited into something the emitter
	// refuses drops the piece it used to carry instead of rendering it
	// forever. Returns what the emitter did, including the per-socket reasons
	// for everything it left to the baker.
	static materials::PieceEmitter::Result applyEmittedPieces(NodeGraph* graph,
	                                                          iris::PbrMaterialPtr material);

	// Rebuilds the evaluated PBR material from a stored material definition
	// (the "pbrMaterial" object written by serialize). Returns null when the
	// definition predates Option B and carries no evaluated output.
	static iris::PbrMaterialPtr createPbrMaterialFromDefinition(QJsonObject matObj);

	// Maps a texture property's stored asset GUID to an image path via
	// TextureManager or the CAS; passes real file paths through untouched.
	// (`projectRoot`/`setProjectRoot` went with the project-folder bake: there
	// is no project-relative path left to resolve, so the one piece of
	// process-wide state in this class is gone.)
	static PbrGraphEvaluator::TextureResolver textureResolver();

	static NodeGraph* extractNodeGraphFromMaterialDefinition(QJsonObject matObj);

	// (generateShader/createMaterialFromShaderGraph/generateMaterialFrom-
	// MaterialDefinition died in MATERIALS_EVALUATOR phase 5 — the GLSL
	// pipeline is gone. Graph-backed definitions load through
	// createPbrMaterialFromDefinition.
	//
	// parseMaterialProperties/parseMaterialStates went with iris::CustomMaterial
	// at HLMS_ADOPTION P4b: they filled a CustomMaterial's property list and
	// render states from a `.shader` definition, and both had exactly one
	// caller — the ShaderHandler that also died there.)
};