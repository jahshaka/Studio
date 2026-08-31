/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "vector.h"

/*    REFLECT    */
ReflectVectorNode::ReflectVectorNode()
{
	setNodeType(NodeCategory::Vector);
	title = "Reflect";
	typeName = "reflect";
	enablePreview = true;

	addInputSocket(new Vector3SocketModel("Normal"));
	addInputSocket(new Vector3SocketModel("Incident"));
	addOutputSocket(new Vector3SocketModel("Result"));
}



/* SPLIT VECTOR */

SplitVectorNode::SplitVectorNode()
{
	setNodeType(NodeCategory::Vector);
	title = "Split Vector";
	typeName = "splitvector";

	addInputSocket(new Vector4SocketModel("Vector"));
	addOutputSocket(new FloatSocketModel("X"));
	addOutputSocket(new FloatSocketModel("Y"));
	addOutputSocket(new FloatSocketModel("Z"));
	addOutputSocket(new FloatSocketModel("W"));
}


/* COMPOSE VECTOR */

ComposeVectorNode::ComposeVectorNode()
{
	setNodeType(NodeCategory::Vector);
	title = "Compose Vector";
	typeName = "composevector";

	addInputSocket(new FloatSocketModel("X"));
	addInputSocket(new FloatSocketModel("Y"));
	addInputSocket(new FloatSocketModel("Z"));
	addInputSocket(new FloatSocketModel("W"));
	// the emitted expression is vec4(x,y,z,w); the socket used to say vec3,
	// which silently rewired W as Z through coercion (audit D11)
	addOutputSocket(new Vector4SocketModel("Vector"));
}


/* DISTANCE */

DistanceVectorNode::DistanceVectorNode()
{
	setNodeType(NodeCategory::Vector);
	title = "Distance";
	typeName = "distance";
	//enablePreview = true;

	// distance(p0, p1) takes two points; the old single-input emission
	// produced GLSL that could not compile
	addInputSocket(new Vector4SocketModel("Vector A"));
	addInputSocket(new Vector4SocketModel("Vector B"));
	addOutputSocket(new FloatSocketModel("Result"));
}


/* DOT PRODUCT */

DotVectorNode::DotVectorNode()
{
	setNodeType(NodeCategory::Vector);
	title = "Dot";
	typeName = "dot";

	addInputSocket(new Vector4SocketModel("VectorA"));
	addInputSocket(new Vector4SocketModel("VectorB"));
	addOutputSocket(new FloatSocketModel("Result"));
}


/* LENGTH */

LengthVectorNode::LengthVectorNode()
{
	setNodeType(NodeCategory::Vector);
	title = "Length";
	typeName = "length";

	addInputSocket(new Vector4SocketModel("Vector"));
	addOutputSocket(new FloatSocketModel("Result"));
}


/* NORMALIZE */

NormalizeVectorNode::NormalizeVectorNode()
{
	setNodeType(NodeCategory::Vector);
	title = "Normalize";
	typeName = "normalize";

	addInputSocket(new Vector4SocketModel("Vector"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

