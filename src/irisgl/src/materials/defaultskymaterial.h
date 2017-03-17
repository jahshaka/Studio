/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SKYMATERIAL_H
#define SKYMATERIAL_H


#include "../graphics/material.h"
#include "../irisglfwd.h"
#include <QOpenGLShaderProgram>
#include <QColor>

class QOpenGLFunctions_3_2_Core;

namespace iris
{

/**
 * This is the default sky material.
 * It's parameters are:
 * skyTexture
 * skyColor
 *
 * if a skyTexture is specified then the final output color is the
 * texture multiplied by the color. Else, only the color is used.
 */
class DefaultSkyMaterial : public Material
{
public:

    void setSkyTexture(Texture2DPtr tex);
    void clearSkyTexture();
    Texture2DPtr getSkyTexture();

    void setSkyColor(QColor color);
    QColor getSkyColor();

    void begin(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;
    void end(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;
    void beginCube(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;
    void endCube(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;

    static DefaultSkyMaterialPtr create();
private:
    DefaultSkyMaterial();

    Texture2DPtr texture;
    QColor color;
};

}

#endif // SKYMATERIAL_H
