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
#include <irisgl/SceneGraph.h>


void PlayerMouseController::setViewer(iris::ViewerNodePtr &value)
{
    viewer = value;
}

void PlayerMouseController::start()
{
    viewer->hide();

    // capture cam transform
    camPos = camera->getLocalPos();
    camRot = camera->getLocalRot();
}

void PlayerMouseController::end()
{
    //clearViewer();
	viewer->hide();

    // restore cam transform
    camera->setLocalPos(camPos);
    camera->setLocalRot(camRot);
    camera->update(0);
}

void PlayerMouseController::clearViewer()
{
    viewer->show();
    viewer.clear();
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
    camera->setLocalPos(viewer->getGlobalPosition());

    auto viewMat = viewer->getGlobalTransform().normalMatrix();
    QQuaternion rot = QQuaternion::fromRotationMatrix(viewMat);
    camera->setLocalRot(rot * QQuaternion::fromEulerAngles(pitch,yaw,0));
    //camera->setLocalRot(QQuaternion::fromEulerAngles(pitch,yaw,0));
    camera->update(0);
}

PlayerMouseController::PlayerMouseController()
{
    yaw = 0;
    pitch = 0;
}

void PlayerMouseController::update(float dt)
{
    updateCameraTransform();
}

