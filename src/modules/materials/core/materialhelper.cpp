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
#include "graphbaker.h"
#include "pbrgraphevaluator.h"
#include "texturemanager.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include <QFileInfo>
#include <QJsonObject>
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "../models/libraryv1.h"
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

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

	materials::GraphBaker::Options opts;
	opts.resolution = graph->settings.bakeResolution;
	opts.outputDir = projectRoot + "/BakedMaps/" + bakeGuid;
	opts.relativePrefix = "BakedMaps/" + bakeGuid + "/";
	const auto baked = materials::GraphBaker::run(graph, opts, textureResolver());

	QJsonObject pbrObj = matObj["pbrMaterial"].toObject();
	pbrObj["values"] = baked.eval.values;
	pbrObj["unsupportedNodes"] = QJsonArray::fromStringList(baked.eval.unsupportedNodes);
	pbrObj["approximatedNodes"] = QJsonArray::fromStringList(baked.eval.approximatedNodes);
	pbrObj["animated"] = baked.eval.animated;
	pbrObj["bakedMaps"] = baked.maps;
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
	return PbrGraphEvaluator::createMaterial(graph, textureResolver());
}

iris::PbrMaterialPtr MaterialHelper::createPbrMaterialFromDefinition(QJsonObject matObj)
{
	if (!matObj.contains("pbrMaterial"))
		return iris::PbrMaterialPtr();

	auto values = matObj["pbrMaterial"].toObject()["values"].toObject();
	return PbrGraphEvaluator::materialFromValues(values, textureResolver());
}

NodeGraph* MaterialHelper::extractNodeGraphFromMaterialDefinition(QJsonObject matObj)
{
	auto graphObj = matObj["shadergraph"].toObject();
	auto graph = NodeGraph::deserialize(graphObj, new LibraryV1());

	return graph;
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
