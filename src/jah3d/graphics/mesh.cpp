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

    //float* meshData = new ;
    numVerts = mesh->mNumVertices;
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
    //vbo->release();

    vertexLayout = VertexLayout::createMeshDefault();
}

void Mesh::draw(QOpenGLFunctions* gl,Material* mat)
{
    draw(gl,mat->program);
}

void Mesh::draw(QOpenGLFunctions* gl,QOpenGLShaderProgram* program)
{
    vbo->bind();

    /*
    auto stride = (3+2+3+3)*sizeof(GLfloat);
    program->setAttributeBuffer(POS_ATTR_LOC, GL_FLOAT, 0, 3,stride);
    program->setAttributeBuffer(TEXCOORD_ATTR_LOC, GL_FLOAT, 3 * sizeof(GLfloat), 2, stride );
    program->setAttributeBuffer(NORMAL_ATTR_LOC, GL_FLOAT, 5 * sizeof(GLfloat), 3, stride);
    program->setAttributeBuffer(TANGENT_ATTR_LOC, GL_FLOAT, 8 * sizeof(GLfloat), 3, stride);
    */

    vertexLayout->bind(program);
    gl->glDrawArrays(GL_TRIANGLES,0,numFaces*3);
    vertexLayout->unbind(program);
}

Mesh* Mesh::loadMesh(QString filePath)
{
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(filePath.toStdString().c_str(),aiProcessPreset_TargetRealtime_Fast);

    auto mesh = scene->mMeshes[0];

    return new Mesh(mesh,VertexLayout::createMeshDefault());
}

}
