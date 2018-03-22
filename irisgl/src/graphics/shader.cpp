/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "shader.h"
#include <QMap>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QFile>
#include "texture.h"
#include <QOpenGLTexture>
#include "mesh.h"
#include "texture.h"

namespace iris
{


Shader::~Shader()
{
    for(auto iter = this->attribs.begin();iter!=this->attribs.end();iter++)
    {
        delete iter.value();
    }

    for(auto iter = this->uniforms.begin();iter!=this->uniforms.end();iter++)
    {
        delete iter.value();
    }

    for(auto iter = this->samplers.begin();iter!=this->samplers.end();iter++)
    {
        delete iter.value();
    }

    delete program;
}

void Shader::setVertexShader(QString vertexShader)
{
	this->vertexShader = vertexShader;
	_setDirty();
}

void Shader::setFragmentShader(QString fragmentShader)
{
	this->fragmentShader = fragmentShader;
	_setDirty();
}

ShaderPtr Shader::load(QString vertexShaderFile,QString fragmentShaderFile)
{

    QFile vsFile(vertexShaderFile);
    vsFile.open(QFile::ReadOnly | QFile::Text);
    QString vertexShader(vsFile.readAll());

    QFile fsFile(fragmentShaderFile);
    fsFile.open(QFile::ReadOnly | QFile::Text);
    QString fragmentShader(fsFile.readAll());

    return create(vertexShader,fragmentShader);
}

ShaderPtr Shader::create(QString vertexShader, QString fragmentShader)
{
	auto shader = new Shader();
	shader->setVertexShader(vertexShader);
	shader->setFragmentShader(fragmentShader);
    return ShaderPtr(shader);
}

ShaderPtr Shader::create()
{
    auto shader = new Shader();
	return ShaderPtr(shader);
}

Shader::Shader()
{
	isDirty = true;
	program = nullptr;

    shaderId = generateNodeId();
}

void Shader::_setDirty()
{
	isDirty = true;
}

ShaderValue* Shader::getUniform(QString name)
{
    if(uniforms.contains(name))
        return uniforms[name];

    return nullptr;
}

long Shader::generateNodeId()
{
    return nextId++;
}

long Shader::nextId = 0;

}
