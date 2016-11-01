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
    QSharedPointer<Material> material;

    static QSharedPointer<jah3d::MeshNode> create();

    void setMesh(QString source);
    void setMesh(Mesh* mesh);

private:
    MeshNode()
    {

    }
};

typedef QSharedPointer<jah3d::MeshNode> MeshNodePtr;


}

#endif // MESHNODE_H
