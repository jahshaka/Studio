#include "viewernode.h"
#include "../core/scene.h"
#include "../core/scenenode.h"
#include "../graphics/mesh.h"
#include "../materials/viewermaterial.h"
#include "../materials/defaultmaterial.h"
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
    renderItem->material = this->material;
    renderItem->mesh = headModel;

    auto cube = Mesh::loadMesh(":/assets/models/head.obj");

    leftHandenderItem = new RenderItem();
    leftHandenderItem->type = RenderItemType::Mesh;
    leftHandenderItem->material = DefaultMaterial::create();
    leftHandenderItem->mesh = cube;

    rightHandRenderItem = new RenderItem();
    rightHandRenderItem->type = RenderItemType::Mesh;
    rightHandRenderItem->material = DefaultMaterial::create();
    rightHandRenderItem->mesh = cube;
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

    scene->geometryRenderList.append(leftHandenderItem);
    scene->geometryRenderList.append(rightHandRenderItem);
}

ViewerNodePtr ViewerNode::create()
{
    return ViewerNodePtr(new ViewerNode());
}

}
