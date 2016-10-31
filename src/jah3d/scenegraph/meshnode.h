#ifndef MESHNODE_H
#define MESHNODE_H

#include "../core/scenenode.h"
//#include "../graphics/mesh.h"
#include "../graphics/material.h"

namespace jah3d
{

class Mesh;
class MeshNode;
typedef QSharedPointer<jah3d::MeshNode> MeshNodePtr;

class MeshNode:public jah3d::SceneNode
{
public:

    Mesh* mesh;
    MaterialPtr material;

    static MeshNodePtr create();

    void setMesh(QString source);
    void setMesh(Mesh* mesh);

private:
    MeshNode()
    {

    }
};

}

#endif // MESHNODE_H
