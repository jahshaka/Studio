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
#include <QColor>
#include "../irisglfwd.h"
#include "../animation/skeletalanimation.h"


class aiMesh;
class aiScene;
class QOpenGLBuffer;
class QOpenGLFunctions_3_2_Core;
class QOpenGLShaderProgram;


namespace iris
{

class BoundingSphere;

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
    BoneIndices = 8,
    BoneWeights = 9,
    Count = 10
};

struct MeshMaterialData
{
    QColor diffuseColor;
    QColor specularColor;
    QColor ambientColor;
    QColor emissionColor;
    float shininess;

    QString diffuseTexture;
    QString specularTexture;
    QString normalTexture;
};

class VertexArrayData
{
public:

    GLuint bufferId;
    VertexAttribUsage usage;
    int numComponents;
    GLenum type;

    VertexArrayData()
    {
        bufferId = 0;
    }

};

//todo: switch to using mesh pointer
class Mesh
{
    SkeletonPtr skeleton;
    QMap<QString, SkeletalAnimationPtr> skeletalAnimations;
public:
    QOpenGLFunctions_3_2_Core* gl;
    GLuint vao;
    GLuint indexBuffer;
    bool usesIndexBuffer;

    BoundingSphere* boundingSphere;

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


    bool hasSkeleton();
    SkeletonPtr getSkeleton();

    void addSkeletalAnimation(QString name, SkeletalAnimationPtr anim);
    QMap<QString, SkeletalAnimationPtr> getSkeletalAnimations();
    bool hasSkeletalAnimations();

    void draw(QOpenGLFunctions_3_2_Core* gl, Material* mat, GLenum primitiveMode = GL_TRIANGLES);
    void draw(QOpenGLFunctions_3_2_Core* gl, QOpenGLShaderProgram* mat, GLenum primitiveMode = GL_TRIANGLES);

    static Mesh* loadMesh(QString filePath);
    static Mesh* loadAnimatedMesh(QString filePath);
    static SkeletonPtr extractSkeleton(const aiMesh* mesh, const aiScene* scene);
    static QMap<QString, SkeletalAnimationPtr> extractAnimations(const aiScene* scene, QString source = "");

    //assumed ownership of vertexLayout
    static Mesh* create(void* data,int dataSize,int numElements,VertexLayout* vertexLayout);

    Mesh(aiMesh* mesh);

    /**
     *
     * @param data
     * @param dataSize
     * @param numElements number of vertices
     * @param vertexLayout
     */
    Mesh(void* data,int dataSize,int numElements,VertexLayout* vertexLayout);

    ~Mesh();

    void setSkeleton(const SkeletonPtr &value);

private:
    void addVertexArray(VertexAttribUsage usage,void* data,int size,GLenum type,int numComponents);
    void addIndexArray(void* data,int size,GLenum type);
};

typedef QSharedPointer<Mesh> MeshPtr;

}

#endif // MESH_H
