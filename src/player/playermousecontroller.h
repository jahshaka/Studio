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
#include "../widgets/sceneviewwidget.h"

class PlayerMouseController : public CameraControllerBase
{
	iris::ScenePtr scene;
    //iris::CameraNodePtr camera;
    iris::SceneNodePtr pickedNode;
	iris::ViewerNodePtr viewer;

    float movementSpeed = 25;

    float pitch;
    float yaw;

    // pos and rot of editor camera before being assigned
    // to this controller
    QQuaternion camRot;
    QVector3D camPos;
	bool _isPlaying = false;

	bool shouldRestoreCameraTransform;

    iris::Viewport viewport;

public:
	void setPlayState(bool playState) { _isPlaying = playState; }

	PlayerMouseController();
	void setCamera(iris::CameraNodePtr  cam);
	void setScene(iris::ScenePtr scene);

	void update(float dt) override;
    void doGodMode(float dt);
	void postUpdate(float dt) override;
    void onMouseMove(int dx,int dy) override;
    void onMouseWheel(int delta) override;
    void onMouseDown(Qt::MouseButton button) override;
    void onMouseUp(Qt::MouseButton button) override;
    QVector3D calculateMouseRay(const QPointF& pos);

    void doObjectPicking(
        const QPointF& point);
    QVector3D screenSpaceToWoldSpace(const QPointF& pos, float depth);
    void doScenePicking(const iris::SceneNodePtr& sceneNode,
                        const QVector3D& segStart,
                        const QVector3D& segEnd,
                        QList<PickingResult>& hitList);
    void setViewport(const iris::Viewport& viewport);

    void updateCameraTransform();
	void captureYawPitchRollFromCamera();
    void setViewer(const iris::ViewerNodePtr &value);
    void start() override;
    void end() override;
    void clearViewer();

	void setRestoreCameraTransform(bool shouldRestore);
};

#endif // VIEWERCONTROLLER_H
