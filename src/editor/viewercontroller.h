/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef VIEWERCONTROLLER_H
#define VIEWERCONTROLLER_H

#include "irisglfwd.h"
#include <QQuaternion>
#include <QVector3D>
#include "cameracontrollerbase.h"

class ViewerCameraController : public CameraControllerBase
{
    iris::ViewerNodePtr viewer;
    float pitch;
    float yaw;

    // pos and rot of editor camera before being assigned
    // to this controller
    QQuaternion camRot;
    QVector3D camPos;

public:

    ViewerCameraController();

    void update(float dt) override;
    void onMouseMove(int dx,int dy) override;
    void onMouseWheel(int delta) override;

    void updateCameraTransform();
    void setViewer(iris::ViewerNodePtr &value);
    void start() override;
    void end() override;
    void clearViewer();
};

#endif // VIEWERCONTROLLER_H
