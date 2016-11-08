#include "mesh.h"
#include "material.h"

#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/mesh.h"

#include <QString>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>

#include "GL/gl.h"

#include "vertexlayout.h"

namespace jah3d
{

Mesh::Mesh(aiMesh* mesh,VertexLayout* vertexLayout)
{
    this->vertexLayout = vertexLayout;
    //assumes mesh is triangulated
    //assumes mesh data is laid out in a flat array
    //i.e. no index buffer used

    //numVerts = mesh->mNumVertices;
    numVerts = mesh->mNumFaces*3;
    numFaces = mesh->mNumFaces;

    //slow, but good enough for now
    QVector<float> data;
    for(int f =0;f<mesh->mNumFaces;f++)
    {
        auto face = mesh->mFaces[f];

        //assumes triangles
        for(int v=0;v<3;v++)
        {
            auto i = face.mIndices[v];

            //pos
            auto pos =  mesh->mVertices[i];
            data.append(pos.x);
            data.append(pos.y);
            data.append(pos.z);

            //texCoord
            auto texCooord =  mesh->mTextureCoords[0][i];
            data.append(texCooord.x);
            data.append(texCooord.y);

            //normal
            auto normal =  mesh->mNormals[i];
            data.append(normal.x);
            data.append(normal.y);
            data.append(normal.z);

            //tangent
            auto tangent =  mesh->mTangents[i];
            data.append(tangent.x);
            data.append(tangent.y);
            data.append(tangent.z);
        }
    }


    vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    vbo->create();
    vbo->bind();
    vbo->allocate(data.constData(), data.count() * sizeof(GLfloat));

}

Mesh::Mesh(void* data,int dataSize,int numElements,VertexLayout* vertexLayout)
{
    this->vertexLayout = vertexLayout;

    numVerts = numElements;

    vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    vbo->create();
    vbo->bind();
    vbo->allocate(data, dataSize);
}

void Mesh::draw(QOpenGLFunctions* gl,Material* mat)
{
    draw(gl,mat->program);
}

void Mesh::draw(QOpenGLFunctions* gl,QOpenGLShaderProgram* program)
{
    vbo->bind();

    vertexLayout->bind(program);
    gl->glDrawArrays(GL_TRIANGLES,0,numVerts);//todo: bad to assume triangles, allow other primitive types
    vertexLayout->unbind(program);
}

Mesh* Mesh::loadMesh(QString filePath)
{
    //QApplicaton
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(filePath.toStdString().c_str(),aiProcessPreset_TargetRealtime_Fast);

    auto mesh = scene->mMeshes[0];

    return new Mesh(mesh,VertexLayout::createMeshDefault());
}

Mesh* Mesh::create(void* data,int dataSize,int numVerts,VertexLayout* vertexLayout)
{
    return new Mesh(data,dataSize,numVerts,vertexLayout);
}

}
