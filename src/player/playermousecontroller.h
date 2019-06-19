/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef PLAYERMOUSECONTROLLER_H
#define PLAYERMOUSECONTROLLER_H

#include "irisglfwd.h"
#include <QQuaternion>
#include <QVector3D>
#include "../editor/cameracontrollerbase.h"

class PlayerMouseController : public CameraControllerBase
{
	iris::ScenePtr scene;
	//iris::CameraNodePtr camera;
	iris::ViewerNodePtr viewer;

    float pitch;
    float yaw;

    // pos and rot of editor camera before being assigned
    // to this controller
    QQuaternion camRot;
    QVector3D camPos;
	bool _isPlaying = false;

	bool shouldRestoreCameraTransform;

public:
	void setPlayState(bool playState) { _isPlaying = playState; }

	PlayerMouseController();
	void setCamera(iris::CameraNodePtr  cam);
	void setScene(iris::ScenePtr scene);

	void update(float dt) override;
	void postUpdate(float dt) override;
    void onMouseMove(int dx,int dy) override;
    void onMouseWheel(int delta) override;

    void updateCameraTransform();
	void captureYawPitchRollFromCamera();
    void setViewer(iris::ViewerNodePtr &value);
    void start() override;
    void end() override;
    void clearViewer();

	void setRestoreCameraTransform(bool shouldRestore);
};

#endif // VIEWERCONTROLLER_H
