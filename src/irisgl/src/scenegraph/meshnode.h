#ifndef MESHNODE_H
#define MESHNODE_H

#include <QSharedPointer>
#include "../core/scenenode.h"
//#include "../graphics/mesh.h"
//#include "../graphics/material.h"

namespace iris
{

class Mesh;
class Material;

class MeshNode:public SceneNode
{
public:
    Mesh* mesh;

    QString meshPath;

    /**
     * This holds the index of the mesh
     */
    int meshIndex;

    QSharedPointer<Material> material;

    static QSharedPointer<iris::MeshNode> create()
    {
        return QSharedPointer<iris::MeshNode>(new iris::MeshNode());
    }

    /**
     * Some model contains multiple meshes with child-parent relationships. This funtion Loads the model as a scene
     * itself. If only one mesh is in the scene, it returns it as an MeshNode rather than a SceneNode. Otherwise, it
     * returns a null shared pointer.
     * @param path
     * @return
     */
    static QSharedPointer<iris::SceneNode> loadAsSceneFragment(QString path);

    void setMesh(QString source);
    void setMesh(Mesh* mesh);

    Mesh* getMesh();

    void setMaterial(QSharedPointer<iris::Material> material);
    QSharedPointer<iris::Material> getMaterial()
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

typedef QSharedPointer<iris::MeshNode> MeshNodePtr;


}

#endif // MESHNODE_H
