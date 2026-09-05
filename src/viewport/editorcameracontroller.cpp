/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "viewport/editorcameracontroller.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include <qmath.h>
#include <math.h>
#include "data/settingsmanager.h"
#include "viewport/gizmo.h"
#include "viewport/ieditorviewport.h"

#include <QDebug>
using namespace iris;

EditorCameraController::EditorCameraController(IEditorViewport* sceneWidget):
	CameraControllerBase()
{
    lookSpeed = 200;
    linearSpeed = 8.0f;   // fly speed, units/second (frame-rate independent)

    yaw = 0;
    pitch = 0;
	orthoZoom = 0;
	this->sceneWidget = sceneWidget;
}

CameraNodePtr EditorCameraController::getCamera()
{
    return camera;
}

/**
 * Adopts a camera: DECOMPOSE ONLY, never write.
 *
 * The controller's state is (yaw, pitch), so adopting a camera means reading
 * those out of its rotation. It used to end in updateCameraRot(), which writes
 * `Quat::fromEulerAngles(pitch, yaw, 0)` straight back onto the node — a round
 * trip that is exact for a roll-free rotation and DESTRUCTIVE for any other.
 * Merely handing a camera to the viewport therefore zeroed its roll, on the
 * document node, permanently: fatal for an authored or socketed camera (a
 * camera riding a bone is rolled by the bone), and camera.screenshot had to
 * snapshot and restore poses around every shot to survive it.
 *
 * So the write is gone. Adoption observes; only NAVIGATION (a drag, a wheel, an
 * axis-view request) may write the node's pose. For a roll-free camera nothing
 * changes at all — the write it replaced was a no-op on those.
 */
void EditorCameraController::setCamera(CameraNodePtr cam)
{
    this->camera = cam;

	orthoZoom = camera->orthoSize;

    float roll;
    cam->getLocalRot().getEulerAngles(&pitch,&yaw,&roll);
}

iris::Vec3 EditorCameraController::getPos()
{
    //return this->camera->position();
    return iris::Vec3();
}

void EditorCameraController::setLinearSpeed(float speed)
{
    linearSpeed = speed;
}

float EditorCameraController::getLinearSpeed()
{
    return linearSpeed;
}

void EditorCameraController::setLookSpeed(float speed)
{
    lookSpeed = speed;
}

float EditorCameraController::getLookSpeed()
{
    return lookSpeed;
}

/**
 * @brief rotates camera around the local x-axis
 * the angle is in degrees
 * pitch is restricted to the range of -90 and 90
 * todo: use global rotation in calculation
 */
void EditorCameraController::tilt(float angle)
{
    /*
    auto forward = camera->rot.rotatedVector(iris::Vec3(0,0,-1));
    auto up = iris::Vec3(0,1,0);

    auto side = iris::Vec3::crossProduct(forward,up);
    */

    pitch += angle;
    pitch = (pitch<-90?-90:(pitch>90?90:pitch));//clamp( pitch,-90,90)

}

/**
 * @brief rotates the camera around the up vector
 * @param angle
 */
void EditorCameraController::pan(float angle)
{
    //camera->rot = iris::Quat::fromAxisAndAngle(iris::Vec3(0,1,0),angle)*camera->rot;
    yaw += angle;
    //yaw = fmod(yaw,360);
}

/**
 *
 * @param x
 * @param y
 */
void EditorCameraController::onMouseMove(int x,int y)
{
    // Alt+LMB orbit (Maya/Unreal): turn yaw/pitch like a look, then put the
    // camera back on the orbit sphere so the pivot stays put on screen. The
    // free camera keeps its own orientation model — this is a temporary
    // orbit for the duration of the drag only.
    if (altOrbit && leftMouseDown && camera) {
        this->yaw += x / 10.0f;
        this->pitch += y / 10.0f;
        pitch = (pitch < -89.0f ? -89.0f : (pitch > 89.0f ? 89.0f : pitch));
        const iris::Quat rot = iris::Quat::fromEulerAngles(pitch, yaw, 0);
        camera->setLocalPos(altOrbitPivot + rot.rotatedVector(iris::Vec3(0, 0, 1)) * altOrbitDistance);
        camera->setLocalRot(rot);
        camera->update(0);
        return;   // never also pan/look on the same drag
    }

    if(rightMouseDown)
    {
        //rotate camera
        this->yaw += x/10.0f;
        this->pitch += y/10.0f;
    }

    if(middleMouseDown || canLeftMouseDrag())
    {
        //translate camera
        float dragSpeed = 0.01f;
        auto dir = camera->getLocalRot().rotatedVector(iris::Vec3(x*dragSpeed,-y*dragSpeed,0));
        camera->setLocalPos( camera->getLocalPos() + dir);

        camera->update(0);//force calculation of global transform. find a better way to do this
    }

    /*
    //todo: world-space translation using keyboard
    iris::Vec3 upVector(0,1,0);
    iris::Vec3 viewVector = camera->viewCenter() - camera->position();
    auto x = iris::Vec3::crossProduct(viewVector, upVector).normalized();
    //auto z = viewVector.normalized();
    auto z = iris::Vec3::crossProduct(upVector,x).normalized();

    camera->translateWorld(txAxis->value()*x*linearSpeed);
    camera->translateWorld(tyAxis->value()*z*linearSpeed);
    */

    // ONLY the look drag writes the rotation. This used to run on every mouse
    // move — the viewport has mouse tracking on for gizmo hover, so simply
    // moving the cursor over the viewport rewrote the camera's rotation from
    // (pitch, yaw, 0) and levelled any roll it had. The pan branch above
    // already moved the camera and called update(0); its rotation is unchanged.
    if (rightMouseDown) updateCameraRot();
}

