/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "materialhelper.h"
#include "../graph/graphnodescene.h"
#include "../graph/nodegraph.h"
#include "../generator/shadergenerator.h"
#include "pbrgraphevaluator.h"
#include "texturemanager.h"
#include <QFileInfo>
#include <QJsonObject>
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/assets/shader.h"
#include "../models/libraryv1.h"
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

/*
EFFECT SHADER FORMAT
===============
{
	version:2,
	name:"",
	type:"shader", //shader, fx, postprocess

	vertexShaderSource:"",
	fragmentShaderSource:"",

	// if custom shadows
	shadowVertexShaderSource:"",
	shadowFragmentShaderSource:"",

	// effect
	shadergraph:{

	},

	values : [

	],

	properties : {

	},

	states : {

	}
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

bool MaterialHelper::generateShader(NodeGraph * graph, QString & vertexShader, QString & fragmentShader)
{
	//todo: change to proper paths
	auto vertTemplate = iris::GraphicsHelper::loadAndProcessShader(assetPath("surface.vert"));
	auto fragTemplate = iris::GraphicsHelper::loadAndProcessShader(assetPath("surface.frag"));

	ShaderGenerator shaderGen;
	shaderGen.generateShader(graph);
	auto vertCode = shaderGen.getVertexShader();
	auto fragCode = shaderGen.getFragmentShader();

	vertexShader = vertTemplate + vertCode;
	fragmentShader = fragTemplate + fragCode;

	return true;
}

QJsonObject MaterialHelper::serialize(NodeGraph* graph)
{
	QJsonObject matObj;
	matObj["name"] = graph->settings.name;
	matObj["version"] = 2.0;
	matObj["type"] = "effect";
	matObj["shaderGuid"] = "";


	// GENERATE SHADERS
	QString vertCode, fragCode;
	generateShader(graph, vertCode, fragCode);
	matObj["shadergraph"] = graph->serialize();

	matObj["vertexShaderSource"] = vertCode;
	matObj["fragmentShaderSource"] = fragCode;

	// todo: if vertex offset or alpha was connected to, generate shadow shader

	// properties are the same as the ones in the shadergraph, they're
	// just placed here for convenience
	matObj["properties"] = matObj["shadergraph"].toObject()["properties"];

	matObj["states"] = matObj["shadergraph"].toObject()["settings"];

	// Option B phase 1: evaluated PBR output alongside the generated GLSL.
	// The GLSL keeps the legacy viewport working; "pbrMaterial" is what the
	// engine-backed editor consumes (see pbrgraphevaluator.h).
	auto evaluated = PbrGraphEvaluator::evaluate(graph, textureResolver());
	QJsonObject pbrObj;
	pbrObj["values"] = evaluated.values;
	pbrObj["unsupportedNodes"] = QJsonArray::fromStringList(evaluated.unsupportedNodes);
	pbrObj["surfaceType"] = evaluated.hasPbrMaster ? "pbr" : "surface";
	matObj["pbrMaterial"] = pbrObj;

	return matObj;
}

PbrGraphEvaluator::TextureResolver MaterialHelper::textureResolver()
{
	return [](const QString& value) -> QString {
		if (value.isEmpty() || QFileInfo::exists(value))
			return value;
		// treat as an asset GUID already loaded by the graph's TextureManager
		for (auto tex : TextureManager::getSingleton()->textures) {
			if (tex->guid == value)
				return tex->path;
		}
		return value;
	};
}

iris::PbrMaterialPtr MaterialHelper::createPbrMaterialFromShaderGraph(NodeGraph* graph)
{
	return PbrGraphEvaluator::createMaterial(graph, textureResolver());
}

iris::PbrMaterialPtr MaterialHelper::createPbrMaterialFromDefinition(QJsonObject matObj)
{
	if (!matObj.contains("pbrMaterial"))
		return iris::PbrMaterialPtr();

	auto values = matObj["pbrMaterial"].toObject()["values"].toObject();
	return PbrGraphEvaluator::materialFromValues(values);
}

iris::CustomMaterialPtr MaterialHelper::createMaterialFromShaderGraph(NodeGraph* graph)
{
	auto matObj = serialize(graph);

	auto shader = iris::Shader::create();
	shader->setVertexShader(matObj["vertexShaderSource"].toString());
	shader->setFragmentShader(matObj["fragmentShaderSource"].toString());

	auto mat = iris::CustomMaterial::createFromShader(shader);
	mat->setMaterialDefinition(matObj);

	return mat;
}

NodeGraph* MaterialHelper::extractNodeGraphFromMaterialDefinition(QJsonObject matObj)
{
	auto graphObj = matObj["shadergraph"].toObject();
	auto graph = NodeGraph::deserialize(graphObj, new LibraryV1());

	return graph;
}

iris::CustomMaterialPtr MaterialHelper::generateMaterialFromMaterialDefinition(QJsonObject matObj, bool generateFromGraph)
{
	//auto nodeGraph = extractNodeGraphFromMaterialDefinition(matObj);

	auto shader = iris::Shader::create();

	if (generateFromGraph) {
		ShaderGenerator shaderGen;
		auto graph = extractNodeGraphFromMaterialDefinition(matObj);
		
		QString vertCode, fragCode;
		generateShader(graph, vertCode, fragCode);

		// append to shader templates
		shader->setVertexShader(vertCode);
		shader->setFragmentShader(fragCode);
	}
	else {
		auto vertShader = matObj["vertexShaderSource"].toString();
		auto fragShader = matObj["fragmentShaderSource"].toString();
		shader->setVertexShader(vertShader);
		shader->setFragmentShader(fragShader);
	}
	

	auto mat = iris::CustomMaterial::createFromShader(shader);
	mat->setMaterialDefinition(matObj);
	mat->setVersion(2);

	// generate properties
	parseMaterialProperties(mat, matObj["properties"].toArray());
	parseMaterialStates(mat, matObj);

	return mat;
}

void MaterialHelper::parseMaterialProperties(iris::CustomMaterialPtr material, QJsonArray propList)
{
	for (int i = 0; i < propList.size(); i++) {
		auto prop = propList[i].toObject();
		auto displayName = prop["displayName"].toString();
		auto name = prop["name"].toString();
		auto uniform = prop["uniform"].toString();

		if (prop["type"] == "float") {
			auto fltProp = new iris::FloatProperty;
			fltProp->id = i;
			fltProp->displayName = displayName;
			fltProp->name = name;
			fltProp->minValue = prop["minValue"].toDouble();
			fltProp->maxValue = prop["maxValue"].toDouble();
			fltProp->uniform = uniform;
			fltProp->value = prop["value"].toDouble();

			material->properties.append(fltProp);
		}

		if (prop["type"] == "int") {
			auto fltProp = new iris::IntProperty;
			fltProp->id = i;
			fltProp->displayName = displayName;
			fltProp->name = name;
			fltProp->minValue = prop["minValue"].toDouble();
			fltProp->maxValue = prop["maxValue"].toDouble();
			fltProp->uniform = uniform;
			fltProp->value = prop["value"].toDouble();

			material->properties.append(fltProp);
		}

		if (prop["type"] == "bool") {
			auto blProp = new iris::BoolProperty;
			blProp->id = i;
			blProp->displayName = displayName;
			blProp->name = name;
			blProp->uniform = uniform;
			blProp->value = prop["value"].toBool();

			material->properties.append(blProp);
		}

		if (prop["type"] == "texture") {
			auto texProp = new iris::TextureProperty;
			texProp->id = i;
			texProp->displayName = displayName;
			texProp->name = name;
			texProp->uniform = uniform;
			texProp->toggleValue = prop["toggle"].toString();
			texProp->value = prop["value"].toString();

			material->properties.append(texProp);
		}

		if (prop["type"] == "color") {
			auto clrProp = new iris::ColorProperty;
			clrProp->id = i;
			clrProp->displayName = displayName;
			clrProp->name = name;
			clrProp->uniform = uniform;

			QColor col;
			col.setNamedColor(prop["value"].toString());
			clrProp->value = col;

			material->properties.append(clrProp);
		}

		if (prop["type"] == "vec2") {
			auto vecProp = new iris::Vec2Property;
			vecProp->id = i;
			vecProp->displayName = displayName;
			vecProp->name = name;
			vecProp->uniform = uniform;

			auto valObj = prop["value"].toObject();
			vecProp->value = QVector2D(valObj["x"].toDouble(), valObj["y"].toDouble());

			material->properties.append(vecProp);
		}

		if (prop["type"] == "vec3") {
			auto vecProp = new iris::Vec3Property;
			vecProp->id = i;
			vecProp->displayName = displayName;
			vecProp->name = name;
			vecProp->uniform = uniform;

			auto valObj = prop["value"].toObject();
			vecProp->value = QVector3D(valObj["x"].toDouble(), valObj["y"].toDouble(), valObj["z"].toDouble());

			material->properties.append(vecProp);
		}

		if (prop["type"] == "vec4") {
			auto vecProp = new iris::Vec4Property;
			vecProp->id = i;
			vecProp->displayName = displayName;
			vecProp->name = name;
			vecProp->uniform = uniform;

			auto valObj = prop["value"].toObject();
			vecProp->value = QVector4D(valObj["x"].toDouble(), valObj["y"].toDouble(), valObj["z"].toDouble(), valObj["w"].toDouble());

			material->properties.append(vecProp);
		}
	}

}

void MaterialHelper::parseMaterialStates(iris::CustomMaterialPtr material, QJsonObject matObj)
{
	if (matObj.contains("states")) {
		auto statesObject = matObj["states"].toObject();
		material->setBaseMaterialProperties(statesObject);
	}
}
