/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "viewermaterial.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisgl/src/graphics/material.h"
#include "irisgl/src/graphics/graphicsdevice.h"

ViewerMaterial::ViewerMaterial()
{
    createProgramFromShaderSource(":assets/shaders/viewer.vert",
                                  ":assets/shaders/viewer.frag");

    this->setRenderLayer((int)iris::RenderLayer::Opaque);
	renderStates.rasterState = iris::RasterizerState::createCullNone();
}

void ViewerMaterial::setTexture(iris::Texture2DPtr tex)
{
    texture = tex;
    if(!!tex)
        this->addTexture("tex",tex);
    else
        this->removeTexture("tex");
}

iris::Texture2DPtr ViewerMaterial::getTexture()
{
    return texture;
}

void ViewerMaterial::begin(iris::GraphicsDevicePtr device, iris::ScenePtr scene)
{
    Material::begin(device, scene);
}

void ViewerMaterial::end(iris::GraphicsDevicePtr device, iris::ScenePtr scene)
{
    Material::end(device, scene);
}

ViewerMaterialPtr ViewerMaterial::create()
{
    return ViewerMaterialPtr(new ViewerMaterial());
}