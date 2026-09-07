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
// Socket ORDER is load-bearing: saved graphs reference sockets BY INDEX, and
// BakeProgram/GraphBaker key their master-slot table off this layout.
//
// It is therefore VERSIONED. Layout 1 had ten sockets with "Occlusion" at
// index 4; HLMS_ADOPTION P2 removed it (the renderer has no AO input at all)
// and layout 2 has nine. NodeGraph::serialize stamps the layout version and
// NodeGraph::deserialize migrates layout-1 files — shifting the indices above
// the removed socket and reporting any Occlusion connection it has to drop.
// The alternative, renumbering silently, would have re-pointed every saved
// Emissive/Alpha connection one slot up with no error anywhere.
//
// NOTE the tail no longer lines up with SurfaceMasterNode's (Vertex
// Offset/Extrusion sit at 7/8 here, 8/9 there). Nothing depends on the
// alignment: the baker matches master slots by socket NAME.
class PbrMasterNode : public NodeModel
{
public:
	PbrMasterNode();
};
