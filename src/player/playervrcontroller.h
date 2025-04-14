/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef PLAYERVRCONTROLLER_H
#define PLAYERVRCONTROLLER_H

#include <QMatrix4x4>
#include "../irisgl/src/irisglfwd.h"
#include "../editor/cameracontrollerbase.h"
#include "irisgl/src/physics/environment.h"
#include "irisgl/src/physics/physicshelper.h"

#include "irisgl/src/bullet3/src/btBulletDynamicsCommon.h"
#include "hand.h"

class VrHand
{
public:
	iris::ModelPtr handModel;
	QVector3D pos, scale;
	QQuaternion rot;
};

class PlayerVrController : public CameraControllerBase
{
	friend class Hand;

	bool _isPlaying;
public:
	iris::ModelPtr leftHandModel;
	iris::ModelPtr rightHandModel;

    iris::MeshPtr leftHandMesh;
    iris::MeshPtr rightHandMesh;

    iris::RenderItem* leftHandRenderItem;
    iris::RenderItem* rightHandRenderItem;

    iris::MeshPtr beamMesh;
    iris::RenderItem* leftBeamRenderItem;
    iris::RenderItem* rightBeamRenderItem;

    iris::VrDevice* vrDevice;

	iris::ContentManagerPtr content;

	QVector3D leftPos, leftScale, rightPos, rightScale;
	QQuaternion leftRot, rightRot;
    iris::SceneNodePtr leftPickedNode;
    iris::SceneNodePtr rightPickedNode;

	iris::SceneNodePtr leftHoveredNode;
	iris::SceneNodePtr rightHoveredNode;

    // These are the offsets from the controllers to the
    // selected object
    QMatrix4x4 leftNodeOffset;
    QMatrix4x4 rightNodeOffset;

    //iris::SceneNodePtr tracker;
    iris::ScenePtr scene;

	iris::MaterialPtr fresnelMat;

	PlayerVrController();

	void setCamera(iris::CameraNodePtr cam);

	void loadAssets(iris::ContentManagerPtr content);

    void setScene(iris::ScenePtr scene);

    void updateCameraRot();

	void start() override;
	void update(float dt) override;
	void postUpdate(float dt) override;

	void setPlayState(bool playState) { _isPlaying = playState; }

    /*
     * Does a ray cast to the scene
     * Returns nearest object hit in the raycast
     */
    bool rayCastToScene(QMatrix4x4 handMatrix, iris::PickingResult& result);

	iris::SceneNodePtr getObjectRoot(iris::SceneNodePtr node);

	// for now this finds the first grab node
	iris::GrabNodePtr findGrabNode(iris::SceneNodePtr node);

private:
	void submitHoveredNodes();
	void submitHoveredNode(iris::SceneNodePtr node);

	

	float turnSpeed;

	Hand* leftHand;
	Hand* rightHand;

	// PHYSICS STUFF
	btRigidBody *activeRigidBody;

	//testing
	//class btTypedConstraint* m_pickedConstraint;
	class btGeneric6DofConstraint* m_pickedConstraint;
	int	m_savedState;
	btVector3 m_oldPickingPos;
	btVector3 m_hitPos;
	btScalar m_oldPickingDist;
	QVector3D rightHandPickOffset;
};

#endif // EDITORVRCONTROLLER_H
