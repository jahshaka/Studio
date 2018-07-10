/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QPoint>
#include <QVector3D>
#include <QSharedPointer>
#include <QtMath>

#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "cameracontrollerbase.h"
#include "orbitalcameracontroller.h"

#include "../core/settingsmanager.h"
#include "../widgets/sceneviewwidget.h"
#include "../editor/gizmo.h"

float lerp(float a, float b, float t)
{
	return a * (1 - t) + b * t;
}

OrbitalCameraController::OrbitalCameraController(SceneViewWidget* sceneWidget)
{
    distFromPivot = 15;
    rotationSpeed = 1.f / 10.f;

	previewMode = false;

	pitch = yaw = 0;
	targetPitch = targetYaw = 0;

	this->sceneWidget = sceneWidget;
}

iris::CameraNodePtr OrbitalCameraController::getCamera()
{
    return camera;
}

/**
 * Calculates the pivot location and its yaw and pitch
 */
void OrbitalCameraController::setCamera(iris::CameraNodePtr  cam)
{
    this->camera = cam;

    //calculate the location of the pivot
    auto viewVec = cam->getLocalRot().rotatedVector(QVector3D(0,0,-1));//default forward is -z
    pivot = cam->getLocalPos() + (viewVec*distFromPivot);

    float roll;
    cam->getLocalRot().getEulerAngles(&pitch,&yaw,&roll);
	targetYaw = yaw;
	targetPitch = pitch;

    this->updateCameraRot();
}

void OrbitalCameraController::setRotationSpeed(float rotationSpeed)
{
    this->rotationSpeed = rotationSpeed;
}

void OrbitalCameraController::onMouseMove(int x,int y)
{
	if (previewMode && (leftMouseDown || rightMouseDown)) {
		// in case lerping is still in progress, match the values with their targets
		yaw = targetYaw;
		pitch = targetPitch;
		this->yaw	+= x * rotationSpeed;
		this->pitch += y * rotationSpeed;

		// keep pitch and yaw in sync
		targetYaw = yaw;
		targetPitch = pitch;
	}
	else if (!previewMode && rightMouseDown) {
		// in case lerping is still in progress, match the values with their targets
		yaw = targetYaw;
		pitch = targetPitch;

		this->yaw	+= x * rotationSpeed;
		this->pitch += y * rotationSpeed;

		// keep pitch and yaw in sync
		targetYaw = yaw;
		targetPitch = pitch;
	}

    if (middleMouseDown ||
		canLeftMouseDrag()) {
        //translate camera
        float dragSpeed = 0.01f;
        auto dir = camera->getLocalRot().rotatedVector(QVector3D(x*dragSpeed,-y*dragSpeed,0));
        pivot += dir;
    }

    updateCameraRot();
}

bool OrbitalCameraController::canLeftMouseDrag()
{
	bool gizmoDragging = false;
	if (sceneWidget != nullptr) {
		auto gizmo = sceneWidget->getActiveGizmo();
		if (gizmo != nullptr) {
			if (gizmo->isDragging())
				gizmoDragging = true;
		}
	}

	return (leftMouseDown && // left mouse must be down
		settings->getValue("mouse_controls", "jahshaka").toString() == "jahshaka" && // left mouse to drag in jahshaka mouse mode
		!gizmoDragging); // cant pan while dragging gizmo
}

/**
 * Zooms camera in
 */
void OrbitalCameraController::onMouseWheel(int delta)
{
    //qDebug()<<delta;
    auto zoomSpeed = 0.01f;
    distFromPivot += -delta * zoomSpeed;
    //distFromPivot += delta;

    //todo: remove magic numbers
    if(distFromPivot<0)
        distFromPivot = 0;



	if (camera->projMode == iris::CameraProjection::Orthogonal) {
		if (distFromPivot <= 0.1f) distFromPivot = .01f;
		camera->setOrthagonalZoom(distFromPivot);
	}else{
		updateCameraRot();

	}

}

void OrbitalCameraController::update(float dt)
{
	yaw = lerp(yaw, targetYaw, 0.8);
	pitch = lerp(pitch, targetPitch, 0.8);

	updateCameraRot();
}

void OrbitalCameraController::onKeyPressed(Qt::Key key)
{
	
}

void OrbitalCameraController::onKeyReleased(Qt::Key key)
{

}

void OrbitalCameraController::keyReleaseEvent(QKeyEvent *event)
{
	// checks if crtl is being pressed
	if (event->modifiers() == Qt::ControlModifier)
	{
		// gets the key being pressed
		switch (event->key())
		{
		case Qt::Key_X: // right?
			targetYaw = -90;
			targetPitch = 0;
			return;

		case Qt::Key_Y: // bottom
			targetYaw = 0;
			targetPitch = 90;
			return;

		case Qt::Key_Z: // back
			targetYaw = -180;
			targetPitch = 0;
			return;
		}
	}
	// gets the key being pressed
	switch (event->key())
	{
	case Qt::Key_X: // left?
		targetYaw = 90;
		targetPitch = 0;
		return;

	case Qt::Key_Y: // top
		targetYaw = 0;
		targetPitch = -90;
		return;

	case Qt::Key_Z: // front
		targetYaw = 0;
		targetPitch = 0;
		return;

	}

	onKeyReleased((Qt::Key)event->key());
}

void OrbitalCameraController::focusOnNode(iris::SceneNodePtr sceneNode)
{
	auto nodePos = sceneNode->getGlobalPosition();
	camera->lookAt(nodePos);
	distFromPivot = camera->getGlobalPosition().distanceToPoint(nodePos);
	this->setCamera(camera);
}

void OrbitalCameraController::updateCameraRot()
{
    auto rot = QQuaternion::fromEulerAngles(pitch,yaw,0);
    auto localPos = rot.rotatedVector(QVector3D(0,0,1));

    camera->setLocalPos(pivot+(localPos*distFromPivot));
    camera->setLocalRot(rot);
    camera->update(0);


}
