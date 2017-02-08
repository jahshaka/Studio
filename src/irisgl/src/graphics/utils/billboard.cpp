/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QOpenGLFunctions_3_2_Core>
#include "billboard.h"
#include <qopengl.h>
#include <QOpenGLShader>
#include <QOpenGLBuffer>
#include "../core/irisutils.h"
#include "../graphicshelper.h"
#include "../mesh.h"
#include "../vertexlayout.h"

namespace iris
{

Billboard::Billboard(QOpenGLFunctions_3_2_Core* gl,float size)
{
    program = GraphicsHelper::loadShader(":assets/shaders/billboard.vert",
                                         ":assets/shaders/billboard.frag");

    //build fbo
    QVector<float> data;

    //TRIANGLE 1
    data.append(-size);data.append(-size);data.append(0);
    data.append(0);data.append(0);

    data.append(size);data.append(-size);data.append(0);
    data.append(1);data.append(0);

    data.append(-size);data.append(size);data.append(0);
    data.append(0);data.append(1);

    //TRIANGLE 2
    data.append(-size);data.append(size);data.append(0);
    data.append(0);data.append(1);

    data.append(size);data.append(-size);data.append(0);
    data.append(1);data.append(0);

    data.append(size);data.append(size);data.append(0);
    data.append(1);data.append(1);

    auto layout = new VertexLayout();
    layout->addAttrib(VertexAttribUsage::Position, GL_FLOAT, 3, sizeof(GLfloat) * 3);
    layout->addAttrib(VertexAttribUsage::TexCoord0, GL_FLOAT, 2, sizeof(GLfloat) * 2);

    mesh = Mesh::create((void*)data.constData(), data.count() * sizeof(GLfloat), 6, layout);

}

void Billboard::draw(QOpenGLFunctions_3_2_Core* gl)
{
    mesh->draw(gl,program,GL_TRIANGLES);
}

}
