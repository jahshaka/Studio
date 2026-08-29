/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "pbrmasternode.h"

#include "../graph/sockets.h"
#include "../generator/shadercontext.h"

PbrMasterNode::PbrMasterNode()
{
	title = "PBR Material";
	typeName = "PbrMaterial";
	setNodeType(NodeCategory::Input);

	// Indices 0-7: the PBR surface. Indices 8/9 MUST stay Vertex
	// Offset/Extrusion (see header).
	addInputSocket(new Vector3SocketModel("Base Color", "vec3(1.0,1.0,1.0)")); // 0
	addInputSocket(new FloatSocketModel("Metallic", "0.0"));                   // 1
	addInputSocket(new FloatSocketModel("Roughness", "0.5"));                  // 2
	addInputSocket(new Vector3SocketModel("Normal", "vec3(0.0, 0.0, 1.0)"));   // 3
	addInputSocket(new FloatSocketModel("Occlusion", "1.0"));                  // 4
	addInputSocket(new Vector3SocketModel("Emissive", "vec3(0.0,0.0,0.0)"));   // 5
	addInputSocket(new FloatSocketModel("Alpha", "1.0"));                      // 6
	addInputSocket(new FloatSocketModel("Alpha Cutoff"));                      // 7
	addInputSocket(new Vector3SocketModel("Vertex Offset"));                   // 8
	addInputSocket(new FloatSocketModel("Vertex Extrusion"));                  // 9
}

void PbrMasterNode::process(ModelContext* ctx)
{
	auto context = (ShaderContext*)ctx;

	auto baseColorVar = this->getValueFromInputSocket(0);
	auto metallicVar = this->getValueFromInputSocket(1);
	auto roughnessVar = this->getValueFromInputSocket(2);
	auto normalVar = this->getValueFromInputSocket(3);
	auto occlusionVar = this->getValueFromInputSocket(4);
	auto emissiveVar = this->getValueFromInputSocket(5);
	auto alphaVar = this->getValueFromInputSocket(6);
	auto alphaCutoffVar = this->getValueFromInputSocket(7);

	// Approximate metallic-roughness on the legacy Blinn-Phong Material
	// struct so the existing surface.frag template still previews the graph.
	QString code = "";
	code += "material.diffuse = " + baseColorVar + " * (1.0 - " + metallicVar + ") * " + occlusionVar + ";\n";
	code += "material.specular = mix(vec3(0.04), " + baseColorVar + ", " + metallicVar + ");\n";
	code += "material.shininess = (1.0 - " + roughnessVar + ") * 100.0;\n";
	code += "material.normal = " + normalVar + ";\n";
	code += "material.ambient = vec3(0.0);\n";
	code += "material.emission = " + emissiveVar + ";\n";
	code += "material.alpha = " + alphaVar + ";\n";
	code += "material.alphaCutoff = " + alphaCutoffVar + ";\n";

	context->addCodeChunk(this, code);
}
