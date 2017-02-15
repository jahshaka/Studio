/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "viewermaterial.h"
#include "../core/irisutils.h"

namespace iris
{

ViewerMaterial::ViewerMaterial()
{
    createProgramFromShaderSource(":assets/shaders/viewer.vert",
                                  ":assets/shaders/viewer.frag");

    this->setRenderLayer((int)RenderLayer::Opaque);
}

void ViewerMaterial::setTexture(Texture2DPtr tex)
{
    texture = tex;
    if(!!tex)
        this->addTexture("tex",tex);
    else
        this->removeTexture("tex");
}

Texture2DPtr ViewerMaterial::getTexture()
{
    return texture;
}

void ViewerMaterial::begin(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene)
{
    Material::begin(gl,scene);
}

void ViewerMaterial::end(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene)
{
    Material::end(gl,scene);
}

ViewerMaterialPtr ViewerMaterial::create()
{
    return ViewerMaterialPtr(new ViewerMaterial());
}

}
