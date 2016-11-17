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
#include <qmath.h>
#include <math.h>

using namespace jah3d;

EditorCameraController::EditorCameraController()
{
    lookSpeed = 200;
    linearSpeed = 1;

    yaw = 0;
    pitch = 0;
}

CameraNodePtr EditorCameraController::getCamera()
{
    return camera;
}

/**
 * Sets camera
 * Reduces the camera's orientation to just the yaw and pitch
 * http://stackoverflow.com/questions/2782647/how-to-get-yaw-pitch-and-roll-from-a-3d-vector/33790309#33790309
 */
void EditorCameraController::setCamera(CameraNodePtr cam)
{
    this->camera = cam;

    auto viewVec = cam->rot.rotatedVector(QVector3D(0,0,-1));//default forward is -z
    viewVec.normalize();

    float roll;
    cam->rot.getEulerAngles(&pitch,&yaw,&roll);

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

    if(middleMouseDown)
    {
        //translate camera
        float dragSpeed = 0.01f;
        auto dir = camera->rot.rotatedVector(QVector3D(x*dragSpeed,-y*dragSpeed,0));
        camera->pos += dir;

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

void EditorCameraController::onMouseWheel(int delta)
{
    auto zoomSpeed = 0.01f;
    auto forward = camera->rot.rotatedVector(QVector3D(0,0,-1));
    camera->pos += forward*zoomSpeed*delta;
}

/*
 * tilt - does the tile
 */

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

/**
 * @brief EditorCameraController::updateCameraRot
 */
void EditorCameraController::updateCameraRot()
{
    //QQuaternion yawQuat = QQuaternion::fromEulerAngles(0,yaw,0);
    //QQuaternion pitchQuat = QQuaternion::fromEulerAngles(pitch,0,0);

    //camera->rot = yawQuat*pitchQuat;
    camera->rot = QQuaternion::fromEulerAngles(pitch,yaw,0);
    camera->update(0);
}
