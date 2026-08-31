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
#include <QSet>
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
    float linearSpeed;   // fly speed in units/second (EDITOR_SHORTCUTS_SPEC §2)

    float yaw;
    float pitch;

	float orthoZoom;

	IEditorViewport* sceneWidget;

	/// Keys currently held (fed by the viewport's key events). Movement only
	/// happens while the right mouse button is down — the Unreal fly rule.
	QSet<int> heldKeys;

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

    /// Snap to a canonical view (Views dropdown / view.* shortcuts): the free
    /// camera keeps its position and turns to the axis orientation.
    void setAxisView(float yawDeg, float pitchDeg);

    void onMouseMove(int x,int y) override;
    void onMouseWheel(int delta) override;
	void onKeyPressed(Qt::Key key) override;
	void onKeyReleased(Qt::Key key) override;
	void clearKeys() override;

    void updateCameraRot();

    void update(float dt) override;

	bool canLeftMouseDrag();

	/// True while the fly keys should own W/A/S/D/Q/E (RMB held) — the viewport
	/// uses this to withhold those keys from the shortcut system.
	bool isFlying() const { return rightMouseDown; }
};

#endif // EDITORCAMERACONTROLLER_H
