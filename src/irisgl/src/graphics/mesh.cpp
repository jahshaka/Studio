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
    lastShaderId = -1;
    gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();

    triMesh = new TriMesh();

    this->vertexLayout = vertexLayout;
    numVerts = mesh->mNumFaces*3;
    numFaces = mesh->mNumFaces;

    gl->glGenVertexArrays(1,&vao);

    if(!mesh->HasPositions())
        throw QString("Mesh has no positions!!");

    this->addVertexArray(VertexAttribUsage::Position, (void*)mesh->mVertices, sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);


    if(mesh->HasTextureCoords(0))
        this->addVertexArray(VertexAttribUsage::TexCoord0, (void*)mesh->mTextureCoords[0], sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);
    if(mesh->HasTextureCoords(1))
        this->addVertexArray(VertexAttribUsage::TexCoord1, (void*)mesh->mTextureCoords[1], sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);
    if(mesh->HasNormals())
        this->addVertexArray(VertexAttribUsage::Normal, (void*)mesh->mNormals, sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);
    if(mesh->HasTangentsAndBitangents())
        this->addVertexArray(VertexAttribUsage::Tangent, (void*)mesh->mTangents, sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);

    gl->glBindVertexArray(vao);
    // Assimp doesnt give the indices in an array
    // So some calculation still has to be done
    QVector<unsigned int> indices;
    indices.reserve(mesh->mNumFaces * 3);
    triMesh->triangles.reserve(mesh->mNumFaces);
    for(unsigned i = 0; i < mesh->mNumFaces; i++)
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

    gl->glBindVertexArray(0);
}

//todo: extract trimesh from data
Mesh::Mesh(void* data,int dataSize,int numElements,VertexLayout* vertexLayout)
{
    lastShaderId = -1;

    gl = new QOpenGLFunctions_3_2_Core();
    gl->initializeOpenGLFunctions();

    gl->glGenVertexArrays(1,&vao);
    gl->glBindVertexArray(vao);

    triMesh = nullptr;
    this->vertexLayout = vertexLayout;

    numVerts = numElements;

    /*
    vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    vbo->create();
    vbo->bind();
    vbo->allocate(data, dataSize);
    */



    gl->glBindVertexArray(0);
}

void Mesh::draw(QOpenGLFunctions_3_2_Core* gl,Material* mat)
{
    draw(gl,mat->program);
}

void Mesh::draw(QOpenGLFunctions_3_2_Core* gl,QOpenGLShaderProgram* program)
{
    auto programId = program->programId();
    gl->glUseProgram(programId);

    gl->glBindVertexArray(vao);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indexBuffer);
    gl->glDrawElements(GL_TRIANGLES,numVerts,GL_UNSIGNED_INT,0);
    gl->glBindVertexArray(0);
}

void Mesh::draw(QOpenGLFunctions_3_2_Core* gl,QOpenGLShaderProgram* program,GLenum primitiveMode)
{
    return;

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

    gl->glVertexAttribPointer((GLuint)usage,numComponents,type,GL_FALSE,0,0);
    gl->glEnableVertexAttribArray((int)usage);

    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl->glBindVertexArray(0);
}

}
