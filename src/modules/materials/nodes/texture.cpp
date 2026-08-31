/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "texture.h"

/*    COMBINE NORMAL    */
CombineNormalsNode::CombineNormalsNode()
{
	setNodeType(NodeCategory::Texture);
	title = "Combine Normals";
	typeName = "combinenormals";
	enablePreview = true;

	addInputSocket(new Vector3SocketModel("NormalA", "vec3(0.0, 0.0, 1.0)"));
	addInputSocket(new Vector3SocketModel("NormalB", "vec3(0.0, 0.0, 1.0)"));
	addOutputSocket(new Vector3SocketModel("Result"));
}



TexelSizeNode::TexelSizeNode()
{
	setNodeType(NodeCategory::Texture);
	title = "Texel Size";
	typeName = "texelsize";
	enablePreview = true;

	addInputSocket(new TextureSocketModel("Texture"));
	addOutputSocket(new Vector2SocketModel("Size"));
	addOutputSocket(new FloatSocketModel("Width"));
	addOutputSocket(new FloatSocketModel("Height"));
	addOutputSocket(new FloatSocketModel("1/Width"));
	addOutputSocket(new FloatSocketModel("1/Height"));
}


/*
SampleEquirectangularTextureNode::SampleEquirectangularTextureNode()
{
	setNodeType(NodeType::Math);
	title = "Sample Texture Equirectangular";
	typeName = "texelsize";
	enablePreview = true;

	addInputSocket(new TextureSocketModel("Texture"));
	addOutputSocket(new Vector2SocketModel("Size"));
	addOutputSocket(new FloatSocketModel("Width"));
	addOutputSocket(new FloatSocketModel("Height"));
	addOutputSocket(new FloatSocketModel("1/Width"));
	addOutputSocket(new FloatSocketModel("1/Height"));
}
*/


FlipbookUVAnimationNode::FlipbookUVAnimationNode()
{
	setNodeType(NodeCategory::Texture);
	title = "Flipbook Animation";
	typeName = "flipbook";
	enablePreview = true;

	addInputSocket(new Vector2SocketModel("UV", "v_texCoord"));
	addInputSocket(new FloatSocketModel("Rows", "1.0"));
	addInputSocket(new FloatSocketModel("Columns", "1.0"));
	addInputSocket(new FloatSocketModel("Animation Length", "2.0"));
	addInputSocket(new FloatSocketModel("Time", "u_time"));

	addOutputSocket(new Vector2SocketModel("UV"));
}

