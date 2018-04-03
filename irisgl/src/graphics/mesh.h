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
#include "../geometry/boundingsphere.h"

#include "assimp/scene.h"

class aiMesh;
class QOpenGLBuffer;
class QOpenGLFunctions_3_2_Core;
class QOpenGLShaderProgram;


namespace iris
{

class BoundingSphere;

enum class VertexAttribUsage : int
{
    Position = 0,
    Color = 1,
    TexCoord0 = 2,
    TexCoord1 = 3,
    TexCoord2 = 4,
    TexCoord3 = 5,
    Normal = 6,
    Tangent = 7,
    BiTangent = 8,
    BoneIndices = 9,
    BoneWeights = 10,
    Count = 11
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

	QString nodeName;
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

enum class PrimitiveMode
{
    Triangles,
    Lines,
    LineLoop,
	LineStrip
};

//todo: switch to using mesh pointer
class Mesh
{
    SkeletonPtr skeleton;
    QMap<QString, SkeletalAnimationPtr> skeletalAnimations;

	QList<VertexBufferPtr> vertexBuffers;
	IndexBufferPtr idxBuffer;
	GraphicsDevicePtr device;

    GLenum glPrimitive;
public:
    PrimitiveMode primitiveMode;
    QOpenGLFunctions_3_2_Core* gl;
    GLuint vao;
    GLuint indexBuffer;
    bool usesIndexBuffer;
	bool _isDirty;

    BoundingSphere boundingSphere;

    // will cause problems if a shader was freed and gl gives the
    // id to another shader
    GLuint lastShaderId;

    VertexArrayData vertexArrays[(int)VertexAttribUsage::Count];

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

    //void draw(QOpenGLFunctions_3_2_Core* gl, Material* mat);
    //void draw(QOpenGLFunctions_3_2_Core* gl, QOpenGLShaderProgram* mat);
    void draw(GraphicsDevicePtr device);

    static MeshPtr loadMesh(QString filePath);
    static MeshPtr loadAnimatedMesh(QString filePath);
    static SkeletonPtr extractSkeleton(const aiMesh* mesh, const aiScene* scene);
//    static QMap<QString, SkeletalAnimationPtr> extractAnimations(const aiScene* scene, QString source = "");
    static QMap<QString, SkeletalAnimationPtr> extractAnimations(const aiScene *scene, QString source = "")
    {
        QMap<QString, SkeletalAnimationPtr> anims;

        for (unsigned i = 0; i<scene->mNumAnimations; i++) {
            auto anim = scene->mAnimations[i];
            auto animName = QString(anim->mName.C_Str());
            //qDebug() << "Animation: " << animName;
            auto skelAnim = SkeletalAnimation::create();
            skelAnim->name = animName;
            skelAnim->source = source;

            for (unsigned j = 0; j<anim->mNumChannels; j++) {
                auto nodeAnim = anim->mChannels[j];

                auto nodeName = QString(nodeAnim->mNodeName.C_Str());
                auto boneAnim = new BoneAnimation();

                // extract tracks
                for (unsigned k = 0; k<nodeAnim->mNumPositionKeys; k++) {
                    auto key = nodeAnim->mPositionKeys[k];
                    boneAnim->posKeys->addKey(QVector3D(key.mValue.x, key.mValue.y, key.mValue.z), key.mTime);
                }

                for (unsigned k = 0; k<nodeAnim->mNumRotationKeys; k++) {
                    auto key = nodeAnim->mRotationKeys[k];
                    boneAnim->rotKeys->addKey(QQuaternion(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z), key.mTime);
                }

                for (unsigned k = 0; k<nodeAnim->mNumScalingKeys; k++) {
                    auto key = nodeAnim->mScalingKeys[k];
                    boneAnim->scaleKeys->addKey(QVector3D(key.mValue.x, key.mValue.y, key.mValue.z), key.mTime);
                }

                skelAnim->addBoneAnimation(nodeName, boneAnim);
            }

            anims.insert(animName, skelAnim);
        }

        return anims;
    }


    //assumed ownership of vertexLayout
    static Mesh* create(void* data,int dataSize,int numElements,VertexLayout* vertexLayout);
	static MeshPtr create(VertexLayout vertexLayout);
	static MeshPtr create();

	Mesh();

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

	void setVertexCount(const unsigned int count);
    void setSkeleton(const SkeletonPtr &value);

    PrimitiveMode getPrimitiveMode() const;
    void setPrimitiveMode(const PrimitiveMode &value);

    void clearVertexBuffers();
	void addVertexBuffer(VertexBufferPtr vertexBuffer);
	void setIndexBuffer(IndexBufferPtr indexBuffer);

private:
    void addVertexArray(VertexAttribUsage usage,void* data,int size,GLenum type,int numComponents);
    void addIndexArray(void* data,int size,GLenum type);

    static BoundingSphere calculateBoundingSphere(const aiMesh* mesh);
};

//typedef QSharedPointer<Mesh> MeshPtr;

}

#endif // MESH_H
