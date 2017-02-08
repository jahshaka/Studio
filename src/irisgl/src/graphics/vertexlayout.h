/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef VERTEXLAYOUT_H
#define VERTEXLAYOUT_H

#include <QList>
#include "../irisglfwd.h"
#include "mesh.h"

class QOpenGLFunctions_3_2_Core;

namespace iris
{

//todo: use VAO
struct VertexAttribute
{
    VertexAttribUsage usage;

    int type;//GL_FLOAT,GL_INT, etc
    int count;//2 for vec2, 3 for vec3, etc
    int sizeInBytes;
};

class VertexLayout
{
    QList<VertexAttribute> attribs;
    int stride;
    QOpenGLFunctions_3_2_Core* gl;

public:
    VertexLayout();

    void addAttrib(VertexAttribUsage usage, int type, int count, int sizeInBytes);

    //todo: make this more efficient
    void bind();
    void unbind();

    //default vertex layout for meshes
    static VertexLayout* createMeshDefault();
};

}

#endif // VERTEXLAYOUT_H
