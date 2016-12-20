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

class QOpenGLFunctions_3_2_Core;
class QOpenGLBuffer;
class QOpenGLShaderProgram;

namespace iris
{

class Billboard
{
    friend class ForwardRenderer;

    QOpenGLBuffer* vbo;
    QOpenGLShaderProgram* program;

public:
    Billboard(QOpenGLFunctions_3_2_Core* gl);
    void draw(QOpenGLFunctions_3_2_Core* gl);
};

}

#endif // BILLBOARD_H
