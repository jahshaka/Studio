#ifndef MESHNODE_H
#define MESHNODE_H

#include "../core/scenenode.h"
#include "../graphics/mesh.h"
#include "../graphics/material.h"

namespace jah3d
{

typedef QSharedPointer<MeshNode> MeshNodePtr;

class MeshNode:SceneNode
{
    MeshPtr mesh;
    MaterialPtr material;

public:
    MeshNodePtr create();

    void setMesh(QString source)
    {
        mesh = Mesh::loadMesh(source);
    }

    void setMesh(MeshPtr mesh)
    {
        this->mesh = mesh;
    }

private:
    MeshNode()
    {

    }
};

}

#endif // MESHNODE_H
