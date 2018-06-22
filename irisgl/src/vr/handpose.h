#pragma once

namespace iris
{

enum class HandPoseType
{
	Grab,
	Pinch
};

class HandPose
{
protected:
	HandPoseType poseType;
public:
	HandPoseType getPoseType()
	{
		return poseType;
	}

	virtual ~HandPose()
	{

	}
};

class GrabHandPose : public HandPose
{
public:
	// this determines how wide the hand stretches
	float stretch;

	GrabHandPose();
};

}