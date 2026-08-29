#pragma once

#include <QJsonObject>
#include "irisgl/src/materials/custommaterial.h"
#include "pbrgraphevaluator.h"

class GraphNodeScene;
class NodeGraph;

class MaterialHelper
{
public:
	static bool materialHasEffect(QJsonObject matObj);

	static QString assetPath(QString relPath);

	// generates vertex and fragment shader form nodegraph
	// returns true if all goes well, false if otherwose
	static bool generateShader(NodeGraph* graph, QString& vertexShader, QString& fragmentShader);
	
	// Converts a NodeGraph to the Material json format.
	// Since Option B phase 1 the result also carries "pbrMaterial", the
	// CPU-evaluated iris::PbrMaterial inputs of the graph (values + the list
	// of unsupported inputs) - see PbrGraphEvaluator.
	static QJsonObject serialize(NodeGraph* graph);
	static iris::CustomMaterialPtr createMaterialFromShaderGraph(NodeGraph* scene);

	// Option B phase 1: the graph evaluated to the document's PBR material
	// (which SceneMirror already mirrors into the engine). Texture-property
	// GUIDs are resolved through TextureManager.
	static iris::PbrMaterialPtr createPbrMaterialFromShaderGraph(NodeGraph* graph);

	// Rebuilds the evaluated PBR material from a stored material definition
	// (the "pbrMaterial" object written by serialize). Returns null when the
	// definition predates Option B and carries no evaluated output.
	static iris::PbrMaterialPtr createPbrMaterialFromDefinition(QJsonObject matObj);

	// Maps a texture property's stored asset GUID to an image path via
	// TextureManager; passes real file paths through untouched.
	static PbrGraphEvaluator::TextureResolver textureResolver();

	static NodeGraph* extractNodeGraphFromMaterialDefinition(QJsonObject matObj);

	// uses the vertexShaderSource and fragmentShaderSource by default
	// if generateFromGraph is set to true, it generates it from the shadergraph
	// if there's an error generating the code then a null material is returned
	static iris::CustomMaterialPtr generateMaterialFromMaterialDefinition(QJsonObject matObj, bool generateFromGraph = false);
	//static QJsonObject serialize(GraphNodeScene* scene);

	static void parseMaterialProperties(iris::CustomMaterialPtr material, QJsonArray propList);
	//static QJsonArray serializeMateriaProperties(iris::)

	static void parseMaterialStates(iris::CustomMaterialPtr material, QJsonObject matObj);


	static QString vertexShaderTemplate;
	static QString fragmentShaderTemplate;
};