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


OrbitalCameraController::OrbitalCameraController()
{
    distFromPivot = 15;
    rotationSpeed = 1.f / 10.f;
}

QSharedPointer<iris::CameraNode>  OrbitalCameraController::getCamera()
{
    return camera;
}

/**
 * Calculates the pivot location and its yaw and pitch
 */
void OrbitalCameraController::setCamera(QSharedPointer<iris::CameraNode>  cam)
{
    this->camera = cam;

    //calculate the location of the pivot
    auto viewVec = cam->getLocalRot().rotatedVector(QVector3D(0,0,-1));//default forward is -z
    pivot = cam->getLocalPos() + (viewVec*distFromPivot);

    float roll;
    cam->getLocalRot().getEulerAngles(&pitch,&yaw,&roll);

    this->updateCameraRot();
}

void OrbitalCameraController::setRotationSpeed(float rotationSpeed)
{
    this->rotationSpeed = rotationSpeed;
}

void OrbitalCameraController::onMouseMove(int x,int y)
{
    if (rightMouseDown) {
        //rotate camera
        this->yaw += x * rotationSpeed;
        this->pitch += y * rotationSpeed;
    }

    if (middleMouseDown) {
        //translate camera
        float dragSpeed = 0.01f;
        auto dir = camera->getLocalRot().rotatedVector(QVector3D(x*dragSpeed,-y*dragSpeed,0));
        pivot += dir;
    }

    updateCameraRot();
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

    updateCameraRot();
}

void OrbitalCameraController::updateCameraRot()
{
    auto rot = QQuaternion::fromEulerAngles(pitch,yaw,0);
    auto localPos = rot.rotatedVector(QVector3D(0,0,1));

    camera->setLocalPos(pivot+(localPos*distFromPivot));
    camera->setLocalRot(rot);
    camera->update(0);


}
