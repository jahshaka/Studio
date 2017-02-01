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
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions_3_2_Core>
#include <QFile>
#include "texture.h"
#include <QOpenGLTexture>
#include "mesh.h"

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

ShaderPtr Shader::load(QOpenGLFunctions_3_2_Core* gl,QString vertexShaderFile,QString fragmentShaderFile)
{

    QFile vsFile(vertexShaderFile);
    vsFile.open(QFile::ReadOnly | QFile::Text);
    QString vertexShader(vsFile.readAll());

    QFile fsFile(fragmentShaderFile);
    fsFile.open(QFile::ReadOnly | QFile::Text);
    QString fragmentShader(fsFile.readAll());

    return create(gl,vertexShader,fragmentShader);
}

ShaderPtr Shader::create(QOpenGLFunctions_3_2_Core* gl,QString vertexShader,QString fragmentShader)
{
    return ShaderPtr(new Shader(gl,vertexShader,fragmentShader));
}

Shader::Shader(QOpenGLFunctions_3_2_Core* gl,QString vertexShader,QString fragmentShader)
{
    this->gl = gl;
    shaderId = generateNodeId();

    QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex);
    vshader->compileSourceCode(vertexShader);

    QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment);
    fshader->compileSourceCode(fragmentShader);

    program = new QOpenGLShaderProgram;
    program->addShader(vshader);
    program->addShader(fshader);

    program->bindAttributeLocation("a_pos",(int)VertexAttribUsage::Position);
    program->bindAttributeLocation("a_texCoord",(int)VertexAttribUsage::TexCoord0);
    program->bindAttributeLocation("a_texCoord1",(int)VertexAttribUsage::TexCoord1);
    program->bindAttributeLocation("a_texCoord2",(int)VertexAttribUsage::TexCoord2);
    program->bindAttributeLocation("a_texCoord3",(int)VertexAttribUsage::TexCoord3);
    program->bindAttributeLocation("a_normal",(int)VertexAttribUsage::Normal);
    program->bindAttributeLocation("a_tangent",(int)VertexAttribUsage::Tangent);

    program->link();

    //todo: check for errors

    //get attribs, uniforms and samplers
    //http://stackoverflow.com/questions/440144/in-opengl-is-there-a-way-to-get-a-list-of-all-uniforms-attribs-used-by-a-shade
    auto programId = program->programId();
    GLint count;
    GLint size;
    GLenum type;

    const GLsizei bufSize = 64;
    GLchar name[bufSize];
    GLsizei length;

    //attributes
    gl->glGetProgramiv(programId,GL_ACTIVE_ATTRIBUTES,&count);

    for(int i=0;i<count;i++)
    {
        gl->glGetActiveAttrib(programId,i,bufSize,&length,&size,&type,name);
        auto attrib = new ShaderValue();
        attrib->location = gl->glGetAttribLocation(programId,name);
        attrib->name = std::string(name);
        attrib->type = type;
        this->attribs.insert(QString(name),attrib);
    }

    //uniforms and samplers
    gl->glGetProgramiv(programId,GL_ACTIVE_UNIFORMS,&count);

    for(int i=0;i<count;i++)
    {
        gl->glGetActiveUniform(programId,i,bufSize,&length,&size,&type,name);

        if(type==GL_SAMPLER_1D || type==GL_SAMPLER_2D || type==GL_SAMPLER_3D || type==GL_SAMPLER_CUBE)
        {
            auto sampler = new ShaderSampler();
            sampler->location = gl->glGetUniformLocation(programId,name);
            sampler->name = std::string(name);
            this->samplers.insert(QString(name),sampler);
        }
        else
        {
            auto uniform = new ShaderValue();
            uniform->location = gl->glGetUniformLocation(programId,name);
            uniform->name = std::string(name);
            uniform->type = type;
            this->uniforms.insert(QString(name),uniform);
        }

    }
}

void Shader::bind()
{
    program->bind();

    //update all uniforms
    while(!updatedUniforms.isEmpty())
    {
        auto uniform = updatedUniforms.takeFirst();
    }

    //set textures
    int index = 0;
    for(auto iter = samplers.begin();iter!=samplers.end();iter++)
    {
        auto sampler = iter.value();
        gl->glActiveTexture(GL_TEXTURE0+index);

        if(!!sampler->texture)
        {
            program->setUniformValue(sampler->location, index);
            sampler->texture->bind();
        }
        else
        {
            //bind empty texture to unit
            gl->glBindTexture(GL_TEXTURE_2D,0);
        }

        index++;
    }
}

void Shader::unbind()
{
    //reset textures
    int index = 0;
    for(auto iter = samplers.begin();iter!=samplers.end();iter++)
    {
        gl->glActiveTexture(GL_TEXTURE0+index);
        gl->glBindTexture(GL_TEXTURE_2D,0);

        index++;
    }

    gl->glActiveTexture(GL_TEXTURE0);
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
