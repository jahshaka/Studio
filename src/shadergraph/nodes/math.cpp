/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "math.h"

/*    ADD    */
AddNode::AddNode()
{
	setNodeType(NodeCategory::Math);
	title = "Add";
	typeName = "add";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addInputSocket(new Vector4SocketModel("B"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void AddNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = " + valA + " + " + valB + ";";
	ctx->addCodeChunk(this, code);
}

QString AddNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);

	auto output = "preview.color = " + valA + " + " + valB + ";";
	return output;
}

/*    SUBTRACT    */
SubtractNode::SubtractNode()
{
	setNodeType(NodeCategory::Math);
	title = "Subtract";
	typeName = "subtract";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addInputSocket(new Vector4SocketModel("B"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void SubtractNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = " + valA + " - " + valB + ";";
	ctx->addCodeChunk(this, code);
}

QString SubtractNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);

	auto output = "preview.color = " + valA + " - " + valB + ";";
	return output;
}

/*    MULTIPLY    */
MultiplyNode::MultiplyNode()
{
	setNodeType(NodeCategory::Math);
	title = "Multiply";
	typeName = "multiply";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addInputSocket(new Vector4SocketModel("B"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void MultiplyNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = " + valA + " * " + valB + ";";
	ctx->addCodeChunk(this, code);
}

QString MultiplyNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);

	auto output = "preview.color = " + valA + " * " + valB + ";";
	return output;
}

/*    DIVIDE    */
DivideNode::DivideNode()
{
	setNodeType(NodeCategory::Math);
	title = "Divide";
	typeName = "divide";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addInputSocket(new Vector4SocketModel("B"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void DivideNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = " + valA + " / " + valB + ";";
	ctx->addCodeChunk(this, code);
}

QString DivideNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);

	auto output = "preview.color = " + valA + " / " + valB + ";";
	return output;
}


/*    POWER    */
PowerNode::PowerNode()
{
	setNodeType(NodeCategory::Math);
	title = "Power";
	typeName = "power";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addInputSocket(new Vector4SocketModel("B"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void PowerNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = pow(" + valA + " , " + valB + ");";
	ctx->addCodeChunk(this, code);
}

QString PowerNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);

	auto output = "preview.color = pow(" + valA + " , " + valB + ");";
	return output;
}

/*    SQUARE ROOT    */
SqrtNode::SqrtNode()
{
	setNodeType(NodeCategory::Math);
	title = "Square Root";
	typeName = "sqrt";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void SqrtNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = sqrt(" + valA + ");";
	ctx->addCodeChunk(this, code);
}

QString SqrtNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);

	auto output = "preview.color = sqrt(" + valA + ");";
	return output;
}

/*    MIN    */
MinNode::MinNode()
{
	setNodeType(NodeCategory::Math);
	title = "Min";
	typeName = "min";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addInputSocket(new Vector4SocketModel("B"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void MinNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = min(" + valA + " , " + valB + ");";
	ctx->addCodeChunk(this, code);
}

QString MinNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);

	auto output = "preview.color = min(" + valA + " , " + valB + ");";
	return output;
}

/*    MAX    */
MaxNode::MaxNode()
{
	setNodeType(NodeCategory::Math);
	title = "Max";
	typeName = "max";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addInputSocket(new Vector4SocketModel("B"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void MaxNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = max(" + valA + " , " + valB + ");";
	ctx->addCodeChunk(this, code);
}
QString MaxNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);

	auto output = "preview.color = max(" + valA + " , " + valB + ");";
	return output;
}


/*    ABS    */
AbsNode::AbsNode()
{
	setNodeType(NodeCategory::Math);
	title = "Abs";
	typeName = "abs";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void AbsNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = abs(" + valA + ");";
	ctx->addCodeChunk(this, code);
}

QString AbsNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);

	auto output = "preview.color = abs(" + valA + ");";
	return output;
}

/*    SIGN    */
SignNode::SignNode()
{
	setNodeType(NodeCategory::Math);
	title = "Sign";
	typeName = "sign";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void SignNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = sign(" + valA + ");";
	ctx->addCodeChunk(this, code);
}

QString SignNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);

	auto output = "preview.color = sign(" + valA + ");";
	return output;
}

/*    CEIL    */
CeilNode::CeilNode()
{
	setNodeType(NodeCategory::Math);
	title = "Ceil";
	typeName = "ceil";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void CeilNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = ceil(" + valA + ");";
	ctx->addCodeChunk(this, code);
}

QString CeilNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);

	auto output = "preview.color = ceil(" + valA + ");";
	return output;
}

/*    FLOOR    */
FloorNode::FloorNode()
{
	setNodeType(NodeCategory::Math);
	title = "Floor";
	typeName = "floor";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void FloorNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = floor(" + valA + ");";
	ctx->addCodeChunk(this, code);
}

QString FloorNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);

	auto output = "preview.color = floor(" + valA + ");";
	return output;
}

/*    ROUND    */
RoundNode::RoundNode()
{
	setNodeType(NodeCategory::Math);
	title = "Round";
	typeName = "round";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

// https://forum.unity.com/threads/round-not-supported-in-shaders.144316/
void RoundNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = round(" + valA + ");";
	ctx->addCodeChunk(this, code);
}

