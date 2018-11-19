/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "defaultskymaterial.h"
#include "core/irisutils.h"
#include "graphics/renderitem.h"
#include "scenegraph/scene.h"

namespace iris
{

DefaultSkyMaterial::DefaultSkyMaterial()
{
    createProgramFromShaderSource(":assets/shaders/defaultsky.vert",
                                  ":assets/shaders/flatsky.frag");

    setTextureCount(1);

    color = QColor(255,255,255,255);

    this->setRenderLayer(static_cast<int>(RenderLayer::Background));
}

void DefaultSkyMaterial::setSkyTexture(Texture2DPtr tex)
{
    texture = tex;
    if (!!tex)  this->addTexture("skybox", tex);
    else        this->removeTexture("skybox");
}

void DefaultSkyMaterial::clearSkyTexture()
{
    texture.clear();
    removeTexture("skybox");
}

Texture2DPtr DefaultSkyMaterial::getSkyTexture()
{
    return texture;
}

//void DefaultSkyMaterial::setSkyColor(QColor color)
//{
//    this->color = color;
//}
//
//QColor DefaultSkyMaterial::getSkyColor()
//{
//    return color;
//}

void DefaultSkyMaterial::begin(GraphicsDevicePtr device,ScenePtr scene)
{
//    Material::begin(gl,scene);
//    this->setUniformValue("skybox", texture);
//    if(!!texture)
//        this->setUniformValue("useTexture", true);
//    else
//        this->setUniformValue("useTexture",false);
    beginCube(device, scene);
}

void DefaultSkyMaterial::beginCube(GraphicsDevicePtr device, ScenePtr scene)
{
    Material::beginCube(device, scene);

    switch (static_cast<int>(scene->skyType))
    {
        case static_cast<int>(SkyType::REALISTIC) : {
            setUniformValue("reileigh", scene->skyRealistic.reileigh);
            setUniformValue("luminance", scene->skyRealistic.luminance);
            setUniformValue("mieCoefficient", scene->skyRealistic.mieCoefficient);
            setUniformValue("mieDirectionalG", scene->skyRealistic.mieDirectionalG);
            setUniformValue("turbidity", scene->skyRealistic.turbidity);
            setUniformValue("sunPosition", QVector3D(scene->skyRealistic.sunPosX,
                scene->skyRealistic.sunPosY,
                scene->skyRealistic.sunPosZ));
        }

        case static_cast<int>(SkyType::SINGLE_COLOR): {
            setUniformValue("color", scene->skyColor);
        }

        case static_cast<int>(SkyType::CUBEMAP) : {
            setUniformValue("color", scene->skyColor);
        }

        case static_cast<int>(SkyType::GRADIENT) : {
            setUniformValue("color0", scene->skyColor);
            setUniformValue("color1", scene->skyColor);
            setUniformValue("scale", scene->skyColor);
            setUniformValue("angle", scene->skyColor);
            setUniformValue("type", scene->skyColor);
        }

        default: {
            // should be cubemap eventually
            this->setUniformValue("color", scene->skyColor);
    /*        if (!!texture)  this->setUniformValue("useTexture", true);
            else            this->setUniformValue("useTexture", false);*/
            break;
        }
    }


}

void DefaultSkyMaterial::end(GraphicsDevicePtr device,ScenePtr scene)
{
    Material::end(device, scene);
}

// not needed
void DefaultSkyMaterial::endCube(GraphicsDevicePtr device,ScenePtr scene)
{
    Material::endCube(device, scene);
}

DefaultSkyMaterialPtr DefaultSkyMaterial::create()
{
    return QSharedPointer<DefaultSkyMaterial>(new DefaultSkyMaterial());
}

void DefaultSkyMaterial::switchSkyShader(const QString &vertexShader, const QString &fragmentShader)
{
    createProgramFromShaderSource(vertexShader, fragmentShader);
}

}
