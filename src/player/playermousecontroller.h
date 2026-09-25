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
#include "viewport/cameraspeed.h"
#include "viewport/flystep.h"

// Local picking record (was defined by the deleted legacy SceneViewWidget).
struct PickingResult
{
    iris::SceneNodePtr hitNode;
    iris::Vec3 hitPoint;

    // this is often used for comparisons so it's not necessary to find the root
    float distanceFromCameraSqrd = 0.0f;
};

class PlayerMouseController : public CameraControllerBase
{
	iris::ScenePtr scene;
    iris::SceneNodePtr pickedNode;

    float movementSpeed = 25;

    float pitch;
    float yaw;
    /// The camera's OWN roll, captured with the other two and written back
    /// unchanged (PLAYER-SPAWN-1 rule 3): a free viewer never has one, an
    /// authored camera may, and flying is not a reason to level it.
    float roll = 0.0f;

	bool _isPlaying = false;

    iris::Viewport viewport;

	/// The wheel's leftover eighths-of-a-degree while the right button is
	/// held, so the camera speed steps once per NOTCH and not once per event.
	WheelNotches speedWheel;

public:
	void setPlayState(bool playState) { _isPlaying = playState; }

	PlayerMouseController();
	void setCamera(iris::CameraNodePtr  cam);
	void setScene(iris::ScenePtr scene);

	void update(float dt) override;
    void doGodMode(float dt);
    /// One frame of free flight — and NOTHING when nothing is held, which is
    /// what makes it safe to fly the run's ACTIVE camera (PLAYER-SPAWN-1 rule
    /// 3; the reasoning is on the definition). True when it flew.
    bool flyThisFrame(const flystep::Keys &keys, float linearSpeed);
    /// What is held right now, as flight intentions (S13): arrows and W/A/S/D
    /// are aliases here, Q/E (PageDown/PageUp) are the vertical pair. Both the
    /// play-mode fly and the free camera turn it into motion through
    /// viewport/flystep.h — the editor's own step, so "forward" is the
    /// camera's true forward on every surface.
    static flystep::Keys heldFlyKeys();
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
    /// Takes a camera over: reads its heading and writes nothing (see the
    /// definition — the matching end() is deleted, not forgotten).
    void start() override;
};

#endif // VIEWERCONTROLLER_H
