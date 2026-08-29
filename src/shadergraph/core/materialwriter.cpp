/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "materialwriter.h"
//#include "nodemodel.h"
//#include "graphtest.h"
#include <QJsonObject>
#include <QJsonArray>
#include "../graph/nodegraph.h"
#include "../generator/shadergenerator.h"
#include "../models/nodemodel.h"
#include "pbrgraphevaluator.h"

QJsonObject MaterialWriter::serializeMaterial(NodeGraph* graph)
{
	QJsonObject matObj;

	QString blendType;
	switch (graph->settings.blendMode) {
	case BlendMode::Opaque:
		blendType = "Opaque";
		break;
	case BlendMode::Blend:
		blendType = "Blend";
		break;
	case BlendMode::Additive:
		blendType = "Blend";
	}

	QString cullMode;
	switch (graph->settings.cullMode) {
	case CullMode::Front:
		cullMode = "Front";
		break;
	case CullMode::Back:
		cullMode = "Back";
		break;
	case CullMode::None:
		cullMode = "None";
	}

	QString renderLayer;
	switch (graph->settings.renderLayer) {
	case RenderLayer::Opaque:
		renderLayer = "Opaque";
		break;
	case RenderLayer::AlphaTested:
		renderLayer = "AlphaTested";
		break;
	case RenderLayer::Transparent:
		renderLayer = "Transparent";
		break;
	case RenderLayer::Overlay:
		renderLayer = "Overlay";
		break;
	}

	matObj["name"] =				graph->settings.name;
    matObj["type"] =				"shadergraph";
	matObj["zWrite"] =				graph->settings.zwrite;
	matObj["depthTest"] =		    graph->settings.depthTest;
	matObj["blendMode"] =			blendType;
	matObj["cullMode"] =			cullMode;
	matObj["renderLayer"] =			renderLayer;
	matObj["fog"] =					graph->settings.fog;
	matObj["castShadows"] =			graph->settings.castShadow;
	matObj["receiveShadows"] =		graph->settings.receiveShadow;
	matObj["lighting"] =			graph->settings.acceptLighting;

	QJsonArray uniformArray;
	// convert properties to uniforms
	for (auto prop : graph->properties) {
		QJsonObject uniformObj;
		//uniformObj["name"] = prop->name;
		uniformObj["displayName"] = prop->displayName;
		uniformObj["name"] = prop->getUniformName();
		uniformObj["uniform"] = prop->getUniformName();

		switch (prop->type) {
		case PropertyType::Float:
			writeFloat((FloatProperty*)prop, uniformObj);
			uniformArray.append(uniformObj);
			break;
		// note: textures have a toggle value
		}
	}

	matObj["uniforms"] = uniformArray;

	// surface type comes from the master node: PbrMasterNode ("PbrMaterial")
	// or the legacy SurfaceMasterNode ("Material")
	auto masterNode = graph->getMasterNode();
	bool isPbr = masterNode && masterNode->typeName == "PbrMaterial";
	matObj["surfaceType"] = isPbr ? "pbr" : "surface";
	matObj["vertex_shader"] = ":assets/shaders/surface.vert";
	matObj["fragment_shader"] = "shader.frag";
	matObj["builtin"] = false;

	// Option B phase 1: the CPU-evaluated PbrMaterial inputs, alongside the
	// legacy GLSL description (see pbrgraphevaluator.h)
	auto evaluated = PbrGraphEvaluator::evaluate(graph);
	QJsonObject pbrObj;
	pbrObj["values"] = evaluated.values;
	pbrObj["unsupportedNodes"] = QJsonArray::fromStringList(evaluated.unsupportedNodes);
	matObj["pbrMaterial"] = pbrObj;

	// save shadergraph
	matObj["shadergraph"] = graph->serialize();
	//matObj["cached_fragment_shader"] = (new ShaderGenerator())->generateShader(graph);

	return matObj;
}

void MaterialWriter::writeFloat(FloatProperty* prop, QJsonObject& uniform)
{
	uniform["value"] = prop->value;
	uniform["min"] = prop->minValue;
	uniform["max"] = prop->maxValue;
	uniform["type"] = "float";
}
