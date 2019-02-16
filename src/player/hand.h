#ifndef HAND_H
#define HAND_H

#include "playervrcontroller.h"
#include "../irisgl/src/irisglfwd.h"

class Hand
{
public:
	virtual void update(float dt) {}
	virtual void loadAssets() {}
};

class LeftHand : public Hand
{

};

class RightHand : public Hand
{
public:
	RightHand();
	void update(float dt);
	void loadAssets();

	iris::MeshPtr handMesh;
	iris::MeshPtr beamMesh;
	iris::MaterialPtr handMaterial;
	iris::SceneNodePtr hoverNode;
	iris::SceneNodePtr grabbedNode;
};


#endif // HAND_H