/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "mesh.h"
#include "material.h"

#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/mesh.h"

#include <QString>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLTexture>

#include "vertexlayout.h"
#include "../geometry/trimesh.h"

namespace iris
{

Mesh::Mesh(aiMesh* mesh,VertexLayout* vertexLayout)
{
    gl = new QOpenGLFunctions_3_2_Core();
    gl->initializeOpenGLFunctions();

    gl->glGenVertexArrays(1,&vao);

    triMesh = new TriMesh();

    this->vertexLayout = vertexLayout;
    //assumes mesh is triangulated
    //assumes mesh data is laid out in a flat array
    //i.e. no index buffer used
    /*
    numVerts = 3;
    QVector<float> pos;
    pos.append(0);pos.append(0);pos.append(0);
    pos.append(1);pos.append(1);pos.append(0);
    pos.append(0);pos.append(1);pos.append(0);

    this->addVertexArray(VertexAttribUsage::Position, (void*)pos.data(), sizeof(float) * pos.size(), GL_FLOAT,3);
    */

    //numVerts = mesh->mNumVertices;
    numVerts = mesh->mNumFaces*3;
    numFaces = mesh->mNumFaces;


    this->addVertexArray(VertexAttribUsage::Position, (void*)mesh->mVertices, sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);


    if(mesh->HasTextureCoords(0))
        this->addVertexArray(VertexAttribUsage::TexCoord0, (void*)mesh->mTextureCoords[0], sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);
    if(mesh->HasTextureCoords(1))
        this->addVertexArray(VertexAttribUsage::TexCoord1, (void*)mesh->mTextureCoords[1], sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);
    if(mesh->HasNormals())
        this->addVertexArray(VertexAttribUsage::Normal, (void*)mesh->mNormals, sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);
    if(mesh->HasTangentsAndBitangents())
        this->addVertexArray(VertexAttribUsage::Tangent, (void*)mesh->mTangents, sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);


    /*
    vertexBuffer = 0;
    texCoord0Buffer = 0;
    texCoord1Buffer = 0;
    normalBuffer = 0;
    tangentBuffer = 0;
    indexBuffer = 0;

    //glGenBuffers(VertexAttribUsage::Count, &vertexArrays);

    glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(aiVector3D) * mesh->mNumVertices, mesh->mVertices, GL_STATIC_DRAW);
    vertexArrays[VertexAttribUsage::Position] = vertexBuffer;

    if(mesh->HasTextureCoords(0))
    {
        glGenBuffers(1, &texCoord0Buffer);
        glBindBuffer(GL_ARRAY_BUFFER, texCoord0Buffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(aiVector3D) * mesh->mNumVertices, mesh->mTextureCoords[0], GL_STATIC_DRAW);
        vertexArrays[VertexAttribUsage::TexCoord0] = texCoord0Buffer;
    }

    if(mesh->HasTextureCoords(1))
    {
        glGenBuffers(1, &texCoord1Buffer);
        glBindBuffer(GL_ARRAY_BUFFER, texCoord1Buffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(aiVector3D) * mesh->mNumVertices, mesh->mTextureCoords[1], GL_STATIC_DRAW);
        vertexArrays[VertexAttribUsage::TexCoord1] = texCoord1Buffer;
    }

    vertexArrays[VertexAttribUsage::TexCoord2] = 0;
    vertexArrays[VertexAttribUsage::TexCoord3] = 1;

    glGenBuffers(1, &normalBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, normalBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(aiVector3D) * mesh->mNumVertices, mesh->mNormals, GL_STATIC_DRAW);
    vertexArrays[VertexAttribUsage::Normal] = normalBuffer;

    glGenBuffers(1, &tangentBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, tangentBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(aiVector3D) * mesh->mNumVertices, mesh->mTangents, GL_STATIC_DRAW);
    vertexArrays[VertexAttribUsage::Tangent] = tangentBuffer;
    */

    //Assimp doesnt give the indices in an array
    //Still slow...
    QVector<unsigned int> indices;
    indices.reserve(mesh->mNumFaces * 3);
    for(unsigned i = 0;i < mesh->mNumFaces; i++)
    {
        auto face = mesh->mFaces[i];
        indices.append(face.mIndices[0]);
        indices.append(face.mIndices[1]);
        indices.append(face.mIndices[2]);

        auto a = mesh->mVertices[face.mIndices[0]];
        auto b = mesh->mVertices[face.mIndices[1]];
        auto c = mesh->mVertices[face.mIndices[2]];

        triMesh->addTriangle(QVector3D(a.x, a.y, a.z),
                             QVector3D(b.x, b.y, b.z),
                             QVector3D(c.x, c.y, c.z));
    }

    gl->glGenBuffers(1, &indexBuffer);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * indices.size(), indices.data(), GL_STATIC_DRAW);

    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    gl->glBindBuffer(GL_ARRAY_BUFFER,0);



