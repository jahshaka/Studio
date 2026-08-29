/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

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

void ReflectVectorNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto normal = this->getValueFromInputSocket(0);
	auto incident = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = reflect(" + incident + " , " + normal + ");";
	ctx->addCodeChunk(this, code);
}

QString ReflectVectorNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto normal = this->getValueFromInputSocket(0);
	auto incident = this->getValueFromInputSocket(1);

	auto output = "preview.color = reflect(" + incident + " , " + normal + ");";
	return output;
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

void SplitVectorNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto vec = this->getValueFromInputSocket(0);
	this->outSockets[0]->varName = vec + ".x";
	this->outSockets[1]->varName = vec + ".y";
	this->outSockets[2]->varName = vec + ".z";
	this->outSockets[3]->varName = vec + ".w";
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
	addOutputSocket(new Vector3SocketModel("Vector"));
}

void ComposeVectorNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto x = this->getValueFromInputSocket(0);
	auto y = this->getValueFromInputSocket(1);
	auto z = this->getValueFromInputSocket(2);
	auto w = this->getValueFromInputSocket(3);

	this->outSockets[0]->varName = "vec4("+x+","+y+","+z+","+w+")";
}


/* DISTANCE */

DistanceVectorNode::DistanceVectorNode()
{
	setNodeType(NodeCategory::Vector);
	title = "Distance";
	typeName = "distance";
	//enablePreview = true;

	addInputSocket(new Vector4SocketModel("Vector"));
	addOutputSocket(new FloatSocketModel("Result"));
}

void DistanceVectorNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto vec = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = distance(" + vec + ");";
	ctx->addCodeChunk(this, code);
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

void DotVectorNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto vec1 = this->getValueFromInputSocket(0);
	auto vec2 = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = dot(" + vec1 + "," + vec2 + ");";
	ctx->addCodeChunk(this, code);
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

void LengthVectorNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto vec1 = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = length(" + vec1 + ");";
	ctx->addCodeChunk(this, code);
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

void NormalizeVectorNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto vec1 = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = normalize(" + vec1 + ");";
	ctx->addCodeChunk(this, code);
}