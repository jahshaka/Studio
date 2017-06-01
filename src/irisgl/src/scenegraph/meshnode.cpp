/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDir>

#include "../irisglfwd.h"
#include "meshnode.h"
#include "../graphics/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/mesh.h"
#include "assimp/material.h"
#include "assimp/matrix4x4.h"
#include "assimp/vector3.h"
#include "assimp/quaternion.h"

#include "../graphics/vertexlayout.h"
#include "../materials/defaultmaterial.h"
#include "../materials/custommaterial.h"
#include "../materials/materialhelper.h"
#include "../graphics/renderitem.h"
#include "../animation/animableproperty.h"

#include "../core/scene.h"
#include "../core/scenenode.h"
#include "../core/irisutils.h"
#include "../animation/animableproperty.h"

#include "../graphics/skeleton.h"

namespace iris
{

MeshNode::MeshNode() {
    mesh = nullptr;
    sceneNodeType = SceneNodeType::Mesh;

    renderItem = new RenderItem();
    renderItem->type = RenderItemType::Mesh;

//    materialType = 2;

//    this->customMaterial = iris::CustomMaterial::create();
    faceCullingMode = FaceCullingMode::Back;
}

// @todo: cleanup previous mesh item
FaceCullingMode MeshNode::getFaceCullingMode() const
{
    return faceCullingMode;
}

void MeshNode::setFaceCullingMode(const FaceCullingMode &value)
{
    faceCullingMode = value;
}

QList<Property*> MeshNode::getProperties()
{
    auto props = SceneNode::getProperties();

    // @todo: extract properties from material

    return props;
}

void MeshNode::setMesh(QString source)
{
    mesh = Mesh::loadMesh(source);
    meshPath = source;
    meshIndex = 0;

    renderItem->mesh = mesh;
}

//should not be used on plain scene meshes
void MeshNode::setMesh(Mesh* mesh)
{
    this->mesh = mesh;
    renderItem->mesh = mesh;
}

Mesh* MeshNode::getMesh()
{
    return mesh;
}

void MeshNode::setMaterial(MaterialPtr material)
{
    this->material = material;
//    setActiveMaterial(1);
    renderItem->material = material;
    renderItem->renderStates = material->renderStates;
}

void MeshNode::setCustomMaterial(MaterialPtr material)
{
//    this->customMaterial = material;
//    renderItem->material = customMaterial;
}

void MeshNode::setActiveMaterial(int type)
{
//    if (type == 1) {
//        renderItem->material = material;
//        materialType = 1;
//    } else {
//        renderItem->material = customMaterial;
//        materialType = 2;
//    }
}

void MeshNode::submitRenderItems()
{
    renderItem->worldMatrix = this->globalTransform;

    if (!!material) {
        renderItem->renderLayer = material->renderLayer;
        //renderItem->faceCullingMode = faceCullingMode;
    }

    this->scene->geometryRenderList.append(renderItem);

    if (this->getShadowEnabled()) {
        this->scene->shadowRenderList.append(renderItem);
    }
}

/*
void MeshNode::updateAnimation(float time)
{
    if (mesh->hasSkeletalAnimations() && mesh->hasSkeleton())
    {
        auto skel = mesh->getSkeleton();
        auto anim = mesh->getSkeletalAnimations().values()[0];
        skel->applyAnimation(anim, time);
    }

    //SceneNode::updateAnimation(time);
}
*/

QJsonObject readJahShader(const QString &filePath)
{
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);

    auto data = file.readAll();
    file.close();
    return QJsonDocument::fromJson(data).object();
}

/**
 * Recursively builds a SceneNode/MeshNode heirarchy from the aiScene of the loaded model
 * todo: read and apply material data
 * @param scene
 * @param node
 * @return
 */
