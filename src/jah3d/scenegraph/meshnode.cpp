#include "meshnode.h"
#include "../graphics/mesh.h"

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

void MeshNode::setMaterial(QSharedPointer<Material> material)
{
    this->material = material;
}

}
