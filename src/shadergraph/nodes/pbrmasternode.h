/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#pragma once

#include "../models/nodemodel.h"

// PBR master node (Option B, PBR_SPEC.md section 5): the graph's output sockets
// are HlmsPbs-compatible iris::PbrMaterial inputs instead of Blinn-Phong ones.
//
// Socket ORDER is load-bearing: ShaderGenerator reads Vertex Offset/Extrusion
// by hard-coded indices 8/9 (generator/shadergenerator.cpp), which is why this
// node keeps ten sockets in the same tail layout as SurfaceMasterNode.
class PbrMasterNode : public NodeModel
{
public:
	PbrMasterNode();

	// Emits a legacy-GLSL approximation of the PBR inputs so the old
	// generator (and with it the legacy viewport preview) keeps working.
	// The real PBR output is produced CPU-side by PbrGraphEvaluator.
	virtual void process(ModelContext* ctx) override;
};
