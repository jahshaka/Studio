/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ORBITALCAMERACONTROLLER_H
#define ORBITALCAMERACONTROLLER_H

#include "../../irisgl/src/irisglfwd.h"
#include <QPoint>
#include <QVector3D>
#include <QSharedPointer>
//#include "../irisgl/src/core/scenenode.h"
//#include "../irisgl/src/scenegraph/cameranode.h"
#include "cameracontrollerbase.h"

//class CameraPtr;
namespace iris
{
    class CameraNode;
}

class SceneViewWidget;
class OrbitalCameraController:public CameraControllerBase
{
public:
    float lookSpeed;
    float linearSpeed;

    float yaw;
    float pitch;

	float targetYaw;
	float targetPitch;
	float lerpSpeed;

	bool previewMode;

    float rotationSpeed;

	SceneViewWidget* sceneWidget;

    QVector3D pivot;
    float distFromPivot;

	iris::CameraNodePtr camera;

    OrbitalCameraController(SceneViewWidget* sceneWidget);

    iris::CameraNodePtr  getCamera();
    void setRotationSpeed(float rotationSpeed);
    void setCamera(iris::CameraNodePtr  cam) override;

    void onMouseMove(int x,int y) override;
    void onMouseWheel(int delta) override;

	void onKeyPressed(Qt::Key key);
	void onKeyReleased(Qt::Key key);

	void update(float dt) override;

	void focusOnNode(iris::SceneNodePtr sceneNode);

    void updateCameraRot();

	bool canLeftMouseDrag();
};

#endif // ORBITALCAMERACONTROLLER_H
