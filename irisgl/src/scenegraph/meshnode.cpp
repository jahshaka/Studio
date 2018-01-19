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
#include "../animation/animation.h"

#include "../scenegraph/scene.h"
#include "../scenegraph/scenenode.h"
#include "../core/irisutils.h"
#include "../animation/animableproperty.h"

#include "../graphics/skeleton.h"
#include "../graphics/renderlist.h"

namespace iris
{

MeshNode::MeshNode() {
    sceneNodeType = SceneNodeType::Mesh;

    renderItem = new RenderItem();
    renderItem->type = RenderItemType::Mesh;

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
void MeshNode::setMesh(MeshPtr mesh)
{
    this->mesh = mesh;
    renderItem->mesh = mesh;
}

MeshPtr MeshNode::getMesh()
{
    return mesh;
}

void MeshNode::setMaterial(MaterialPtr material)
{
    this->material = material;
    renderItem->material = material;
    renderItem->renderStates = material->renderStates;
}

void MeshNode::submitRenderItems()
{
    if (visible) {
        renderItem->cullable = false;

        renderItem->worldMatrix = this->globalTransform;
 
        if (!!mesh && renderItem->cullable) {
            renderItem->boundingSphere.pos = this->globalTransform * mesh->boundingSphere.pos;
            renderItem->boundingSphere.radius = mesh->boundingSphere.radius * getMeshRadius();
        }

        if (!!material) {
            renderItem->renderLayer = material->renderLayer;
        }

        this->scene->geometryRenderList->add(renderItem);

        if (this->getShadowCastingEnabled()) {
            this->scene->shadowRenderList->add(renderItem);
        }
    }
}

float MeshNode::getMeshRadius()
{
    float scaleX = globalTransform.column(0).toVector3D().length();
    float scaleY = globalTransform.column(1).toVector3D().length();
    float scaleZ = globalTransform.column(2).toVector3D().length();

    return qMax(qMax(scaleX, scaleY), scaleZ);
}

BoundingSphere MeshNode::getTransformedBoundingSphere()
{
    BoundingSphere boundingSphere;
    boundingSphere.pos = this->globalTransform * mesh->boundingSphere.pos;
    boundingSphere.radius = mesh->boundingSphere.radius * getMeshRadius();
    return boundingSphere;
}

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
QSharedPointer<iris::SceneNode> _buildScene(const aiScene* scene,
											aiNode* node,
											SceneNodePtr rootBone,
											QString filePath,
											std::function<MaterialPtr(MeshPtr mesh, MeshMaterialData& data)> createMaterialFunc)
{
    QSharedPointer<iris::SceneNode> sceneNode;

    // if this node only has one child then make sceneNode a meshnode and add mesh to it
    if (node->mNumMeshes == 1) {
        auto meshNode = iris::MeshNode::create();
        auto mesh = scene->mMeshes[node->mMeshes[0]];

        // objects like Bezier curves have no vertex positions in the aiMesh
        // aside from that, iris currently only renders meshes
        if (mesh->HasPositions()) {
            auto meshObj = MeshPtr(new Mesh(mesh));
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
            auto mat = createMaterialFunc(meshObj, meshMat);
            if (!!mat) meshNode->setMaterial(mat);
        }

        meshNode->rootBone = rootBone;
        sceneNode = meshNode;
    }
    else {
        //otherwise, add meshes as child nodes
        sceneNode = QSharedPointer<iris::SceneNode>(new iris::SceneNode());
        sceneNode->name = QString(node->mName.C_Str());

        for (unsigned i = 0; i < node->mNumMeshes; i++) {
            auto mesh = scene->mMeshes[node->mMeshes[i]];
            auto meshObj = MeshPtr(new Mesh(mesh));
            auto skel = Mesh::extractSkeleton(mesh, scene);
            meshObj->setSkeleton(skel);

            auto meshNode = iris::MeshNode::create();
            meshNode->name = QString(mesh->mName.C_Str());
            meshNode->meshPath = filePath;
            meshNode->meshIndex = node->mMeshes[i];

            meshNode->setMesh(meshObj);
            sceneNode->addChild(meshNode);

            //apply material
            auto m = scene->mMaterials[mesh->mMaterialIndex];
            auto dir = QFileInfo(filePath).absoluteDir().absolutePath();

            MeshMaterialData meshMat;
            MaterialHelper::extractMaterialData(m, dir, meshMat);
            auto mat = createMaterialFunc(meshObj, meshMat);
            if (!!mat) meshNode->setMaterial(mat);
        }
    }

    //extract transform
    aiVector3D pos,scale;
    aiQuaternion rot;

    //auto transform = node->mTransformation;
    node->mTransformation.Decompose(scale,rot,pos);
    sceneNode->setLocalPos(QVector3D(pos.x,pos.y,pos.z));
    sceneNode->setLocalScale(QVector3D(scale.x,scale.y,scale.z));

    sceneNode->setLocalRot(QQuaternion(rot.w,rot.x,rot.y,rot.z));

    // this is probably the first node in the hierarchy, set it as the rootBone
    if (!rootBone) rootBone = sceneNode;

    for (unsigned i = 0 ;i < node->mNumChildren; i++) {
        auto child = _buildScene(scene, node->mChildren[i], rootBone, filePath, createMaterialFunc);
        sceneNode->addChild(child, false);
    }

    sceneNode->setAttached(true);
    return sceneNode;
}

QSharedPointer<iris::SceneNode>
MeshNode::loadAsSceneFragment(QString filePath,
                              std::function<MaterialPtr(MeshPtr mesh, MeshMaterialData& data)> createMaterialFunc,
                              SceneSource *scene_, IModelReadProgress* progressReader)
{
    ModelProgressHandler *handle = new ModelProgressHandler();
    handle->setHandler(progressReader);

    scene_->importer.SetProgressHandler(handle);
    const aiScene *scene = scene_->importer.ReadFile(filePath.toStdString().c_str(), aiProcessPreset_TargetRealtime_Quality);

    if (scene->mNumMeshes == 0) return QSharedPointer<iris::MeshNode>(nullptr);
    if (scene->mNumMeshes == 1) {
        auto mesh = scene->mMeshes[0];
        auto node = iris::MeshNode::create();

        auto meshObj = MeshPtr(new Mesh(mesh));

        //todo: use relative path from scene root
        auto anims = Mesh::extractAnimations(scene, filePath);
        for (auto animName : anims.keys()) {
            // meshObj->addSkeletalAnimation(animName, anims[animName]);
            auto anim = Animation::createFromSkeletalAnimation(anims[animName]);
            node->addAnimation(anim);
            node->setAnimation(anim);
        }

        auto skel = Mesh::extractSkeleton(mesh, scene);
        meshObj->setSkeleton(skel);

        node->setMesh(meshObj);
        node->meshPath = filePath;
        node->meshIndex = 0;

        auto m = scene->mMaterials[mesh->mMaterialIndex];
        auto dir = QFileInfo(filePath).absoluteDir().absolutePath();

        MeshMaterialData meshMat;
        MaterialHelper::extractMaterialData(m, dir, meshMat);
        auto mat = createMaterialFunc(meshObj, meshMat);
        if (!!mat) node->setMaterial(mat);

        return node;
    }

    auto node = _buildScene(scene, scene->mRootNode, SceneNodePtr(), filePath, createMaterialFunc);
    node->setAttached(false); // root of object shouldnt be attached

    // extract animations and add them one by one
    // todo: use relative path from scene root (Nic)
    auto anims = Mesh::extractAnimations(scene, filePath);
    for (auto animName : anims.keys()) {
        auto anim = Animation::createFromSkeletalAnimation(anims[animName]);
        node->addAnimation(anim);
        node->setAnimation(anim);
    }

    node->applyDefaultPose();

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

    return node;
}

}
