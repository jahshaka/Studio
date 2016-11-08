/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITORCAMERACONTROLLER_H
#define EDITORCAMERACONTROLLER_H

#include <QPoint>
#include <QVector3D>
#include <QSharedPointer>
//#include "../jah3d/core/scenenode.h"
//#include "../jah3d/scenegraph/cameranode.h"

//class CameraPtr;
namespace jah3d
{
    class CameraNode;
}

class EditorCameraController
{
    //Q_OBJECT

    QSharedPointer<jah3d::CameraNode> camera;

    float lookSpeed;
    float linearSpeed;

    float yaw;
    float pitch;

public:
    EditorCameraController(QSharedPointer<jah3d::CameraNode>  cam);

    QSharedPointer<jah3d::CameraNode>  getCamera();
    void setCamera(QSharedPointer<jah3d::CameraNode>  cam);

    QVector3D getPos();

    void setLinearSpeed(float speed);
    float getLinearSpeed();

    void setLookSpeed(float speed);
    float getLookSpeed();

    void onMouseDragged(int x,int y);

    QVector3D unproject(int viewPortWidth, int viewPortHeight,QPoint pos);

    void updateCameraRot();
};

#endif // EDITORCAMERACONTROLLER_H
