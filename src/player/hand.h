#ifndef HAND_H
#define HAND_H

#include "playervrcontroller.h"
#include "../irisgl/src/irisglfwd.h"

class PlayerVrController;

class Hand
{
protected:
	PlayerVrController* controller;

	iris::ScenePtr scene;
	iris::CameraNodePtr camera;
	iris::ViewerNodePtr viewer;
	btRigidBody* activeRigidBody = nullptr;

	QVector3D rightPos, rightScale;
	QQuaternion rightRot;

	QMatrix4x4 rightNodeOffset;
	QMatrix4x4 rightHandMatrix;

	class btGeneric6DofConstraint* m_pickedConstraint = nullptr;
	int	m_savedState;
	btVector3 m_oldPickingPos;
	btVector3 m_hitPos;
	btScalar m_oldPickingDist;
	QVector3D rightHandPickOffset;
public:
	Hand(PlayerVrController* controller) { this->controller = controller; }
	virtual void update(float dt) {}
	virtual void loadAssets(iris::ContentManagerPtr content) {}
	virtual void init(iris::ScenePtr scene, iris::CameraNodePtr cam, iris::ViewerNodePtr viewer);
	void setCamera(iris::CameraNodePtr camera);
};

class LeftHand : public Hand
{
public:
	LeftHand(PlayerVrController* controller):
		Hand(controller)
	{
	}
};

class RightHand : public Hand
{
public:
	RightHand(PlayerVrController* controller) :
		Hand(controller)
	{
	}

	void update(float dt);
	void loadAssets(iris::ContentManagerPtr content);
	void submitItemsToScene();

	float hoverDist;
	iris::MeshPtr sphereMesh;
	iris::MeshPtr beamMesh;
	iris::MaterialPtr beamMaterial;
	iris::MeshPtr handMesh;
	iris::ModelPtr handModel;
	iris::MaterialPtr handMaterial;
	iris::SceneNodePtr hoveredNode;
	iris::SceneNodePtr grabbedNode;
};


#endif // HAND_H