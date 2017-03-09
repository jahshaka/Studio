/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "defaultskymaterial.h"
#include "../core/irisutils.h"
#include "../graphics/renderitem.h"

namespace iris
{

DefaultSkyMaterial::DefaultSkyMaterial()
{
    createProgramFromShaderSource(":assets/shaders/defaultsky.vert",
                                  ":assets/shaders/defaultsky.frag");
    setTextureCount(1);

    color = QColor(255,255,255,255);

    this->setRenderLayer((int)RenderLayer::Background);
}

void DefaultSkyMaterial::setSkyTexture(Texture2DPtr tex)
{
    texture = tex;
    if(!!tex)
        this->addTexture("texture",tex);
    else
        this->removeTexture("texture");
}

void DefaultSkyMaterial::clearSkyTexture()
{
    texture.clear();
    removeTexture("texture");
}

Texture2DPtr DefaultSkyMaterial::getSkyTexture()
{
    return texture;
}

void DefaultSkyMaterial::setSkyColor(QColor color)
{
    this->color = color;
}

QColor DefaultSkyMaterial::getSkyColor()
{
    return color;
}

void DefaultSkyMaterial::begin(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene)
{
    Material::begin(gl,scene);
    this->setUniformValue("color",color);
    if(!!texture)
        this->setUniformValue("useTexture",true);
    else
        this->setUniformValue("useTexture",false);

}

void DefaultSkyMaterial::end(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene)
{
    Material::end(gl,scene);
}

DefaultSkyMaterialPtr DefaultSkyMaterial::create()
{
    return QSharedPointer<DefaultSkyMaterial>(new DefaultSkyMaterial());
}

}
