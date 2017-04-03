/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "material.h"
#include <QOpenGLFunctions_3_2_Core>
#include "texture2d.h"
#include "graphicshelper.h"

namespace iris
{

void Material::begin(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene)
{
    this->program->bind();
    this->bindTextures(gl);
}

void Material::beginCube(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene)
{
    this->program->bind();
    this->bindCubeTextures(gl);
}

void Material::end(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene)
{
    this->unbindTextures(gl);
}

void Material::endCube(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene)
{
    this->unbindTextures(gl);
}

void Material::addTexture(QString name,Texture2DPtr texture)
{
    // remove texture if it already exists
    if (textures.contains(name)) {
        auto tex = textures[name];
        textures.remove(name);
        // delete tex;
    }

    textures.insert(name, texture);
}

void Material::removeTexture(QString name)
{
    if (textures.contains(name)) {
        textures.remove(name);
    }
}

void Material::bindTextures(QOpenGLFunctions_3_2_Core* gl)
{
    int count = 0;
    for (auto it = textures.begin(); it != textures.end(); it++, count++) {
        auto tex = it.value();
        gl->glActiveTexture(GL_TEXTURE0+count);

        if (tex->texture != nullptr) {
            tex->texture->bind();
            program->setUniformValue(it.key().toStdString().c_str(), count);
        } else {
            gl->glBindTexture(GL_TEXTURE_2D,0);
        }
    }

    // bind the rest of the textures to 0
    for (; count < numTextures; count++) {
        gl->glActiveTexture(GL_TEXTURE0 + count);
        gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// @TODO -- remove or clean
void Material::bindCubeTextures(QOpenGLFunctions_3_2_Core* gl)
{
    int count=0;
    for(auto it = textures.begin();it != textures.end();it++,count++)
    {
        auto tex = it.value();
        gl->glActiveTexture(GL_TEXTURE0+count);

        if(tex->texture!=nullptr)
        {
            tex->texture->bind();
            program->setUniformValue(it.key().toStdString().c_str(), count);
        }
        else
        {
            gl->glBindTexture(GL_TEXTURE_2D,0);
        }
    }

    //bind the rest of the textures to 0
    for(;count<numTextures;count++)
    {
        gl->glActiveTexture(GL_TEXTURE0+count);
        gl->glBindTexture(GL_TEXTURE_2D,0);
    }

}

void Material::unbindTextures(QOpenGLFunctions_3_2_Core* gl)
{
    for (auto i = 0; i < numTextures; i++) {
        gl->glActiveTexture(GL_TEXTURE0 + i);
        gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void Material::createProgramFromShaderSource(QString vsFile,QString fsFile)
{
    program = GraphicsHelper::loadShader(vsFile, fsFile);
}

void Material::setTextureCount(int count)
{
    numTextures = count;
}

}
