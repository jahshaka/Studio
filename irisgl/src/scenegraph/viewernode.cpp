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

    this->setViewScale(1.0f);
    exportable = false;
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
