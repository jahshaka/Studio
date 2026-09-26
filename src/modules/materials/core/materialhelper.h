#pragma once

#include <QJsonObject>
#include "pbrgraphevaluator.h"
#include "pieceemitter.h"
#include "services/assethome.h"

class GraphNodeScene;
class NodeGraph;
class NodeLibrary;

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
	//
	// HOW A FILE-NAMED IMAGE IS BOUND, and it is a decision with a device wait
	// on one side of it (PRESET-UNIFY-1 fix round): `Import` is the ordinary
	// route — the one content import, a library Texture row, pinned into the
	// open project, and therefore bytes written and an fsync on the calling
	// thread for a picture the store does not already hold. `PathOnly` binds
	// the shipped FILE to the node and writes NOTHING: no row, no pin, no
	// device wait. LOOKING at a material must never be a write, so every
	// READ-ONLY open takes PathOnly, and only a gesture that makes the user a
	// material of their own (a new material from a preset, Customise) imports.
	enum class TextureBinding {
		Import,
		PathOnly,
	};
	/// `home` is the graph's MATERIAL's home: an Import mints the pictures'
	/// rows there (ASSETS-SCOPE-1 F1).
	static int resolveAppRelativeTextures(NodeGraph* graph, TextureBinding binding,
	                                      const assethome::Home &home);

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

	/// ONE NODE LIBRARY FOR THE PROCESS. A NodeLibrary is a stateless
	/// registry of node factories (name -> icon, category, constructor);
	/// every graph the app opened used to build its own copy of it, and
	/// nothing ever freed one. Graphs borrow this; none owns a library.
	static NodeLibrary* sharedNodeLibrary();

	// The definition's graph, or NULL when this build refuses it — a material
	// written on the deleted "Surface Material" master (LEGACY-MASTER-CRUD).
	// `refusalReason`, when given, carries the one sentence to show the user.
	static NodeGraph* extractNodeGraphFromMaterialDefinition(QJsonObject matObj,
	                                                         QString* refusalReason = nullptr);

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