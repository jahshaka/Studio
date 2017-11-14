/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef VIEWERMATERIAL_H
#define VIEWERMATERIAL_H


#include "../graphics/material.h"
#include "../irisglfwd.h"
#include <QOpenGLShaderProgram>
#include <QColor>

class QOpenGLFunctions_3_2_Core;

namespace iris
{

class ViewerMaterial : public Material
{
public:

    void setTexture(Texture2DPtr tex);
    Texture2DPtr getTexture();

    void begin(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;
    void end(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;

    static ViewerMaterialPtr create();
private:
    ViewerMaterial();

    Texture2DPtr texture;
};

}

#endif // VIEWERMATERIAL_H
