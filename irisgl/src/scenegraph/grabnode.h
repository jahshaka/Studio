#pragma once

#include "scenenode.h"
#include "scene.h"
#include "../vr/handpose.h"

namespace iris
{
class HandPose;

class GrabNode : public SceneNode
{
public:
	HandPose * handPose;

	GrabNode();
};

}