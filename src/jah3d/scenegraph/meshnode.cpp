#include "meshnode.h"
#include "../graphics/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/mesh.h"
#include "assimp/matrix4x4.h"
#include "assimp/vector3.h"
#include "assimp/quaternion.h"

#include "../graphics/vertexlayout.h"
#include "../materials/defaultmaterial.h"

namespace jah3d
{

void MeshNode::setMesh(QString source)
{
    mesh = Mesh::loadMesh(source);
}

void MeshNode::setMesh(Mesh* mesh)
{
    this->mesh = mesh;
}

Mesh* MeshNode::getMesh()
{
    return mesh;
}

void MeshNode::setMaterial(QSharedPointer<Material> material)
{
    this->material = material;
}

/**
 * Recursively builds a SceneNode/MeshNode heirarchy from the aiScene of the loaded model
 * todo: read and apply material data
 * @param scene
 * @param node
 * @return
 */
QSharedPointer<jah3d::SceneNode> _buildScene(const aiScene* scene,aiNode* node)
{
    QSharedPointer<jah3d::SceneNode> sceneNode;// = QSharedPointer<jah3d::SceneNode>(new jah3d::SceneNode());

    //if this node only has one child then make sceneNode a meshnode and add mesh to it
    if(node->mNumMeshes==1)
    {
        auto meshNode = jah3d::MeshNode::create();
        auto mesh = scene->mMeshes[node->mMeshes[0]];

        //objects like Bezier curves have no vertex positions in the aiMesh
        //aside from that, jah3d currently only renders meshes
        if(mesh->HasPositions())
        {
            meshNode->setMesh(new Mesh(mesh,VertexLayout::createMeshDefault()));
            meshNode->name = QString(mesh->mName.C_Str());

            //apply material
            auto mat = jah3d::DefaultMaterial::create();
            meshNode->setMaterial(mat);

            //todo
            /*
            if(mesh->mMaterialIndex>=0)
            {
                auto m = scene->mMaterials[mesh->mMaterialIndex];
                m->
            }
            */
        }
        sceneNode = meshNode;
    }
    else
    {
        //otherwise, add meshes as child nodes
        sceneNode = QSharedPointer<jah3d::SceneNode>(new jah3d::SceneNode());

        for(auto i=0;i<node->mNumMeshes;i++)
        {
            auto mesh = scene->mMeshes[node->mMeshes[i]];

            auto meshNode = jah3d::MeshNode::create();
            meshNode->name = QString(mesh->mName.C_Str());

            meshNode->setMesh(new Mesh(mesh,VertexLayout::createMeshDefault()));
            sceneNode->addChild(meshNode);

            //apply material
            meshNode->setMaterial(jah3d::DefaultMaterial::create());
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
    sceneNode->rot = QQuaternion(rot.w,rot.x,rot.y,rot.z);//not sure if this is correct

    for(auto i=0;i<node->mNumChildren;i++)
    {
        auto child = _buildScene(scene,node->mChildren[i]);
        sceneNode->addChild(child);
    }

    return sceneNode;
}

QSharedPointer<jah3d::SceneNode> MeshNode::loadAsSceneFragment(QString filePath)
{
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(filePath.toStdString().c_str(),aiProcessPreset_TargetRealtime_Fast);

    if(!scene)
        return QSharedPointer<jah3d::MeshNode>(nullptr);

    if(scene->mNumMeshes==0)
        return QSharedPointer<jah3d::MeshNode>(nullptr);

    if(scene->mNumMeshes==1)
    {
        auto node = jah3d::MeshNode::create();
        node->setMesh(new Mesh(scene->mMeshes[0],VertexLayout::createMeshDefault()));
        return node;
    }

    auto node = _buildScene(scene,scene->mRootNode);

    return node;
}

}
