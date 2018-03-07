/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHADERPROGRAM_H
#define SHADERPROGRAM_H

#include "../irisglfwd.h"
#include <QVariant>
#include <qopengl.h>

class QOpenGLShaderProgram;
class QOpenGLFunctions_3_2_Core;
class QOpenGLTexture;

namespace iris
{

class ShaderValue
{

public:
    ShaderValue()
    {
        modified = false;
        location = -1;
        //name = nullptr;
        //value = QVariant();
        type = -1;
    }

    template<typename T>
    void setValue(T val)
    {
        value = QVariant(val);
        modified = true;
    }

public:
    int location;
    std::string name;
    int type;
    QVariant value;

private:
    bool modified;
};

class ShaderSampler
{
public:
    int location;
    std::string name;
    TexturePtr texture;
};

class Shader
{
    friend class Material;
	friend class VertexLayout;
	friend class GraphicsDevice;

public:
    static ShaderPtr load(GraphicsDevicePtr device, QString vertexShaderFile, QString fragmentShaderFile);
    static ShaderPtr create(GraphicsDevicePtr device, QString vertexShader, QString fragmentShader);
	static ShaderPtr create(GraphicsDevicePtr device);

    ~Shader();

	void setVertexShader(QString vertexShader);
	void setFragmentShader(QString fragmentShader);

	void _setDirty();

private:
	QOpenGLShaderProgram * program;
	long shaderId;

    ShaderValue* getUniform(QString name);

    template <typename T>
    void setUniformValue(QString name, T value)
    {
        if(uniforms.contains(name))
        {
            auto uniform = uniforms[name];
            uniform->value = value;
            updatedUniforms.append(uniform);
        }
    }

    void setTexture(QString name, TexturePtr texture)
    {
        if(samplers.contains(name))
        {
            auto sampler = samplers[name];
            sampler->texture = texture;
        }
    }

	Shader(GraphicsDevicePtr device);

	GraphicsDevicePtr device;
	bool isDirty;

	void compileShader();

    static long generateNodeId();
    static long nextId;

protected:
    QMap<QString,ShaderValue*> attribs;
    QMap<QString,ShaderValue*> uniforms;
    QMap<QString,ShaderSampler*> samplers;

    QList<ShaderValue*> updatedUniforms;

	QString vertexShader, fragmentShader;
};

}

#endif // SHADERPROGRAM_H
