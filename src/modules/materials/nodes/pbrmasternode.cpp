/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "pbrmasternode.h"

#include "../graph/sockets.h"

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

