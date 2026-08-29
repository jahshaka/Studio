/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

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

void DepthNode::process(ModelContext* context)
{
	outSockets[0]->setVarName("(1.0 - gl_FragCoord.z/gl_FragCoord.w)");
	
}

QString DepthNode::generatePreview(ModelContext* context)
{
	auto output = "preview.color = vec4(vec3(1.0 - gl_FragCoord.z/gl_FragCoord.w), 1.0);";
	//auto output = "preview.color = vec4(vec3(1.0), 1.0);";
	return output;
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

void FresnelNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	ctx->addFunction("fresnel", R"(
vec4 fresnel(float power)
{
float f = 1.0 - max(0, dot(normalize(u_eyePos - v_worldPos), v_normal));
f = pow(f, power);
return vec4(f);
}
)");

	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	//auto code = res + " = vec4(1.0 - max(0, dot(normalize(u_eyePos - v_worldPos), v_normal)));";
	auto code = res + "= fresnel("+valB+");";
	ctx->addCodeChunk(this, code);
}

QString FresnelNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	ctx->addFunction("fresnel", R"(
vec4 fresnel(float power)
{
float f = 1.0 - max(0, dot(normalize(u_eyePos - v_worldPos), v_normal));
f = pow(f, power);
return vec4(f);
}
)");

	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);

	auto output = "preview.color = fresnel(" + valB + ");";
	return output;
}