    /*
    //slow, but good enough for now

    QVector<float> data;
    //data.reserve(mesh->mNumFaces * 8);
    triMesh->triangles.reserve(mesh->mNumFaces * 3);
    for (unsigned f = 0; f < mesh->mNumFaces; f++) {
        auto face = mesh->mFaces[f];

        //assumes mesh is made completely of triangles
        auto a = mesh->mVertices[face.mIndices[0]];
        auto b = mesh->mVertices[face.mIndices[1]];
        auto c = mesh->mVertices[face.mIndices[2]];

        triMesh->addTriangle(QVector3D(a.x, a.y, a.z),
                             QVector3D(b.x, b.y, b.z),
                             QVector3D(c.x, c.y, c.z));


        for(int v=0;v<3;v++)
        {
            auto i = face.mIndices[v];

            //pos
            if(mesh->mNumVertices>0)
            {
                auto pos =  mesh->mVertices[i];
                data.append(pos.x);
                data.append(pos.y);
                data.append(pos.z);
            }
            else
            {
                data.append(0);
                data.append(0);
                data.append(0);
            }

            //texCoord
            if(mesh->mNumUVComponents[0]>0)
            {
                auto texCooord =  mesh->mTextureCoords[0][i];
                data.append(texCooord.x);
                data.append(texCooord.y);
            }
            else
            {
                data.append(0);
                data.append(0);
            }

            if(mesh->HasNormals())
            {
                //normal
                auto normal =  mesh->mNormals[i];
                data.append(normal.x);
                data.append(normal.y);
                data.append(normal.z);
            }
            else
            {
                data.append(0);
                data.append(0);
                data.append(0);
            }


            if(mesh->HasTangentsAndBitangents())
            {
                //tangent
                auto tangent =  mesh->mTangents[i];
                data.append(tangent.x);
                data.append(tangent.y);
                data.append(tangent.z);
            }
            else
            {
                data.append(0);
                data.append(0);
                data.append(0);
            }

        }
    }

    vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    vbo->create();
    vbo->bind();
    vbo->allocate(data.constData(), data.count() * sizeof(GLfloat));
    */
}

//todo: extract trimesh from data
Mesh::Mesh(void* data,int dataSize,int numElements,VertexLayout* vertexLayout)
{
    auto gl = new QOpenGLFunctions_3_2_Core();
    gl->initializeOpenGLFunctions();

    gl->glGenVertexArrays(1,&vao);

    triMesh = nullptr;
    this->vertexLayout = vertexLayout;

    numVerts = numElements;

    vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    vbo->create();
    vbo->bind();
    vbo->allocate(data, dataSize);
}

void Mesh::draw(QOpenGLFunctions_3_2_Core* gl,Material* mat)
{
    draw(gl,mat->program);
}

void Mesh::draw(QOpenGLFunctions_3_2_Core* gl,QOpenGLShaderProgram* program)
{
    /*
    gl->glBindVertexArray(vao);
    vbo->bind();

    vertexLayout->bind(program);
    gl->glDrawArrays(GL_TRIANGLES,0,numVerts);
    vertexLayout->unbind(program);

    gl->glBindVertexArray(0);
    */
    //program->bind();
    gl->glUseProgram(program->programId());
    gl->glBindVertexArray(vao);

    for(int i=0; i < (int)VertexAttribUsage::Count; i++)
    {
        auto attrib = vertexArrays[i];
        if(attrib.bufferId == 0)
            continue;
        gl->glEnableVertexAttribArray(i);
    }

    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indexBuffer);
    //gl->glDrawElements(GL_TRIANGLES,numVerts,GL_UNSIGNED_INT,0);
    gl->glDrawElementsBaseVertex(GL_TRIANGLES,numVerts,GL_UNSIGNED_INT,0,0);
    //gl->glDrawArrays(GL_TRIANGLES,0,numVerts);

    for(int i=0; i < (int)VertexAttribUsage::Count; i++)
    {
        gl->glDisableVertexAttribArray(i);
    }

    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    gl->glBindVertexArray(0);
}

void Mesh::draw(QOpenGLFunctions_3_2_Core* gl,QOpenGLShaderProgram* program,GLenum primitiveMode)
{
    gl->glBindVertexArray(vao);
    vbo->bind();

    vertexLayout->bind(program);
    gl->glDrawArrays(primitiveMode,0,numVerts);
    vertexLayout->unbind(program);

    gl->glBindVertexArray(0);
}

Mesh* Mesh::loadMesh(QString filePath)
{
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(filePath.toStdString().c_str(),aiProcessPreset_TargetRealtime_Fast);

    auto mesh = scene->mMeshes[0];

    return new Mesh(mesh,VertexLayout::createMeshDefault());
}

Mesh* Mesh::create(void* data,int dataSize,int numVerts,VertexLayout* vertexLayout)
{
    return new Mesh(data,dataSize,numVerts,vertexLayout);
}

Mesh::~Mesh()
{
    delete vertexLayout;
    delete vbo;
    delete triMesh;
}

void Mesh::addVertexArray(VertexAttribUsage usage,void* dataPtr,int size,GLenum type,int numComponents)
{
    gl->glBindVertexArray(vao);

    GLuint bufferId;
    gl->glGenBuffers(1, &bufferId);
    gl->glBindBuffer(GL_ARRAY_BUFFER, bufferId);
    gl->glBufferData(GL_ARRAY_BUFFER, size, dataPtr, GL_STATIC_DRAW);


    auto data = VertexArrayData();
    data.usage = usage;
    data.numComponents = numComponents;
    data.type = type;
    data.bufferId = bufferId;

    vertexArrays[(int)usage] = data;

    gl->glVertexAttribPointer((int)usage,numComponents,type,GL_FALSE,0,0);

    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    //gl->glBindVertexArray(0);
}

}
