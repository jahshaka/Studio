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
#include "../jah3d/core/scenenode.h"
#include "../jah3d/scenegraph/cameranode.h"

using namespace jah3d;

EditorCameraController::EditorCameraController(CameraNodePtr cam)
{
    setCamera(cam);

    lookSpeed = 200;
    linearSpeed = 1;

    yaw = 0;
    pitch = 0;
}

CameraNodePtr EditorCameraController::getCamera()
{
    return camera;
}

void EditorCameraController::setCamera(CameraNodePtr cam)
{
    this->camera = cam;
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

void EditorCameraController::onMouseDragged(int x,int y)
{
    //rotate camera accordingly
    this->yaw += x/10.0f;
    this->pitch += y/10.0f;

    /*
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

//returns ray direction
//stolen from qtlabs 3d editor
QVector3D EditorCameraController::unproject(int viewPortWidth, int viewPortHeight,QPoint pos)
{
    auto projectionMatrix = camera->projMatrix;
    auto viewMatrix = camera->viewMatrix;

    float x = ((2.0f * pos.x()) / viewPortWidth) - 1.0f;
    float y = 1.0f - ((2.0f * pos.y()) / viewPortHeight);

    QVector4D ray = projectionMatrix.inverted() * QVector4D(x, y, -1.0f, 1.0f);
    ray.setZ(-1.0f);
    ray.setW(0.0f);
    ray = viewMatrix.inverted() * ray;
    return ray.toVector3D().normalized();
}

void EditorCameraController::updateCameraRot()
{
    //QQuaternion yawQuat = QQuaternion::fromEulerAngles(0,yaw,0);
    //QQuaternion pitchQuat = QQuaternion::fromEulerAngles(pitch,0,0);

    //camera->rot = yawQuat*pitchQuat;

    camera->rot = QQuaternion::fromEulerAngles(pitch,yaw,0);
}
