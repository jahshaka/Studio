/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef BILLBOARD_H
#define BILLBOARD_H

#include <qopengl.h>
#include "../../irisglfwd.h"

class QOpenGLFunctions_3_2_Core;
class QOpenGLBuffer;
class QOpenGLShaderProgram;

namespace iris
{

class Mesh;

class Billboard
{
    friend class ForwardRenderer;

    Mesh* mesh;
    QOpenGLShaderProgram* program;

public:
    Billboard(float size=0.5f);
    void draw(GraphicsDevicePtr device);
};

}

#endif // BILLBOARD_H
