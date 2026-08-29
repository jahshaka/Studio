/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "libraryv1.h"
#include "../nodes/test.h"
#include "../nodes/math.h"
#include "../nodes/object.h"
#include "../nodes/vector.h"
#include "../nodes/texture.h"


void LibraryV1::init()
{
	initTest();
	initMath();
	initObject();
	initVector();
	initTexture();
	initConstants();
	initUtility();
}

void LibraryV1::initTest()
{
	auto lib = this;
	QString iconPath = ":/icons/input.png";
	auto type = NodeCategory::Input;

	// property
	lib->addNode("property", "Property", iconPath, type, []()
	{
		auto node = new PropertyNode();
		return node;
	});

	// mult
	lib->addNode("vectorMultiply", "Vector Multiply", iconPath, type, []()
	{
		auto multNode = new VectorMultiplyNode();
		return multNode;
	});

	// normal
	lib->addNode("worldNormal", "World Normal", iconPath, type, []()
	{
		auto normalNode = new WorldNormalNode();
		return normalNode;
	});

	// normal
	lib->addNode("localNormal", "Local Normal", iconPath, type, []()
	{
		auto normalNode = new LocalNormalNode();
		return normalNode;
	});

	// time
	lib->addNode("time", "Time", iconPath, type, []()
	{
		auto node = new TimeNode();
		return node;
	});


	// uv
	lib->addNode("texCoords", "Texture Coordinate", iconPath, type, []()
	{
		return new TextureCoordinateNode();
	});

	//sample texture
	lib->addNode("textureSampler", "Sample Texture", iconPath, type, []() {
		return new TextureSamplerNode();
	});

	// texture
	lib->addNode("texture", "Texture", iconPath, type, []() {
		return new TextureNode();
	});

	// panner
	lib->addNode("panner", "Panner", iconPath, type, []() {
		return new PannerNode();
	});
	
	// normal intensity
	lib->addNode("normalintensity", "Normal Intensity", iconPath, type, []() {
		return new NormalIntensityNode();
	});
}

void LibraryV1::initObject()
{
	QString iconPath = ":/icons/object.png";
	auto type = NodeCategory::Object;

	addNode("fresnel", "Fresnel", iconPath,type, []() {
		return new FresnelNode();
	});

	addNode("depth", "Depth", iconPath,type, []() {
		return new DepthNode();
	});
}

void LibraryV1::initMath()
{
	auto type = NodeCategory::Math;
	QString iconPath = ":/icons/math.png";

	addNode("add", "Add", iconPath, type, []() {
		return new AddNode();
	});

	addNode("subtract", "Subtract", iconPath, type, []() {
		return new SubtractNode();
	});

	addNode("multiply", "Multiply", iconPath, type, []() {
		return new MultiplyNode();
	});

	addNode("divide", "Divide", iconPath, type, []() {
		return new DivideNode();
	});

	/*
	addNode(iconPath, iconPath, iconPath, type, []() {
		return new Node();
	});
	*/

	// sine
	addNode("sine", "Sine", iconPath, type, []()
	{
		return new SineNode();
	});

	addNode("power", "Power", iconPath, type, []() {
		return new PowerNode();
	});

	addNode("sqrt", "Square Root", iconPath, type, []() {
		return new SqrtNode();
	});

	addNode("min", "Min", iconPath, type, []() {
		return new MinNode();
	});

	addNode("max", "Max", iconPath, type, []() {
		return new MaxNode();
	});

	addNode("abs", "Abs", iconPath, type, []() {
		return new AbsNode();
	});

	addNode("sign", "Sign", iconPath, type, []() {
		return new SignNode();
	});

	addNode("ceil", "Ceil", iconPath, type, []() {
		return new CeilNode();
	});

	addNode("floor", "Floor", iconPath, type, []() {
		return new FloorNode();
	});

	addNode("round", "Round", iconPath, type, []() {
		return new RoundNode();
	});

	addNode("trunc", "Truncate", iconPath, type, []() {
		return new TruncNode();
	});

	addNode("step", "Step", iconPath, type, []() {
		return new StepNode();
	});

	addNode("smoothstep", "Smooth Step", iconPath, type, []() {
		return new SmoothStepNode();
	});

	addNode("fraction", "Fraction", iconPath, type, []() {
		return new FracNode();
	});

	addNode("clamp", "Clamp", iconPath, type, []() {
		return new ClampNode();
	});

	addNode("lerp", "Lerp", iconPath, type, []() {
		return new LerpNode();
	});

	addNode("oneminus", "One Minus", iconPath, type, []() {
		return new OneMinusNode();
	});

	addNode("negate", "Negate", iconPath, type, []() {
		return new NegateNode();
	});

	
}

void LibraryV1::initVector()
{
	auto type = NodeCategory::Vector;
	QString iconPath = ":/icons/vector.png";
	addNode("reflect", "Reflect", iconPath, type, []() {
		return new ReflectVectorNode();
	});

	addNode("splitvector", "Split Vector", iconPath, type, []() {
		return new SplitVectorNode();
	});

	addNode("composevector", "Compose Vector", iconPath, type, []() {
		return new ComposeVectorNode();
	});

	addNode("distance", "Distance", iconPath, type, []() {
		return new DistanceVectorNode();
	});

	addNode("dot", "Dot", iconPath, type, []() {
		return new DotVectorNode();
	});

	addNode("length", "Length", iconPath, type, []() {
		return new LengthVectorNode();
	});

	addNode("normalize", "Normalize", iconPath, type, []() {
		return new NormalizeVectorNode();

	});
}

void LibraryV1::initTexture()
{
	auto type = NodeCategory::Texture;
	QString iconPath = ":/icons/texture.png";
	addNode("combinenormals", "Combine Normals", iconPath, type, []() {
		return new CombineNormalsNode();
	});

	addNode("texelsize", "Texel Size", iconPath, type, []() {
		return new TexelSizeNode();
	});

	addNode("flipbook", "Flipbook Animation", iconPath, type, []() {
		return new FlipbookUVAnimationNode();
	});
}

void LibraryV1::initConstants()
{
	auto type = NodeCategory::Constants;
	QString iconPath = ":/icons/constant.png";
	addNode("vector2", "Vector2", iconPath, type, []() {
		return new Vector2Node();
	});

	addNode("vector3", "Vector3", iconPath, type, []() {
		return new Vector3Node();
	});

	addNode("vector4", "Vector4", iconPath, type, []() {
		return new Vector4Node();
	});

	// float
	addNode("float", "Float", iconPath, type, []()
	{
		auto floatNode = new FloatNodeModel();
		return floatNode;
	});

#if(EFFECT_BUILD_AS_LIB)
	addNode("color", "Color", iconPath, type, []() {
		return new ColorPickerNode();

	});
#endif
}

void LibraryV1::initUtility()
{
	auto type = NodeCategory::Utility;
	QString iconPath = ":/icons/utility.png";

	// pulsate
	addNode("pulsate", "Pulsate Node", iconPath, type, []()
	{
		return new PulsateNode();
	});

	//make color
	addNode("makeColor", "Make Color", iconPath, type, []() {
		return new MakeColorNode();
	});
}
