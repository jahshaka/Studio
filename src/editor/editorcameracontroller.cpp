/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "editorcameracontroller.h"
#include <QVector3D>
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include <qmath.h>
#include <math.h>
#include "../core/keyboardstate.h"
#include "../core/settingsmanager.h"
#include "../widgets/sceneviewwidget.h"
#include "../editor/gizmo.h"

using namespace iris;

EditorCameraController::EditorCameraController(SceneViewWidget* sceneWidget):
	CameraControllerBase()
{
    lookSpeed = 200;
    linearSpeed = 0.4f;

    yaw = 0;
    pitch = 0;

	this->sceneWidget = sceneWidget;
}

CameraNodePtr EditorCameraController::getCamera()
{
    return camera;
}

/**
 * Sets camera
 * Reduces the camera's orientation to just the yaw and pitch
 */
void EditorCameraController::setCamera(CameraNodePtr cam)
{
    this->camera = cam;

    auto viewVec = cam->getLocalRot().rotatedVector(QVector3D(0,0,-1));//default forward is -z
    viewVec.normalize();

    float roll;
    cam->getLocalRot().getEulerAngles(&pitch,&yaw,&roll);

    this->updateCameraRot();
}

QVector3D EditorCameraController::getPos()
{
    //return this->camera->position();
    return QVector3D();
}

void EditorCameraController::setLinearSpeed(float speed)
{
    linearSpeed = speed;
}

float EditorCameraController::getLinearSpeed()
{
    return linearSpeed;
}

void EditorCameraController::setLookSpeed(float speed)
{
    lookSpeed = speed;
}

float EditorCameraController::getLookSpeed()
{
    return lookSpeed;
}

/**
 * @brief rotates camera around the local x-axis
 * the angle is in degrees
 * pitch is restricted to the range of -90 and 90
 * todo: use global rotation in calculation
 */
void EditorCameraController::tilt(float angle)
{
    /*
    auto forward = camera->rot.rotatedVector(QVector3D(0,0,-1));
    auto up = QVector3D(0,1,0);

    auto side = QVector3D::crossProduct(forward,up);
    */

    pitch += angle;
    pitch = (pitch<-90?-90:(pitch>90?90:pitch));//clamp( pitch,-90,90)

}

/**
 * @brief rotates the camera around the up vector
 * @param angle
 */
void EditorCameraController::pan(float angle)
{
    //camera->rot = QQuaternion::fromAxisAndAngle(QVector3D(0,1,0),angle)*camera->rot;
    yaw += angle;
    //yaw = fmod(yaw,360);
}

/**
 *
 * @param x
 * @param y
 */
void EditorCameraController::onMouseMove(int x,int y)
{
    if(rightMouseDown)
    {
        //rotate camera
        this->yaw += x/10.0f;
        this->pitch += y/10.0f;
    }

    if(middleMouseDown || canLeftMouseDrag())
    {
        //translate camera
        float dragSpeed = 0.01f;
        auto dir = camera->getLocalRot().rotatedVector(QVector3D(x*dragSpeed,-y*dragSpeed,0));
        camera->setLocalPos( camera->getLocalPos() + dir);

        camera->update(0);//force calculation of global transform. find a better way to do this
    }

    /*
    //todo: world-space translation using keyboard
    QVector3D upVector(0,1,0);
    QVector3D viewVector = camera->viewCenter() - camera->position();
    auto x = QVector3D::crossProduct(viewVector, upVector).normalized();
    //auto z = viewVector.normalized();
    auto z = QVector3D::crossProduct(upVector,x).normalized();

    camera->translateWorld(txAxis->value()*x*linearSpeed);
    camera->translateWorld(tyAxis->value()*z*linearSpeed);
    */

    updateCameraRot();
}

bool EditorCameraController::canLeftMouseDrag()
{
	
	bool gizmoDragging = false;
	auto gizmo = sceneWidget->getActiveGizmo();
	if (gizmo != nullptr) {
		if (gizmo->isDragging())
			gizmoDragging = true;
	}

	return (leftMouseDown && // left mouse must be down
		settings->getValue("mouse_controls", "jahshaka").toString() == "jahshaka" && // left mouse to drag in jahshaka mouse mode
		!gizmoDragging); // cant pan while dragging gizmo
}

void EditorCameraController::onMouseWheel(int delta)
{
    auto zoomSpeed = 0.01f;
    auto forward = camera->getLocalRot().rotatedVector(QVector3D(0,0,-1));
    auto movement = camera->getLocalPos() + forward*zoomSpeed*delta;
    camera->setLocalPos(movement);
}

void EditorCameraController::onKeyPressed(Qt::Key key)
{

}

void EditorCameraController::onKeyReleased(Qt::Key key)
{
	if (key == Qt::Key_X) {
		//
	}
}

/**
 * @brief EditorCameraController::updateCameraRot
 */
void EditorCameraController::updateCameraRot()
{
    //QQuaternion yawQuat = QQuaternion::fromEulerAngles(0,yaw,0);
    //QQuaternion pitchQuat = QQuaternion::fromEulerAngles(pitch,0,0);

    //camera->rot = yawQuat*pitchQuat;
    camera->setLocalRot(QQuaternion::fromEulerAngles(pitch,yaw,0));
    camera->update(0);
}

void EditorCameraController::update(float dt)
{
    const QVector3D upVector(0, 1, 0);
    auto forwardVector = camera->getLocalRot().rotatedVector(QVector3D(0, 0, -1));
    auto x = QVector3D::crossProduct(forwardVector,upVector).normalized();
    auto z = QVector3D::crossProduct(upVector,x).normalized();

    auto camPos = camera->getLocalPos();
    // left
    if(KeyboardState::isKeyDown(Qt::Key_Left))
        camPos -= x * linearSpeed;

    // right
    if(KeyboardState::isKeyDown(Qt::Key_Right))
        camPos += x * linearSpeed;

    // up
    if(KeyboardState::isKeyDown(Qt::Key_Up))
        camPos += z * linearSpeed;

    // down
    if(KeyboardState::isKeyDown(Qt::Key_Down))
        camPos -= z * linearSpeed;

    camera->setLocalPos(camPos);
}
