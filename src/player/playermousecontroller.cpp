/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "playermousecontroller.h"
#include "../editor/animationpath.h"
#include "../core/keyboardstate.h"
#include <irisgl/SceneGraph.h>
#include <irisgl/Vr.h>
#include <irisgl/Physics.h>


void PlayerMouseController::setViewer(iris::ViewerNodePtr &value)
{
    viewer = value;
}

void PlayerMouseController::start()
{
	// reset viewer since a new one could be added since scene
	// was set and playing
	this->setViewer(scene->getActiveVrViewer());

	if (!!viewer)
		viewer->hide();

    // capture cam transform
    camPos = camera->getLocalPos();
    camRot = camera->getLocalRot();

	// capture yaw and pitch
	float roll;
	camera->getLocalRot().getEulerAngles(&pitch, &yaw, &roll);
}

void PlayerMouseController::end()
{
    //clearViewer();
	if (!!viewer)
		viewer->show();

	if (shouldRestoreCameraTransform) {
		// restore cam transform
		camera->setLocalPos(camPos);
		camera->setLocalRot(camRot);
	}
	camera->update(0);
}

void PlayerMouseController::clearViewer()
{
    viewer->show();
    viewer.clear();
}

void PlayerMouseController::setRestoreCameraTransform(bool shouldRestore)
{
	this->shouldRestoreCameraTransform = shouldRestore;
}

void PlayerMouseController::onMouseMove(int dx, int dy)
{
    if(rightMouseDown) {
        this->yaw += dx/10.0f;
        this->pitch += dy/10.0f;
    }
    updateCameraTransform();
}

void PlayerMouseController::onMouseWheel(int delta)
{

}

void PlayerMouseController::updateCameraTransform()
{
	if (!!viewer) {
		camera->setLocalPos(viewer->getGlobalPosition());
		auto viewMat = viewer->getGlobalTransform().normalMatrix();
		QQuaternion rot = QQuaternion::fromRotationMatrix(viewMat);
		camera->setLocalRot(rot * QQuaternion::fromEulerAngles(pitch, yaw, 0));
		//camera->setLocalRot(QQuaternion::fromEulerAngles(pitch,yaw,0));
	}
	else {
		camera->setLocalRot(QQuaternion::fromEulerAngles(pitch, yaw, 0));
	}
    camera->update(0);
}

void PlayerMouseController::captureYawPitchRollFromCamera()
{
	// capture yaw and pitch
	float roll; // roll not used
	camera->getLocalRot().getEulerAngles(&pitch, &yaw, &roll);
}

PlayerMouseController::PlayerMouseController()
{
    yaw = 0;
    pitch = 0;
}

void PlayerMouseController::setCamera(iris::CameraNodePtr cam)
{
	camera = cam;
}

void PlayerMouseController::setScene(iris::ScenePtr scene)
{
	this->scene = scene;
	this->setViewer(scene->getActiveVrViewer());
}

void PlayerMouseController::update(float dt)
{
	auto linearSpeed = 10 * dt;
	if (!_isPlaying)
		return;

	const QVector3D upVector(0, 1, 0);
	auto forwardVector = camera->getLocalRot().rotatedVector(QVector3D(0, 0, -1));
	auto x = QVector3D::crossProduct(forwardVector, upVector).normalized();
	auto z = QVector3D::crossProduct(upVector, x).normalized();

	if (!viewer) {
		auto camPos = camera->getLocalPos();
		// left
		if (KeyboardState::isKeyDown(Qt::Key_Left))
			camPos -= x * linearSpeed;

		// right
		if (KeyboardState::isKeyDown(Qt::Key_Right))
			camPos += x * linearSpeed;

		// up
		if (KeyboardState::isKeyDown(Qt::Key_Up))
			camPos += z * linearSpeed;

		// down
		if (KeyboardState::isKeyDown(Qt::Key_Down))
			camPos -= z * linearSpeed;

		camera->setLocalPos(camPos);

		updateCameraTransform();
	}
	else {
		//auto vrDevice = iris::VrManager::getDefaultDevice();

		float dirX = 0;
		float dirY = 0;
		if (KeyboardState::isKeyDown(Qt::Key_Left))
			dirX -= linearSpeed;

		// right
		if (KeyboardState::isKeyDown(Qt::Key_Right))
			dirX += linearSpeed;

		// up
		if (KeyboardState::isKeyDown(Qt::Key_Up))
			dirY -= linearSpeed;

		// down
		if (KeyboardState::isKeyDown(Qt::Key_Down))
			dirY += linearSpeed;


		// lock rot to yaw so user is always right side up
		auto yawRot = QQuaternion::fromEulerAngles(0, yaw, 0);
		viewer->setLocalRot(yawRot);

		// keyboard movement
		const QVector3D upVector(0, 1, 0);
		//not giving proper rotation when not in debug mode
		//apparently i need to normalize the head rotation quaternion
		auto rot = yawRot;
		rot.normalize();
		auto forwardVector = rot.rotatedVector(QVector3D(0, 0, -1));
		auto x = QVector3D::crossProduct(forwardVector, upVector).normalized();
		auto z = QVector3D::crossProduct(upVector, x).normalized();

		auto newDir = rot.rotatedVector(QVector3D(dirX, 0, dirY)) * 10;
		scene->getPhysicsEnvironment()->setDirection(QVector2D(newDir.x(), newDir.z()));
	}
}

void PlayerMouseController::postUpdate(float dt)
{
	if (!!viewer) {
		updateCameraTransform();
		//camera->setLocalTransform(viewer->getGlobalTransform());
	}
}