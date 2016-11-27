#ifndef MESHNODE_H
#define MESHNODE_H

#include <QSharedPointer>
#include "../core/scenenode.h"
//#include "../graphics/mesh.h"
//#include "../graphics/material.h"

namespace jah3d
{

class Mesh;
class Material;

class MeshNode:public SceneNode
{
public:
    Mesh* mesh;
    QString meshPath;

    QSharedPointer<Material> material;

    static QSharedPointer<jah3d::MeshNode> create()
    {
        return QSharedPointer<jah3d::MeshNode>(new jah3d::MeshNode());
    }

    /**
     * Some model contains multiple meshes with child-parent relationships. This funtion Loads the model as a scene
     * itself. If only one mesh is in the scene, it returns it as an MeshNode rather than a SceneNode. Otherwise, it
     * returns a null shared pointer.
     * @param path
     * @return
     */
    static QSharedPointer<jah3d::SceneNode> loadAsSceneFragment(QString path);

    void setMesh(QString source);
    void setMesh(Mesh* mesh);

    Mesh* getMesh();

    void setMaterial(QSharedPointer<jah3d::Material> material);
    QSharedPointer<jah3d::Material> getMaterial()
    {
        return material;
    }

private:
    MeshNode()
    {
        mesh = nullptr;
        sceneNodeType = SceneNodeType::Mesh;
    }
};

typedef QSharedPointer<jah3d::MeshNode> MeshNodePtr;


}

#endif // MESHNODE_H
