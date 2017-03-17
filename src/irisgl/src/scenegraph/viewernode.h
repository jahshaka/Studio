/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef VIEWERNODE_H
#define VIEWERNODE_H

#include "../irisglfwd.h"
#include "../core/scenenode.h"

namespace iris
{

class RenderItem;
class Mesh;
class ViewerMaterial;

class ViewerNode : public SceneNode
{
    float viewScale;
    ViewerNode();

public:
    Mesh* headModel;
    ViewerMaterialPtr material;

    void setViewScale(float scale);
    float getViewScale();

    static ViewerNodePtr create();

    void submitRenderItems() override;

    RenderItem* renderItem;
    RenderItem* leftHandenderItem;
    RenderItem* rightHandRenderItem;

    ~ViewerNode();
};

}

#endif // VIEWERNODE_H