QSharedPointer<iris::SceneNode> _buildScene(const aiScene* scene,aiNode* node,QString filePath,
                                            std::function<MaterialPtr(MeshMaterialData& data)> createMaterialFunc)
{
    QSharedPointer<iris::SceneNode> sceneNode;// = QSharedPointer<iris::SceneNode>(new iris::SceneNode());

    // if this node only has one child then make sceneNode a meshnode and add mesh to it
    if(node->mNumMeshes==1)
    {
        auto meshNode = iris::MeshNode::create();
        auto mesh = scene->mMeshes[node->mMeshes[0]];

        // objects like Bezier curves have no vertex positions in the aiMesh
        // aside from that, iris currently only renders meshes
        if(mesh->HasPositions())
        {
            auto meshObj = new Mesh(mesh);
            auto skel = Mesh::extractSkeleton(mesh, scene);
            meshObj->setSkeleton(skel);

            meshNode->setMesh(meshObj);
            meshNode->name = QString(mesh->mName.C_Str());
            meshNode->meshPath = filePath;
            meshNode->meshIndex = node->mMeshes[0];

            // mesh->mMaterialIndex is always at least 0
            auto m = scene->mMaterials[mesh->mMaterialIndex];
            auto dir = QFileInfo(filePath).absoluteDir().absolutePath();

            MeshMaterialData meshMat;
            MaterialHelper::extractMaterialData(m, dir, meshMat);
            auto mat = createMaterialFunc(meshMat);
            if (!!mat)
                meshNode->setMaterial(mat);

        }
        sceneNode = meshNode;
    }
    else
    {
        //otherwise, add meshes as child nodes
        sceneNode = QSharedPointer<iris::SceneNode>(new iris::SceneNode());
        sceneNode->name = QString(node->mName.C_Str());

        for(unsigned i=0;i<node->mNumMeshes;i++)
        {
            auto mesh = scene->mMeshes[node->mMeshes[i]];
            auto meshObj = new Mesh(mesh);
            auto skel = Mesh::extractSkeleton(mesh, scene);
            meshObj->setSkeleton(skel);

            auto meshNode = iris::MeshNode::create();
            meshNode->name = QString(mesh->mName.C_Str());
            meshNode->meshPath = filePath;
            meshNode->meshIndex = node->mMeshes[i];

            meshNode->setMesh(meshObj);
            sceneNode->addChild(meshNode);

            //apply material
            //meshNode->setMaterial(iris::DefaultMaterial::create());
            auto m = scene->mMaterials[mesh->mMaterialIndex];
            auto dir = QFileInfo(filePath).absoluteDir().absolutePath();

            MeshMaterialData meshMat;
            MaterialHelper::extractMaterialData(m, dir, meshMat);
            auto mat = createMaterialFunc(meshMat);
            if (!!mat)
                meshNode->setMaterial(mat);
        }

    }

    //extract transform
    aiVector3D pos,scale;
    aiQuaternion rot;

    //auto transform = node->mTransformation;
    node->mTransformation.Decompose(scale,rot,pos);
    sceneNode->pos = QVector3D(pos.x,pos.y,pos.z);
    sceneNode->scale = QVector3D(scale.x,scale.y,scale.z);

    rot.Normalize();
    sceneNode->rot = QQuaternion(rot.w,rot.x,rot.y,rot.z);

    for(unsigned i=0;i<node->mNumChildren;i++)
    {
        auto child = _buildScene(scene,node->mChildren[i],filePath, createMaterialFunc);
        sceneNode->addChild(child);
    }

    return sceneNode;
}

QSharedPointer<iris::SceneNode> MeshNode::loadAsSceneFragment(QString filePath,
                                                              std::function<MaterialPtr(MeshMaterialData& data)> createMaterialFunc)
{
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(filePath.toStdString().c_str(),aiProcessPreset_TargetRealtime_Fast);

    if (!scene) return QSharedPointer<iris::MeshNode>(nullptr);
    if (scene->mNumMeshes == 0) return QSharedPointer<iris::MeshNode>(nullptr);

    if (scene->mNumMeshes == 1) {
        auto mesh = scene->mMeshes[0];
        auto node = iris::MeshNode::create();

        auto meshObj = new Mesh(mesh);
        auto anims = Mesh::extractAnimations(scene);
        for(auto animName : anims.keys())
            meshObj->addSkeletalAnimation(animName, anims[animName]);

        auto skel = Mesh::extractSkeleton(mesh, scene);
        meshObj->setSkeleton(skel);

        node->setMesh(meshObj);
        node->meshPath = filePath;
        node->meshIndex = 0;

        auto m = scene->mMaterials[mesh->mMaterialIndex];
        auto dir = QFileInfo(filePath).absoluteDir().absolutePath();

        MeshMaterialData meshMat;
        MaterialHelper::extractMaterialData(m, dir, meshMat);
        auto mat = createMaterialFunc(meshMat);
        if (!!mat)
            node->setMaterial(mat);

        return node;
    }

    auto node = _buildScene(scene,scene->mRootNode,filePath, createMaterialFunc);

    return node;
}

SceneNodePtr MeshNode::createDuplicate()
{
    auto node = MeshNode::create();

    // @todo: pass duplicates instead of copies!!!!!!!!
    node->setMesh(this->getMesh());
    node->meshPath = this->meshPath;
    node->meshIndex = this->meshIndex;
    node->setMaterial(this->material);
    //node->setMesh(this->getMesh()->duplicate());
    //node->setMaterial(this->material->duplicate());

    return node;
}

}
