/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "shadergenerator.h"
#include "shadercontext.h"

#include "../graph/nodegraph.h"
#include "../models/nodemodel.h"
#include "../models/connectionmodel.h"

void ShaderGenerator::generateShader(NodeGraph* graph)
{
	//auto masterNode = graph->getMasterNode();

	this->vertexShader = this->processVertexShader(graph);
	this->fragmentShader = this->processFragmentShader(graph);
}

void ShaderGenerator::generateProperties(NodeGraph* graph, ShaderContext* ctx)
{
	for (auto node : graph->nodes.values()) {
		for (auto sock : node->outSockets) {
			auto tempVar = ctx->createTempVar(sock);
			sock->setVarName(tempVar.name);
		}
	}
}

void ShaderGenerator::processMasterNode(NodeModel* node, ShaderContext* ctx)
{
	/*
	for (auto sock : node->inSockets) {
		if (sock->hasConnection()) {
			auto con = sock->getConnection();
			processNode(con->leftSocket->getNode(), ctx);
		}
	}
	*/
}

void ShaderGenerator::preprocess()
{

}

void ShaderGenerator::processNode(NodeModel* node, ShaderContext* ctx)
{
	if (node) 
	for (auto sock : node->inSockets) {
		if (sock->hasConnection()) {
			auto con = sock->getConnection();
			processNode(con->leftSocket->getNode(), ctx);
		}
	}

	// generate code up until this point
	auto previewChunk = node->generatePreview(ctx);
	//auto previewCode = ctx->generateFragmentCode(true);
	auto previewCode = ctx->generateCode(true);
	auto code = ctx->generateUniforms();
	code += ctx->generateVars();
	code += ctx->generateFunctionDefinitions();
	
	code += "void preview(inout PreviewMaterial preview){\n";
	code += previewCode;
	code += previewChunk;
	code += "}\n";

	this->shaderPreviews[node->id] = code;

	node->process(ctx);
}

void ShaderGenerator::postprocess()
{

}

QString ShaderGenerator::processVertexShader(NodeGraph* graph)
{
	auto masterNode = graph->getMasterNode();

	// FRAGMENT SHADER
	auto ctx = new ShaderContext();

	// assigns temp var to each socket
	generateProperties(graph, ctx);

	// process fragment section first
	for (auto sock : masterNode->inSockets) {
		if (sock->hasConnection() && sock->name == "Vertex Offset") {
			auto con = sock->getConnection();
			processNode(con->leftSocket->getNode(), ctx);
		}

		if (sock->hasConnection() && sock->name == "Vertex Extrusion") {
			auto con = sock->getConnection();
			processNode(con->leftSocket->getNode(), ctx);
		}
	}

	auto vertexOffsetVar = masterNode->getValueFromInputSocket(8);
	auto vertexExtrusionVar = masterNode->getValueFromInputSocket(9);
	ctx->addCodeChunk(masterNode, "vertexOffset = " + vertexOffsetVar + ";\n");
	ctx->addCodeChunk(masterNode, "vertexExtrusion = " + vertexExtrusionVar + ";\n");
	

	auto code = ctx->generateUniforms();
	code += ctx->generateVars();
	code += ctx->generateFunctionDefinitions();

	code += "void surface(inout vec3 vertexOffset, inout float vertexExtrusion){\n";
	code += "vertexOffset = " + vertexOffsetVar + ";\n";
	code += "vertexExtrusion = " + vertexExtrusionVar + ";\n";
	code += ctx->generateCode(true);;
	code += "}\n";

	return code;
}

QString ShaderGenerator::processFragmentShader(NodeGraph* graph)
{
	auto masterNode = graph->getMasterNode();

	// FRAGMENT SHADER
	ShaderContext ctx;

	// assigns temp var to each socket
	generateProperties(graph, &ctx);

	// process fragment section first
	//processNode(masterNode, ctx);
	for (auto sock : masterNode->inSockets) {
		if (sock->hasConnection() &&
			sock->name != "Vertex Offset" &&
			sock->name != "Vertex Extrusion") {
			auto con = sock->getConnection();
			processNode(con->leftSocket->getNode(), &ctx);
		}
	}
	masterNode->process(&ctx);

	auto code = ctx.generateUniforms();
	code += ctx.generateVars();
	code += ctx.generateFunctionDefinitions();

	code += "void surface(inout Material material){\n";
	code += ctx.generateCode(true);
	code += "}\n";


	return code;
}
