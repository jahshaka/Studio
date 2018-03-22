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
#include "shader.h"
#include "graphicsdevice.h"

namespace iris
{
/*
int RenderLayer::Background = 1000;
int RenderLayer::Opaque = 2000;
int RenderLayer::AlphaTested = 3000;
int RenderLayer::Transparent = 4000;
int RenderLayer::Overlay = 5000;
int RenderLayer::Gizmo = 6000;//the scene depth is cleared before this pass
*/

void Material::begin(GraphicsDevicePtr device,ScenePtr scene)
{
    //shader->program->bind();
	device->setShader(shader);
    this->bindTextures(device);
}

void Material::beginCube(GraphicsDevicePtr device,ScenePtr scene)
{
    //shader->program->bind();
	device->setShader(shader);
    this->bindCubeTextures(device);
}

void Material::end(GraphicsDevicePtr device,ScenePtr scene)
{
    this->unbindTextures(device);
}

void Material::endCube(GraphicsDevicePtr device,ScenePtr scene)
{
    this->unbindTextures(device);
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

void Material::bindTextures(GraphicsDevicePtr device)
{
	auto gl = device->getGL();
    int count = 0;
    for (auto it = textures.begin(); it != textures.end(); it++, count++) {
        auto tex = it.value();
        gl->glActiveTexture(GL_TEXTURE0+count);

        if (!!tex) {
            tex->texture->bind();
			//qDebug() << " texture: " << it.key() << " - " << count << " - " << tex->getTextureId();
            shader->program->setUniformValue(it.key().toStdString().c_str(), count);

			//device->setTexture(count, tex);
			//device->setShaderUniform(it.key().toStdString().c_str(), count);
        } else {
            //gl->glBindTexture(GL_TEXTURE_2D,0);
			device->clearTexture(count);
        }
    }

    // bind the rest of the textures to 0
    for (; count < numTextures; count++) {
        //gl->glActiveTexture(GL_TEXTURE0 + count);
        //gl->glBindTexture(GL_TEXTURE_2D, 0);
		device->clearTexture(count);
    }
}

// @TODO -- remove or clean
void Material::bindCubeTextures(GraphicsDevicePtr device)
{
	auto gl = device->getGL();
    int count=0;
    for(auto it = textures.begin();it != textures.end();it++,count++)
    {
        auto tex = it.value();
        gl->glActiveTexture(GL_TEXTURE0+count);

        if(tex->texture!=nullptr)
        {
            tex->texture->bind();
            shader->program->setUniformValue(it.key().toStdString().c_str(), count);
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

void Material::unbindTextures(GraphicsDevicePtr device)
{
    for (auto i = 0; i < numTextures; i++) {
        //gl->glActiveTexture(GL_TEXTURE0 + i);
        //gl->glBindTexture(GL_TEXTURE_2D, 0);
		device->clearTexture(i);
    }
}

void Material::createProgramFromShaderSource(QString vsFile,QString fsFile)
{
    //program = GraphicsHelper::loadShader(vsFile, fsFile);
	shader = Shader::load(vsFile, fsFile);
}

void Material::setTextureCount(int count)
{
    numTextures = count;
}

QOpenGLShaderProgram* Material::getProgram()
{
	return shader->program;
}

}
