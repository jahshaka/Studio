/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "viewercontroller.h"
#include "cameracontrollerbase.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "../irisgl/src/scenegraph/viewernode.h"


void ViewerCameraController::setViewer(iris::ViewerNodePtr &value)
{
    viewer = value;
}

void ViewerCameraController::start()
{
    viewer->hide();

    // capture cam transform
    camPos = camera->getLocalPos();
    camRot = camera->getLocalRot();
}

void ViewerCameraController::end()
{
    //clearViewer();
	viewer->hide();

    // restore cam transform
    camera->setLocalPos(camPos);
    camera->setLocalRot(camRot);
    camera->update(0);
}

void ViewerCameraController::clearViewer()
{
    viewer->show();
    viewer.clear();
}

void ViewerCameraController::onMouseMove(int dx, int dy)
{
    if(rightMouseDown) {
        this->yaw += dx/10.0f;
        this->pitch += dy/10.0f;
    }
    updateCameraTransform();
}

void ViewerCameraController::onMouseWheel(int delta)
{

}

void ViewerCameraController::updateCameraTransform()
{
    camera->setLocalPos(viewer->getGlobalPosition());

    auto viewMat = viewer->getGlobalTransform().normalMatrix();
    QQuaternion rot = QQuaternion::fromRotationMatrix(viewMat);
    camera->setLocalRot(rot * QQuaternion::fromEulerAngles(pitch,yaw,0));
    //camera->setLocalRot(QQuaternion::fromEulerAngles(pitch,yaw,0));
    camera->update(0);
}

ViewerCameraController::ViewerCameraController()
{
    yaw = 0;
    pitch = 0;
}

void ViewerCameraController::update(float dt)
{
    updateCameraTransform();
}

