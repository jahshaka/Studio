/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITORCAMERACONTROLLER_H
#define EDITORCAMERACONTROLLER_H

#include <QPoint>
#include <QVector3D>
#include <QSharedPointer>
#include "viewport/cameracontrollerbase.h"

namespace iris
{
    class CameraNode;
}

class IEditorViewport;
class EditorCameraController : public CameraControllerBase
{
    QSharedPointer<iris::CameraNode> camera;

    float lookSpeed;
    float linearSpeed;

    float yaw;
    float pitch;

	float orthoZoom;

	IEditorViewport* sceneWidget;

public:
    EditorCameraController(IEditorViewport* sceneWidget);

	iris::CameraNodePtr getCamera();
    void setCamera(iris::CameraNodePtr cam) override;

    QVector3D getPos();

    void setLinearSpeed(float speed);
    float getLinearSpeed();

    void setLookSpeed(float speed);
    float getLookSpeed();

    //incomplete
    void tilt(float angle);

    //incomplete
    void pan(float angle);

    void onMouseMove(int x,int y) override;
    void onMouseWheel(int delta) override;
	void onKeyPressed(Qt::Key key);
	void onKeyReleased(Qt::Key key);

    void updateCameraRot();

    void update(float dt);

	bool canLeftMouseDrag();
};

#endif // EDITORCAMERACONTROLLER_H
