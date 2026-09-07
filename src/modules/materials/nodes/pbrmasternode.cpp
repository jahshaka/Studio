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

	// SOCKET LAYOUT 2 (HLMS_ADOPTION P2). Layout 1 carried an "Occlusion"
	// socket at index 4 that fed a per-texel bake nothing ever rendered; it is
	// gone, and every later socket moved down one. Saved graphs reference
	// sockets BY INDEX, so NodeGraph::deserialize migrates layout-1 files —
	// see kSocketLayoutVersion there. Do not renumber again without doing the
	// same.
	addInputSocket(new Vector3SocketModel("Base Color", "vec3(1.0,1.0,1.0)")); // 0
	addInputSocket(new FloatSocketModel("Metallic", "0.0"));                   // 1
	addInputSocket(new FloatSocketModel("Roughness", "0.5"));                  // 2
	addInputSocket(new Vector3SocketModel("Normal", "vec3(0.0, 0.0, 1.0)"));   // 3
	addInputSocket(new Vector3SocketModel("Emissive", "vec3(0.0,0.0,0.0)"));   // 4
	addInputSocket(new FloatSocketModel("Alpha", "1.0"));                      // 5
	addInputSocket(new FloatSocketModel("Alpha Cutoff"));                      // 6
	addInputSocket(new Vector3SocketModel("Vertex Offset"));                   // 7
	addInputSocket(new FloatSocketModel("Vertex Extrusion"));                  // 8
}