void EditorCameraController::setAltOrbit(bool active, const iris::Vec3 &pivot)
{
	CameraControllerBase::setAltOrbit(active, pivot);
	if (!active || !camera) return;
	// Capture the orbit radius at drag start so the first frame cannot jump;
	// a camera sitting exactly on the pivot gets a sane default distance.
	altOrbitDistance = camera->getGlobalPosition().distanceToPoint(pivot);
	if (altOrbitDistance < 0.001f) altOrbitDistance = 5.0f;
}

bool EditorCameraController::canLeftMouseDrag()
{
	// Alt+LMB orbits; it must not also pan in jahshaka mouse mode.
	if (altOrbit) return false;

	// Refuse camera drags while a gizmo drag is in progress (step-14 fix: the
	// old guard dynamic_cast to the deleted legacy widget and was dead in
	// engine mode, letting the camera pan mid-gizmo-drag).
	auto gizmo = sceneWidget ? sceneWidget->activeGizmo() : nullptr;
	bool gizmoDragging = gizmo && gizmo->isDragging();

	return (leftMouseDown && // left mouse must be down
		settings->getValue("mouse_controls", "default").toString() == "jahshaka" && // left mouse to drag in jahshaka mouse mode
		!gizmoDragging); // cant pan while dragging gizmo
}

void EditorCameraController::onMouseWheel(int delta)
{
    auto zoomSpeed = 0.01f;
    auto forward = camera->getLocalRot().rotatedVector(iris::Vec3(0,0,-1));
    auto movement = camera->getLocalPos() + forward*zoomSpeed*delta;
	if (camera->projMode == iris::CameraProjection::Perspective)
		camera->setLocalPos(movement);
	else {	
		orthoZoom -= delta/120;
		if (orthoZoom <= 0.1f) orthoZoom = 0.1f;
		camera->setOrthagonalZoom(orthoZoom);
	}
}

void EditorCameraController::onKeyPressed(Qt::Key key)
{
	heldKeys.insert(int(key));
}

void EditorCameraController::onKeyReleased(Qt::Key key)
{
	heldKeys.remove(int(key));
}

void EditorCameraController::clearKeys()
{
	heldKeys.clear();
}

/**
 * @brief EditorCameraController::updateCameraRot
 */
void EditorCameraController::setAxisView(float yawDeg, float pitchDeg)
{
    if (!camera) return;
    yaw = yawDeg;
    pitch = pitchDeg;
    updateCameraRot();
}

void EditorCameraController::updateCameraRot()
{
    //iris::Quat yawQuat = iris::Quat::fromEulerAngles(0,yaw,0);
    //iris::Quat pitchQuat = iris::Quat::fromEulerAngles(pitch,0,0);

    //camera->rot = yawQuat*pitchQuat;
    camera->setLocalRot(iris::Quat::fromEulerAngles(pitch,yaw,0));
    camera->update(0);
}

// Unreal-style fly: while the right mouse button is held, W/A/S/D move along
// the view direction and the camera's right vector, Q/E move down/up the world
// axis, Shift boosts 3x. Frame-rate independent (dt) — the dead KeyboardState
// arrow-key path this replaces was never fed (EDITOR_SHORTCUTS_SPEC §2).
void EditorCameraController::update(float dt)
{
    if (!camera || !rightMouseDown || heldKeys.isEmpty()) return;

    const iris::Vec3 worldUp(0, 1, 0);
    const iris::Vec3 forward = camera->getLocalRot().rotatedVector(iris::Vec3(0, 0, -1));
    const iris::Vec3 right = iris::Vec3::crossProduct(forward, worldUp).normalized();

    iris::Vec3 move;
    if (heldKeys.contains(Qt::Key_W)) move += forward;
    if (heldKeys.contains(Qt::Key_S)) move -= forward;
    if (heldKeys.contains(Qt::Key_D)) move += right;
    if (heldKeys.contains(Qt::Key_A)) move -= right;
    if (heldKeys.contains(Qt::Key_E)) move += worldUp;
    if (heldKeys.contains(Qt::Key_Q)) move -= worldUp;
    if (move.isNull()) return;

    const float boost = heldKeys.contains(Qt::Key_Shift) ? 3.0f : 1.0f;
    camera->setLocalPos(camera->getLocalPos() + move.normalized() * linearSpeed * boost * dt);
    camera->update(0);
}
