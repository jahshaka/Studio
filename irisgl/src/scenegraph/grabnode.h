#pragma once

#include "../irisglfwd.h"
#include "scenenode.h"
#include "scene.h"
#include "../vr/handpose.h"

namespace iris
{
class HandPose;

class GrabNode : public SceneNode
{
public:
	// this class owns the hand pose
	HandPose * handPose;

	GrabNode(HandPoseType poseType = HandPoseType::Grab);

	void setPose(HandPoseType poseType);

	static GrabNodePtr create() {
		return GrabNodePtr(new GrabNode());
	}
};

}