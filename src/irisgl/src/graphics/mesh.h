/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MESH_H
#define MESH_H

#include <QString>
#include <qopengl.h>
#include "../irisglfwd.h"

class aiMesh;
class QOpenGLBuffer;
class QOpenGLFunctions_3_2_Core;
class QOpenGLShaderProgram;


namespace iris
{

enum class VertexAttribUsage : int
{
    Position = 0,
    TexCoord0 = 1,
    TexCoord1 = 2,
    TexCoord2 = 3,
    TexCoord3 = 4,
    Normal = 5,
    Tangent = 6,
    BiTangent = 7,
    Count = 8
};

class VertexArrayData
{
public:
    //std::string shaderAttrib;
    //int attribLoc;
    GLuint bufferId;
    VertexAttribUsage usage;
    int numComponents;
    GLenum type;

    VertexArrayData()
    {
        bufferId = 0;
    }

};

class Mesh
{

public:
    QOpenGLFunctions_3_2_Core* gl;
    GLuint vao;
    GLuint vertexBuffer;
    GLuint texCoord0Buffer;
    GLuint texCoord1Buffer;
    GLuint normalBuffer;
    GLuint tangentBuffer;
    GLuint indexBuffer;

    // will cause problems if a shader was freed and gl gives the
    // id to another shader
    GLuint lastShaderId;

    VertexArrayData vertexArrays[(int)VertexAttribUsage::Count];

    QOpenGLBuffer* vbo;

    VertexLayout* vertexLayout;
    int numVerts;
    int numFaces;

    TriMesh* triMesh;
    TriMesh* getTriMesh()
    {
        return triMesh;
    }

    void draw(QOpenGLFunctions_3_2_Core* gl,Material* mat);
    void draw(QOpenGLFunctions_3_2_Core* gl,QOpenGLShaderProgram* mat);
    void draw(QOpenGLFunctions_3_2_Core* gl,QOpenGLShaderProgram* mat,GLenum primitiveMode);

    static Mesh* loadMesh(QString filePath);

    //assumes mesh is triangulated
    //makes copy of data
    //assumed ownership of vertexLayout
    static Mesh* create(void* data,int dataSize,int numElements,VertexLayout* vertexLayout);

    Mesh(aiMesh* mesh,VertexLayout* vertexLayout);

    /**
     *
     * @param data
     * @param dataSize
     * @param numElements number of vertices
     * @param vertexLayout
     */
    Mesh(void* data,int dataSize,int numElements,VertexLayout* vertexLayout);

    ~Mesh();

private:
    void addVertexArray(VertexAttribUsage usage,void* data,int size,GLenum type,int numComponents);
};

typedef QSharedPointer<Mesh> MeshPtr;

}

#endif // MESH_H
