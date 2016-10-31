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
#include "../jah3d/core/scenenode.h"
#include "../jah3d/scenegraph/cameranode.h"

//class CameraPtr;

class EditorCameraController
{
    Q_OBJECT

    jah3d::CameraNodePtr camera;

    float lookSpeed;
    float linearSpeed;

public:
    EditorCameraController(jah3d::CameraNodePtr  cam);

    jah3d::CameraNodePtr  getCamera();
    void setCamera(jah3d::CameraNodePtr  cam);

    QVector3D getPos();

    void setLinearSpeed(float speed);
    float getLinearSpeed();

    void setLookSpeed(float speed);
    float getLookSpeed();

    QVector3D unproject(int viewPortWidth, int viewPortHeight,QPoint pos);

private slots:
    void onFrame(float dt);
};

#endif // EDITORCAMERACONTROLLER_H
