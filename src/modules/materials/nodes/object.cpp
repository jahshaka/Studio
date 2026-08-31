/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "object.h"



DepthNode::DepthNode()
{
	setNodeType(NodeCategory::Object);
	title = "Depth";
	typeName = "depth";
	enablePreview = true;

	addOutputSocket(new FloatSocketModel("Depth"));
}



FresnelNode::FresnelNode()
{
	setNodeType(NodeCategory::Object);
	title = "Fresnel";
	typeName = "fresnel";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("Normal"));
	addInputSocket(new FloatSocketModel("Power", "1.0f"));
	addOutputSocket(new Vector4SocketModel("Result"));
}
