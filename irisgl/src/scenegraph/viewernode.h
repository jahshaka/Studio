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
#include "../scenegraph/scenenode.h"

namespace iris
{

class RenderItem;
class Mesh;
class ViewerMaterial;

class ViewerNode : public SceneNode
{
    float viewScale;
    ViewerNode();

	// viewer-specific properties about how it controls in vr mode`
	bool allowMovement;
	bool allowPicking;
	bool showHands;

public:

	bool isMovementAllowed()
	{
		return allowMovement;
	}

	void setMovementAllowed(bool allow)
	{
		this->allowMovement = allow;
	}

	bool isPickingAllowed()
	{
		return allowPicking;
	}

	void setPickingAllowed(bool pickingAllowed)
	{
		this->allowPicking = pickingAllowed;
	}

	void setShowHands(bool show)
	{
		this->showHands = show;
	}

	bool getShowHands()
	{
		return this->showHands;
	}

    void setViewScale(float scale);
    float getViewScale();

    static ViewerNodePtr create();

    void submitRenderItems() override;

	SceneNodePtr createDuplicate() override;

    RenderItem* renderItem;
    RenderItem* leftHandenderItem;
    RenderItem* rightHandRenderItem;

    ~ViewerNode();
};

}

#endif // VIEWERNODE_H
