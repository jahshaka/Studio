#include "viewernode.h"
#include "../core/scenenode.h"
#include "../graphics/mesh.h"
#include "../materials/viewermaterial.h"
#include "../graphics/texture2d.h"

namespace iris
{

ViewerNode::ViewerNode()
{
    this->sceneNodeType = SceneNodeType::Viewer;
    this->headModel = Mesh::loadMesh(":/assets/models/head.obj");
    this->material = ViewerMaterial::create();
    this->material->setTexture(Texture2D::load(":/assets/models/head.png"));
}

ViewerNodePtr ViewerNode::create()
{
    return ViewerNodePtr(new ViewerNode());
}

}
