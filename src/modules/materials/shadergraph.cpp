/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "shadergraph.h"
#include "graph/nodegraph.h"
#include "models/nodemodel.h"
#include "nodes/test.h"
#include "nodes/pbrmasternode.h"


// Every graph is a PBR graph: the master is PbrMasterNode, the only master
// there is. A file saved with the deleted Blinn-Phong master is REFUSED on
// load (NodeGraph::deserialize) — there are no legacy graphs to convert.
ShaderGraph* ShaderGraph::createDefaultShaderGraph()
{
	return createPBRShaderGraph();
}

// to be implemented
ShaderGraph* ShaderGraph::createParticleShaderGraph()
{
	return nullptr;
}

ShaderGraph* ShaderGraph::createPBRShaderGraph()
{
	auto nodeGraph = new ShaderGraph();
	auto masterNode = new PbrMasterNode();
	nodeGraph->addNode(masterNode);
	nodeGraph->setMasterNode(masterNode);

	return nodeGraph;
}
