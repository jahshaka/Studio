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

#include "irisgl/core/math/vec.h"
#include <QPoint>
#include <QSet>
#include <QSharedPointer>
#include "viewport/cameracontrollerbase.h"

namespace iris
{
    class CameraNode;
}

#include "viewport/cameraspeed.h"

class IEditorViewport;
class EditorCameraController : public CameraControllerBase
{
    float lookSpeed;
    float linearSpeed;   // fly speed in units/second (EDITOR_SHORTCUTS_SPEC §2)

    float yaw;
    float pitch;

	float orthoZoom;

	IEditorViewport* sceneWidget;

	/// Keys currently held (fed by the viewport's key events). Movement only
	/// happens while the right mouse button is down — the Unreal fly rule.
	QSet<int> heldKeys;

	/// The wheel's leftover eighths-of-a-degree while flying, so a trackpad or
	/// a high-resolution wheel steps the camera speed once per NOTCH and not
	/// once per event (see WheelNotches).
	WheelNotches speedWheel;

public:
    EditorCameraController(IEditorViewport* sceneWidget);

    void setCamera(iris::CameraNodePtr cam) override;

    iris::Vec3 getPos();

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
	/// THE HELD SET LIVES EXACTLY AS LONG AS THE RIGHT BUTTON (ledger §356).
	/// Both handlers drop it, which is behaviour-neutral — update() reads the
	/// set only while the right button is down — and closes the one way a fly
	/// key could stay down forever.
	void onMouseDown(Qt::MouseButton button) override;
	void onMouseUp(Qt::MouseButton button) override;
	void onKeyPressed(Qt::Key key) override;
	void onKeyReleased(Qt::Key key) override;
	void clearKeys() override;
	/// The keys the fly currently believes are down — `editor.viewportState()`
	/// reports them, because "the arrows are dead" is otherwise invisible
	/// (Left and Right in the set cancel to no movement at all).
	QSet<int> heldKeyCodes() const override { return heldKeys; }

    void updateCameraRot();

    void update(float dt) override;

	bool canLeftMouseDrag();

	/// True while the fly keys should own the arrow cluster (RMB held) — the
	/// viewport uses this to withhold Up/Down/Left/Right/PageUp/PageDown from
	/// the shortcut system. W/A/S/D/Q/E are NOT fly keys in the editor any
	/// more (owner decision 2026-09-09); the player still takes both.
	bool isFlying() const override { return rightMouseDown; }
};

#endif // EDITORCAMERACONTROLLER_H
