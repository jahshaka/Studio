/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITORVRCONTROLLER_H
#define EDITORVRCONTROLLER_H

#include "../irisgl/src/irisglfwd.h"
#include "cameracontrollerbase.h"
#include <QMatrix4x4>

class VrHand
{
public:
	iris::ModelPtr handModel;
	QVector3D pos, scale;
	QQuaternion rot;
};

class EditorVrController : public CameraControllerBase
{
public:
	iris::MaterialPtr beamMaterial;

	iris::ModelPtr leftHandModel;
	iris::ModelPtr rightHandModel;

    iris::MeshPtr leftHandMesh;
    iris::MeshPtr rightHandMesh;

	iris::MeshPtr sphereMesh;

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

	EditorVrController(iris::ContentManagerPtr content);

    void setScene(iris::ScenePtr scene);

    void updateCameraRot();

    void update(float dt);

private:
    /*
     * Does a ray cast to the scene
     * Returns nearest object hit in the raycast
     */
    bool rayCastToScene(QMatrix4x4 handMatrix, iris::PickingResult& result);

	iris::SceneNodePtr getObjectRoot(iris::SceneNodePtr node);

	void submitHoveredNodes();
	void submitHoveredNode(iris::SceneNodePtr node);

	// for now this finds the first grab node
	iris::GrabNodePtr findGrabNode(iris::SceneNodePtr node);

	QMatrix4x4 calculateHandMatrix(iris::VrDevice* device, int handIndex);

	QMatrix4x4 getBeamOffset();
	float turnSpeed;
};

#endif // EDITORVRCONTROLLER_H
