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
    camera = cam;

    lookSpeed = 200;
    linearSpeed = 1;
}

CameraNodePtr EditorCameraController::getCamera()
{
    return camera;
}

void EditorCameraController::setCamera(CameraNodePtr cam)
{
    this->camera = cam;
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

void EditorCameraController::onFrame(float dt)
{
    /*
    if(rightMouseButtonAction->isActive())
    {

        camera->tilt((mouseYAxis->value() * lookSpeed) * dt);
        camera->pan((mouseXAxis->value() * lookSpeed) * dt, QVector3D(0,1,0));
    }

    if(middleMouseButtonAction->isActive())
    {
        auto panSpeed = 1.0f;

        camera->translate(QVector3D(-mouseXAxis->value()*panSpeed,-mouseYAxis->value()*panSpeed,0));
        //qDebug()<<"pan";
    }

    QVector3D upVector(0,1,0);
    QVector3D viewVector = camera->viewCenter() - camera->position();
    auto x = QVector3D::crossProduct(viewVector, upVector).normalized();
    //auto z = viewVector.normalized();
    auto z = QVector3D::crossProduct(upVector,x).normalized();

    camera->translateWorld(txAxis->value()*x*linearSpeed);
    camera->translateWorld(tyAxis->value()*z*linearSpeed);
    //camera->translateWorld(QVector3D(txAxis->value()*linearSpeed*x,0,tyAxis->value()*linearSpeed*z));
    */
}
