/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "viewernode.h"
#include "../scenegraph/scene.h"
#include "../scenegraph/scenenode.h"
#include "../graphics/mesh.h"
#include "../materials/defaultmaterial.h"
#include "../graphics/texture2d.h"
#include "../graphics/renderitem.h"
#include "../graphics/renderlist.h"
#include "../vr/vrdevice.h"
#include "../vr/vrmanager.h"

namespace iris
{

ViewerNode::ViewerNode()
{
    this->sceneNodeType = SceneNodeType::Viewer;
    //this->headModel = Mesh::loadMesh(":/assets/models/head2.obj");
    //this->material = ViewerMaterial::create();
    //this->material->setTexture(Texture2D::load(":/assets/models/head.png"));

    this->setViewScale(1.0f);

    renderItem = new RenderItem();
    renderItem->type = RenderItemType::Mesh;
    //renderItem->material = this->material;
    //renderItem->mesh = headModel;

    auto cube = Mesh::loadMesh(":/assets/models/cube.obj");

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
    this->scale = QVector3D(scale, scale, scale);
}

float ViewerNode::getViewScale()
{
    return this->viewScale;
}

void ViewerNode::submitRenderItems()
{
    if( !visible)
        return;

    //renderItem->worldMatrix = this->globalTransform;
    //scene->geometryRenderList->add(renderItem);

    /*
    auto device = VrManager::getDefaultDevice();

    QMatrix4x4 world;
    world.setToIdentity();
    world.translate(device->getHandPosition(0));
    world.rotate(device->getHandRotation(0));
    world.scale(0.05f);
    leftHandenderItem->worldMatrix = this->globalTransform * world;

    world.setToIdentity();
    world.translate(device->getHandPosition(1));
    world.rotate(device->getHandRotation(1));
    world.scale(0.05f);
    rightHandRenderItem->worldMatrix = this->globalTransform * world;

    scene->geometryRenderList->add(leftHandenderItem);
    scene->geometryRenderList->add(rightHandRenderItem);
    */
}

SceneNodePtr ViewerNode::createDuplicate()
{
	auto viewer = iris::ViewerNode::create();

	viewer->setViewScale(this->viewScale);

	return viewer;
}

ViewerNodePtr ViewerNode::create()
{
    return ViewerNodePtr(new ViewerNode());
}

}
