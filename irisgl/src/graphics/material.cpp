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
        //gl->glActiveTexture(GL_TEXTURE0+count);

        if (!!tex) {
            //tex->texture->bind();
            //shader->program->setUniformValue(it.key().toStdString().c_str(), count);
			device->setShaderUniform(it.key(), count);
			device->setTexture(count, tex);
        } else {
			device->clearTexture(count);
        }
    }

    // bind the rest of the textures to 0
    for (; count < numTextures; count++) {
		device->clearTexture(count);
    }
}

// @TODO -- remove or clean
void Material::bindCubeTextures(GraphicsDevicePtr device)
{
	auto gl = device->getGL();
    int count = 0;
    for (auto it = textures.begin(); it != textures.end(); it++, count++) {
        auto tex = it.value();
        //gl->glActiveTexture(GL_TEXTURE0 + count);

		//if (tex->texture != nullptr) {
		if (!!tex) {
            //tex->texture->bind();
            //shader->program->setUniformValue(it.key().toStdString().c_str(), count);
			device->setShaderUniform(it.key(), count);
			device->setTexture(count, tex);
        }
        else {
            gl->glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    // bind the rest of the textures to 0
    //for (; count < numTextures; count++) {
    //    gl->glActiveTexture(GL_TEXTURE0 + count);
    //    gl->glBindTexture(GL_TEXTURE_2D, 0);
    //}

	// bind the rest of the textures to 0
	for (; count < numTextures; count++) {
		device->clearTexture(count);
	}

}

void Material::unbindTextures(GraphicsDevicePtr device)
{
    for (auto i = 0; i < numTextures; i++) {
		device->clearTexture(i);
    }
}

bool Material::isFlagEnabled(QString flag)
{
	return flags.contains(flag);
}

void Material::enableFlag(QString flag)
{
	flags.insert(flag);
	if (!!shader) shader->enableFlag(flag);
	if (!!shadowShader) shadowShader->enableFlag(flag);
}

void Material::disableFlag(QString flag)
{
	flags.remove(flag);
	if (!!shader) shader->disableFlag(flag);
	if (!!shadowShader) shadowShader->disableFlag(flag);
}

void Material::createProgramFromShaderSource(QString vsFile, QString fsFile)
{
	setShader(Shader::load(vsFile, fsFile));
}

static MaterialPtr fromShader(ShaderPtr shader)
{
	Material* mat = new Material();
	mat->setShader(shader);

	return MaterialPtr(mat);
}

void Material::setTextureCount(int count)
{
    numTextures = count;
}

void Material::setShader(ShaderPtr shader)
{
	this->shader = shader;
	if (!!shader) {
		this->numTextures = shader->samplers.count();

		for (auto flag : flags)
			shader->enableFlag(flag);
	}
	else {
		this->numTextures = 0;
	}
}

void Material::setShadowShader(ShaderPtr shader)
{
	this->shadowShader = shader;
	if (!!shader) {
		this->numTextures = shader->samplers.count();

		for (auto flag : flags)
			shader->enableFlag(flag);
	}
	else {
		this->numTextures = 0;
	}
}

QOpenGLShaderProgram* Material::getProgram()
{
	return shader->program;
}

}
