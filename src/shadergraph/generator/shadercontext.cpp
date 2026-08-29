/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include <QString>
#include <QList>

#include "shadercontext.h"
#include "../models/nodemodel.h"
#include "../models/socketmodel.h"

void ShaderContext::addFunction(QString name, QString function)
{
	if (!functions.contains(name))
		functions.insert(name, { name, function });
}

// created a temporary variable using the socket
TempVar ShaderContext::createTempVar(SocketModel* sock)
{
	if (tempVars.contains(sock->id))
		return tempVars[sock->id];

	TempVar tempVar;
	tempVar.name = QString("temp%1").arg(tempVars.count());
	tempVar.typeName = sock->typeName;
	tempVars.insert(sock->id, tempVar);

	return tempVar;
}

TempVar ShaderContext::createTempVar(QString typeName)
{
	TempVar tempVar;
	tempVar.name = QString("temp%1").arg(tempVars.count());
	tempVar.typeName = typeName;
	tempVars.insert((new QUuid())->toString(), tempVar);

	return tempVar;
}

QList<TempVar> ShaderContext::getTempVars()
{
	return tempVars.values();
}

void ShaderContext::clear()
{
	tempVars.clear();
}

void ShaderContext::addCodeChunk(NodeModel* node, QString code)
{
	QString ownerName = node->title + " - " + node->id;
	//CodeChunk chunk = {node, code};
	codeChunks.append({ ownerName, code });
}

// add declaration for uniform here
// only adds it once
void ShaderContext::addUniform(QString uniformDecl)
{
	if (uniforms.indexOf(uniformDecl) == -1)
		uniforms.append(uniformDecl);
}

QString ShaderContext::generateVars()
{
	QString finalCode = "";

	// generate temp vars
	for (auto& var : tempVars) {
		// todo: ensure only valid types
		// for example, there is a texture type but it's not a valid shader type
		// it is still however used in calculations
		// so it' excluded
		if (var.typeName == "texture")
			continue;

		auto c = var.typeName + " " + var.name + ";\n";
		finalCode = c + finalCode;
	}

	return finalCode;
}

// generates uniforms from properties
QString ShaderContext::generateUniforms()
{
	QString finalCode = "";

	// generate temp vars
	for (auto& uniform : uniforms) {
		finalCode += uniform + ";\n";
	}

	return finalCode;
}

QString ShaderContext::generateFunctionDefinitions()
{
	QString finalCode = "";

	// generate temp vars
	for (auto& function : functions) {
		finalCode += function.functionBody + ";\n\n";
	}

	return finalCode;
}


QString ShaderContext::generateCode(bool debug)
{
	QString finalCode = "";

	// combine code chunks
	for (auto chunk : codeChunks) {
		if (debug)
			finalCode += "// " + chunk.owner + "\n";

		finalCode += chunk.code;
		finalCode += "\n";

		// extra space for debug mode
		if (debug)
			finalCode += "\n";
	}

	return finalCode;
}

QString ShaderContext::generateFragmentCode(bool debug)
{
	QString finalCode = "";

	// combine code chunks
	for (auto chunk : codeChunks) {
		if (debug)
			finalCode += "// " + chunk.owner + "\n";

		finalCode += chunk.code;
		finalCode += "\n";

		// extra space for debug mode
		if (debug)
			finalCode += "\n";
	}

	return finalCode;
}