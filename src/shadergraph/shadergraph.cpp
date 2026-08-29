/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "shadergraph.h"
#include "graph/nodegraph.h"
#include "models/nodemodel.h"
#include "nodes/test.h"
#include "nodes/pbrmasternode.h"


// New graphs author PBR (Option B): the default master is PbrMasterNode.
// Legacy graphs deserialized with a SurfaceMasterNode keep working - see
// NodeGraph::deserialize.
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
