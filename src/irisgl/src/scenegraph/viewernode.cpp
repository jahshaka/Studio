#include "viewernode.h"
#include "../core/scene.h"
#include "../core/scenenode.h"
#include "../graphics/mesh.h"
#include "../materials/viewermaterial.h"
#include "../graphics/texture2d.h"
#include "../graphics/renderitem.h"

namespace iris
{

ViewerNode::ViewerNode()
{
    this->sceneNodeType = SceneNodeType::Viewer;
    this->headModel = Mesh::loadMesh(":/assets/models/head.obj");
    this->material = ViewerMaterial::create();
    this->material->setTexture(Texture2D::load(":/assets/models/head.png"));

    this->viewScale = 5.0f;

    renderItem = new RenderItem();
    renderItem->type = RenderItemType::Mesh;
    renderItem->matrial = this->material;
    renderItem->mesh = headModel;
}

ViewerNode::~ViewerNode()
{
    delete renderItem;
}

void ViewerNode::setViewScale(float scale)
{
    this->viewScale = scale;
}

float ViewerNode::getViewScale()
{
    return this->viewScale;
}

void ViewerNode::submitRenderItems()
{
    renderItem->worldMatrix = this->globalTransform;
    scene->geometryRenderList.append(renderItem);
}

ViewerNodePtr ViewerNode::create()
{
    return ViewerNodePtr(new ViewerNode());
}

}
