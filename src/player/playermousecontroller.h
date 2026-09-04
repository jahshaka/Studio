/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PLAYERMOUSECONTROLLER_H
#define PLAYERMOUSECONTROLLER_H

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/scenegraph/scene.h"
#include "viewport/cameracontrollerbase.h"

// Local picking record (was defined by the deleted legacy SceneViewWidget).
struct PickingResult
{
    iris::SceneNodePtr hitNode;
    iris::Vec3 hitPoint;

    // this is often used for comparisons so it's not necessary to find the root
    float distanceFromCameraSqrd;
};

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
    iris::Quat camRot;
    iris::Vec3 camPos;
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
    iris::Vec3 calculateMouseRay(const QPointF& pos);

    void doObjectPicking(
        const QPointF& point);
    iris::Vec3 screenSpaceToWoldSpace(const QPointF& pos, float depth);
    void doScenePicking(const iris::SceneNodePtr& sceneNode,
                        const iris::Vec3& segStart,
                        const iris::Vec3& segEnd,
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