QString RoundNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);

	auto output = "preview.color = round(" + valA + ");";
	return output;
}

/*    TRUNCATE    */
TruncNode::TruncNode()
{
	setNodeType(NodeCategory::Math);
	title = "Truncate";
	typeName = "truncate";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

// https://stackoverflow.com/questions/14/difference-between-math-floor-and-math-truncate
void TruncNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = trunc(" + valA + ");";
	ctx->addCodeChunk(this, code);
}

QString TruncNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);

	auto output = "preview.color = trunc(" + valA + ");";
	return output;
}

/*    STEP    */
StepNode::StepNode()
{
	setNodeType(NodeCategory::Math);
	title = "Step";
	typeName = "step";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("Edge"));
	addInputSocket(new Vector4SocketModel("Value"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void StepNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto edge = this->getValueFromInputSocket(0);
	auto val = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = step(" + edge + " , " + val + ");";
	ctx->addCodeChunk(this, code);
}

QString StepNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto edge = this->getValueFromInputSocket(0);
	auto val = this->getValueFromInputSocket(1);

	auto output = "preview.color = step(" + edge + " , " + val + ");";
	return output;
}

/*    SMOOTHSTEP    */
SmoothStepNode::SmoothStepNode()
{
	setNodeType(NodeCategory::Math);
	title = "SmoothStep";
	typeName = "smoothstep";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("Edge1"));
	addInputSocket(new Vector4SocketModel("Edge2"));
	addInputSocket(new Vector4SocketModel("Value"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void SmoothStepNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto edge1 = this->getValueFromInputSocket(0);
	auto edge2 = this->getValueFromInputSocket(1);
	auto val = this->getValueFromInputSocket(2);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = smoothstep(" + edge1 + " , " + edge2 + " , " + val + ");";
	ctx->addCodeChunk(this, code);
}

QString SmoothStepNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto edge1 = this->getValueFromInputSocket(0);
	auto edge2 = this->getValueFromInputSocket(1);
	auto val = this->getValueFromInputSocket(2);

	auto output = "preview.color = smoothstep(" + edge1 + " , " + edge2 + " , " + val + ");";
	return output;
}

/*    FRACTION    */
FracNode::FracNode()
{
	setNodeType(NodeCategory::Math);
	title = "Fraction";
	typeName = "fraction";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void FracNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = fract(" + valA + ");";
	ctx->addCodeChunk(this, code);
}

QString FracNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);

	auto output = "preview.color = fract(" + valA + ");";
	return output;
}

/*    CLAMP    */
ClampNode::ClampNode()
{
	setNodeType(NodeCategory::Math);
	title = "Clamp";
	typeName = "clamp";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("Min"));
	addInputSocket(new Vector4SocketModel("Max"));
	addInputSocket(new Vector4SocketModel("Value"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void ClampNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto minVal = this->getValueFromInputSocket(0);
	auto maxVal = this->getValueFromInputSocket(1);
	auto val = this->getValueFromInputSocket(2);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = clamp(" + minVal + " , " + maxVal + " , " + val + ");";
	ctx->addCodeChunk(this, code);
}

QString ClampNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto minVal = this->getValueFromInputSocket(0);
	auto maxVal = this->getValueFromInputSocket(1);
	auto val = this->getValueFromInputSocket(2);

	auto output = "preview.color = clamp(" + minVal + " , " + maxVal + " , " + val + ");";
	return output;
}

/*    LERP    */
LerpNode::LerpNode()
{
	setNodeType(NodeCategory::Math);
	title = "Lerp";
	typeName = "lerp";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addInputSocket(new Vector4SocketModel("B"));
	addInputSocket(new FloatSocketModel("T"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void LerpNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);
	auto t = this->getValueFromInputSocket(2);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = mix(" + valA + " , " + valB + " , " + t + ");";
	//auto code = res + " = mix(vec3(1.0,0.0,0.0).xyzz,vec3(0.0,1.0,1.0).xyzz, " + t + ");";
	ctx->addCodeChunk(this, code);
}

QString LerpNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);
	auto t = this->getValueFromInputSocket(2);

	auto output = "preview.color = mix(" + valA + " , " + valB + " , " + t + ");";
	return output;
}

/*    ONEMINUS    */
OneMinusNode::OneMinusNode()
{
	setNodeType(NodeCategory::Math);
	title = "One Minus";
	typeName = "oneminus";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void OneMinusNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = vec4(1.0) - " + valA + ";";
	ctx->addCodeChunk(this, code);
}

QString OneMinusNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);

	auto output = "preview.color = vec4(1.0) - " + valA + ";";
	return output;
}

/*    NEGATE    */
NegateNode::NegateNode()
{
	setNodeType(NodeCategory::Math);
	title = "Negate";
	typeName = "negate";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

void NegateNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = -" + valA + ";";
	ctx->addCodeChunk(this, code);
}

QString NegateNode::generatePreview(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);

	auto output = "preview.color = -" + valA + ";";
	return output;
}