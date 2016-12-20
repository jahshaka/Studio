/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef FULLSCREENQUAD_H
#define FULLSCREENQUAD_H

class QOpenGLFunctions_3_2_Core;
class QOpenGLShaderProgram;

namespace iris
{

class Mesh;


class FullScreenQuad
{
public:
    FullScreenQuad();
    ~FullScreenQuad();

    void draw(QOpenGLFunctions_3_2_Core* gl);

    QOpenGLShaderProgram* shader;
    Mesh* mesh;
};
}

#endif // FULLSCREENQUAD_H
