/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#pragma once

#include "../models/nodemodel.h"

// PBR master node (Option B, PBR_SPEC.md section 5): the graph's output sockets
// are HlmsPbs-compatible iris::PbrMaterial inputs instead of Blinn-Phong ones.
//
// Socket ORDER is load-bearing: saved graphs reference sockets by index, and
// BakeProgram/GraphBaker key their master-slot table off this layout — the
// node keeps ten sockets in the same tail layout as SurfaceMasterNode
// (Vertex Offset/Extrusion at 8/9, honestly-unsupported).
class PbrMasterNode : public NodeModel
{
public:
	PbrMasterNode();
};
