/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

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


/*    TRUNCATE    */
TruncNode::TruncNode()
{
	setNodeType(NodeCategory::Math);
	title = "Truncate";
	typeName = "trunc"; // was "truncate" — the library key is "trunc"; the mismatch made saved Truncate nodes unloadable
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("A"));
	addOutputSocket(new Vector4SocketModel("Result"));
}

// https://stackoverflow.com/questions/14/difference-between-math-floor-and-math-truncate


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



/*    SMOOTHSTEP    */
SmoothStepNode::SmoothStepNode()
{
	setNodeType(NodeCategory::Math);
	title = "Smooth Step";
	typeName = "smoothstep";
	enablePreview = true;

	addInputSocket(new Vector4SocketModel("Edge1"));
	addInputSocket(new Vector4SocketModel("Edge2"));
	addInputSocket(new Vector4SocketModel("Value"));
	addOutputSocket(new Vector4SocketModel("Result"));
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

