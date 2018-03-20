/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "../irisglfwd.h"
#include "mesh.h"
#include "material.h"

#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/mesh.h"

#include <QString>
#include <QFile>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLTexture>
#include <QtMath>

#include "graphicsdevice.h"
#include "vertexlayout.h"
#include "../geometry/trimesh.h"
#include "skeleton.h"
#include "../animation/skeletalanimation.h"
#include "../geometry/boundingsphere.h"

#include <functional>

namespace iris
{

QMatrix4x4 aiMatrixToQMatrix(aiMatrix4x4 aiMat) {
    aiVector3D pos,scale;
    aiQuaternion rot;

    aiMat.Decompose(scale,rot,pos);

    rot.Normalize();

    QMatrix4x4 mat;
    mat.setToIdentity();
    mat.translate(QVector3D(pos.x, pos.y, pos.z));
    mat.rotate(QQuaternion(rot.w, rot.x, rot.y, rot.z));
    mat.scale(QVector3D(scale.x, scale.y, scale.z));

    return mat;
}

// http://ogldev.atspace.co.uk/www/tutorial38/tutorial38.html
Mesh::Mesh(aiMesh* mesh)
{
	_isDirty = 0;
    lastShaderId = -1;
    gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();

    triMesh = new TriMesh();

    this->vertexLayout = nullptr;
    numVerts = mesh->mNumFaces*3;
    numFaces = mesh->mNumFaces;

    gl->glGenVertexArrays(1,&vao);

    if(!mesh->HasPositions())
        return;
        //throw QString("Mesh has no positions!!");

    this->addVertexArray(VertexAttribUsage::Position, (void*)mesh->mVertices, sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);


    if(mesh->HasTextureCoords(0))
        this->addVertexArray(VertexAttribUsage::TexCoord0, (void*)mesh->mTextureCoords[0], sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);
    if(mesh->HasTextureCoords(1))
        this->addVertexArray(VertexAttribUsage::TexCoord1, (void*)mesh->mTextureCoords[1], sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);
    if(mesh->HasNormals())
        this->addVertexArray(VertexAttribUsage::Normal, (void*)mesh->mNormals, sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);
    if(mesh->HasTangentsAndBitangents())
        this->addVertexArray(VertexAttribUsage::Tangent, (void*)mesh->mTangents, sizeof(aiVector3D) * mesh->mNumVertices, GL_FLOAT,3);

    if (mesh->HasBones()) {
        // bone weights for skeletal animation
    #define MAX_BONE_INDICES 4
        QVector<float> boneIndices;
        boneIndices.resize(MAX_BONE_INDICES * mesh->mNumVertices);
        boneIndices.fill(0);
        QVector<float> boneWeights;
        boneWeights.resize(MAX_BONE_INDICES * mesh->mNumVertices);
        boneWeights.fill(0);


        for (unsigned i = 0;i<mesh->mNumBones; i++) {
            auto bone = mesh->mBones[i];

            for (unsigned j = 0;j<bone->mNumWeights ; j++) {
                auto weight = bone->mWeights[j];
                auto baseIndex = weight.mVertexId * MAX_BONE_INDICES;
                //qDebug() << weight.mVertexId << " - " << i << " - " << weight.mWeight;
                // find empty slot and set weight
                for(unsigned k = 0; k<MAX_BONE_INDICES; k++) {
                    if (baseIndex + k < (unsigned)boneWeights.size()) { //just in case
                        if(boneWeights[baseIndex + k] == 0) {
                            // an empty weight means an empty slot
                            boneIndices[baseIndex + k] = i;// bone index in array
                            boneWeights[baseIndex + k] = weight.mWeight;
                            break;
                        }
                    } else {
                        //qDebug() << "Invalid vertex index "<<baseIndex + k;
                    }
                }
            }
        }

//        for ( auto i =0 ; i < boneWeights.size(); i++) {
//            qDebug() << boneIndices[i] << " - " << boneWeights[i];
//        }

        //this->addVertexArray(VertexAttribUsage::BoneIndices, (void*)boneIndices.data(), sizeof(int) * boneIndices.size(), GL_INT, MAX_BONE_INDICES);
        this->addVertexArray(VertexAttribUsage::BoneIndices, (void*)boneIndices.data(), sizeof(float) * boneIndices.size(), GL_FLOAT, MAX_BONE_INDICES);
        this->addVertexArray(VertexAttribUsage::BoneWeights, (void*)boneWeights.data(), sizeof(float) * boneWeights.size(), GL_FLOAT, MAX_BONE_INDICES);
    }

    // Assimp doesnt give the indices in an array
    // So some calculation still has to be done
    QVector<unsigned int> indices;
    indices.reserve(mesh->mNumFaces * 3);
    triMesh->triangles.reserve(mesh->mNumFaces);
    for(unsigned i = 0; i < mesh->mNumFaces; i++)
    {

        auto face = mesh->mFaces[i];

        if (face.mNumIndices!=3)
            continue;

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
    /*
    gl->glGenBuffers(1, &indexBuffer);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * indices.size(), indices.data(), GL_STATIC_DRAW);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    */
    usesIndexBuffer = true;
    idxBuffer = IndexBuffer::create();
    idxBuffer->setData(indices.data(), sizeof(unsigned int) * indices.size());

    // the true size
    numVerts = indices.size();

    this->setPrimitiveMode(PrimitiveMode::Triangles);

    boundingSphere = calculateBoundingSphere(mesh);
}

//todo: extract trimesh from data
Mesh::Mesh(void* data,int dataSize,int numElements,VertexLayout* vertexLayout)
{
    lastShaderId = -1;
    triMesh = nullptr;
    numVerts = numElements;
/*
    gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    this->vertexLayout = vertexLayout;

    gl->glGenVertexArrays(1,&vao);
    gl->glBindVertexArray(vao);

    GLuint vbo;
    gl->glGenBuffers(1, &vbo);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl->glBufferData(GL_ARRAY_BUFFER,dataSize,data,GL_STATIC_DRAW);

    vertexLayout->bind();

    gl->glBindBuffer(GL_ARRAY_BUFFER,0);
    gl->glBindVertexArray(0);
*/
    auto vb = VertexBuffer::create(*vertexLayout);
    vb->setData(data, dataSize);
    vertexBuffers.append(vb);

    usesIndexBuffer = false;
    this->setPrimitiveMode(PrimitiveMode::Triangles);
}

void Mesh::setSkeleton(const SkeletonPtr &value)
{
    skeleton = value;
}

PrimitiveMode Mesh::getPrimitiveMode() const
{
    return primitiveMode;
}

void Mesh::setPrimitiveMode(const PrimitiveMode &value)
{
    primitiveMode = value;

    switch (primitiveMode) {
    case PrimitiveMode::Triangles:
        glPrimitive = GL_TRIANGLES;
        break;
    case PrimitiveMode::Lines:
        glPrimitive = GL_LINES;
        break;
    case PrimitiveMode::LineLoop:
        glPrimitive = GL_LINE_LOOP;
        break;
    default:
        glPrimitive = GL_TRIANGLES;
        break;
    }
}

bool Mesh::hasSkeleton()
{
    return !!skeleton;
}

SkeletonPtr Mesh::getSkeleton()
{
    return skeleton;
}

void Mesh::addSkeletalAnimation(QString name, SkeletalAnimationPtr anim)
{
    skeletalAnimations.insert(name, anim);
}

QMap<QString, SkeletalAnimationPtr> Mesh::getSkeletalAnimations()
{
    return skeletalAnimations;
}

bool Mesh::hasSkeletalAnimations()
{
    return skeletalAnimations.count() != 0;
}
/*
void Mesh::draw(QOpenGLFunctions_3_2_Core* gl,Material* mat)
{
    draw(gl,mat->program);
}

void Mesh::draw(QOpenGLFunctions_3_2_Core* gl,QOpenGLShaderProgram* program)
{
    if (program) {
        auto programId = program->programId();
        gl->glUseProgram(programId);
    }

    gl->glBindVertexArray(vao);
    if(usesIndexBuffer)
    {
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indexBuffer);
        gl->glDrawElements(glPrimitive,numVerts,GL_UNSIGNED_INT,0);
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    }
    else
    {
        gl->glDrawArrays(glPrimitive,0,numVerts);
    }
    gl->glBindVertexArray(0);
}
*/
void Mesh::draw(GraphicsDevicePtr device)
{
    device->setVertexBuffers(vertexBuffers);
    if (!!idxBuffer) {
        device->setIndexBuffer(idxBuffer);
        device->drawIndexedPrimitives(glPrimitive, 0, numVerts);
    } else {
        device->drawPrimitives(glPrimitive, 0, numVerts);
    }
}

MeshPtr Mesh::loadMesh(QString filePath)
{
    // legacy -- update TODO
    Assimp::Importer importer;
    const aiScene *scene;

    if (filePath.startsWith(":") || filePath.startsWith("qrc:")) {
        // loads mesh from resource
        QFile file(filePath);
        file.open(QIODevice::ReadOnly);
        auto data = file.readAll();
        scene = importer.ReadFileFromMemory((void*)data.data(),
                                            data.length(),
                                            aiProcessPreset_TargetRealtime_Fast);
    } else {
        scene = importer.ReadFile(filePath.toStdString().c_str(),
                                  aiProcessPreset_TargetRealtime_Fast);
    }

    auto mesh = scene->mMeshes[0];
    auto meshObj = new Mesh(scene->mMeshes[0]);
    auto skel = extractSkeleton(mesh, scene);

    if (!!skel)
        meshObj->setSkeleton(skel);

    auto anims = extractAnimations(scene);
    for (auto animName : anims.keys())
    {
        meshObj->addSkeletalAnimation(animName, anims[animName]);
    }

    return MeshPtr(meshObj);
}

MeshPtr Mesh::loadAnimatedMesh(QString filePath)
{
    Assimp::Importer importer;
    const aiScene *scene;

    if (filePath.startsWith(":") || filePath.startsWith("qrc:")) {
        // loads mesh from resource
        QFile file(filePath);
        file.open(QIODevice::ReadOnly);
        auto data = file.readAll();
        scene = importer.ReadFileFromMemory((void*)data.data(),
                                            data.length(),
                                            aiProcessPreset_TargetRealtime_Fast);
    } else {
        scene = importer.ReadFile(filePath.toStdString().c_str(),
                                  aiProcessPreset_TargetRealtime_Fast);
    }

    //extract animations from scene
    auto mesh = new Mesh(scene->mMeshes[0]);

    return MeshPtr(mesh);
}

SkeletonPtr Mesh::extractSkeleton(const aiMesh *mesh, const aiScene *scene)
{
    if (mesh->mNumBones == 0)
        return SkeletonPtr();

    // create skeleton and add animations
    auto skel = Skeleton::create();
    for (unsigned i = 0;i<mesh->mNumBones; i++) {
        auto meshBone = mesh->mBones[i];

        auto bone = Bone::create(QString(meshBone->mName.C_Str()));
        bone->inversePoseMatrix = aiMatrixToQMatrix(meshBone->mOffsetMatrix);
        bone->poseMatrix = bone->inversePoseMatrix.inverted();

        skel->addBone(bone);
    }

    //evaluate the bone heirarchy
    std::function<void(aiNode*)> evalChildren;
    evalChildren = [skel, &evalChildren](aiNode* parent){
        auto bone = skel->getBone(QString(parent->mName.C_Str()));
        if (!!bone) {
            //qDebug() << bone->name;
            for ( unsigned i = 0; i < parent->mNumChildren; i++)
            {
                auto childNode = parent->mChildren[i];
                auto childBone = skel->getBone(QString(childNode->mName.C_Str()));
                if (!!childBone)
                    bone->addChild(childBone);

                //evalChildren(childNode);
            }
        }

        for ( unsigned i = 0; i < parent->mNumChildren; i++)
        {
            auto childNode = parent->mChildren[i];
            evalChildren(childNode);
        }
    };

    evalChildren(scene->mRootNode);

    return skel;
}

Mesh* Mesh::create(void* data,int dataSize,int numVerts,VertexLayout* vertexLayout)
{
    return new Mesh(data,dataSize,numVerts,vertexLayout);
}

Mesh::~Mesh()
{
    //delete vertexLayout;
	if (triMesh)
		delete triMesh;
}

void Mesh::addVertexArray(VertexAttribUsage usage,void* dataPtr,int size,GLenum type,int numComponents)
{
    /*
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
    */
    VertexLayout layout;
    int sizeInBytes = 0;
    switch(type) {
    case GL_FLOAT:
    case GL_INT:
        sizeInBytes = 4 * numComponents;
    }
    layout.addAttrib(usage, type, numComponents, sizeInBytes);
    auto vb = VertexBuffer::create(layout);
    vb->setData(dataPtr, size);
    vertexBuffers.append(vb);
}

void Mesh::addIndexArray(void* data,int size,GLenum type)
{

}

// https://github.com/playcanvas/engine/blob/master/src/shape/bounding-sphere.js#L30
// bounding sphere wont necessarily be at the center of the mesh's origin
// the true positon in world space would be the bounds's center plus the mesh's absolute position
BoundingSphere Mesh::calculateBoundingSphere(const aiMesh *mesh)
{
    // find average pos
    aiVector3D averagePos;// = mesh->mVertices[0];
    aiVector3D sum(0,0,0);

    for(unsigned int i =0;i<mesh->mNumVertices;i++) {
        sum += mesh->mVertices[i];

        if (i%100 == 0) {
            sum /= mesh->mNumVertices;
            averagePos += sum;
            sum = aiVector3D(0,0,0);
        }
    }

    sum/=mesh->mNumVertices;
    averagePos += sum;

    //averagePos = averagePos/mesh->mNumVertices;

    // find furthest distance
    float maxDistSqrd = 0;
    for(unsigned int i =0;i<mesh->mNumVertices;i++) {
        auto vert = mesh->mVertices[i];
        auto diff = vert-averagePos;
        auto dist = diff.SquareLength();

        if (dist > maxDistSqrd)
            maxDistSqrd = dist;
    }

    BoundingSphere sphere;
    sphere.pos = QVector3D(averagePos.x, averagePos.y, averagePos.x);
    sphere.radius = qSqrt(maxDistSqrd);
    return sphere;
}

}
