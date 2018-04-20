/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "vertexlayout.h"
#include "shader.h"
#include <QOpenGLFunctions_3_2_Core>

namespace iris
{

VertexLayout::VertexLayout()
{
    //gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    stride = 0;
}

QList<VertexAttribute> VertexLayout::getAttribs()
{
	return attribs;
}

void VertexLayout::addAttrib(VertexAttribUsage usage,int type,int count,int sizeOfAttribInBytes)
{
    VertexAttribute attrib = {usage, type, count, sizeOfAttribInBytes};
    attribs.append(attrib);

    stride += sizeOfAttribInBytes;
}

int VertexLayout::getStride()
{
    return stride;
}

// https://stackoverflow.com/a/30106751
#define BUFFER_OFFSET(i) ((char*)nullptr+(i))

void VertexLayout::bind(QOpenGLFunctions_3_2_Core* gl)
{
    int offset = 0;
    for(auto attrib: attribs)
    {
        //gl->glVertexAttribPointer((GLuint)attrib.usage, attrib.count, (GLenum)attrib.type, GL_FALSE, stride, (void*)offset);
        gl->glVertexAttribPointer((GLuint)attrib.usage, attrib.count, (GLenum)attrib.type, GL_FALSE, stride, BUFFER_OFFSET(offset));
        gl->glEnableVertexAttribArray((int)attrib.usage);
        offset += attrib.sizeInBytes;
    }
}

void VertexLayout::unbind(QOpenGLFunctions_3_2_Core* gl)
{
    for(auto attrib: attribs)
    {
        gl->glDisableVertexAttribArray((int)attrib.usage);
    }
}

/*
//default vertex layout for meshes
VertexLayout* VertexLayout::createMeshDefault()
{
    auto layout = new VertexLayout();

    layout->addAttrib("a_pos",GL_FLOAT,3,sizeof(GLfloat)*3);
    layout->addAttrib("a_texCoord",GL_FLOAT,2,sizeof(GLfloat)*2);
    layout->addAttrib("a_normal",GL_FLOAT,3,sizeof(GLfloat)*3);
    layout->addAttrib("a_tangent",GL_FLOAT,3,sizeof(GLfloat)*3);

    return layout;
}
*/
}
