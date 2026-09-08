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
#include <algorithm>
#include "data/settingsmanager.h"
#include "viewport/gizmo.h"
#include "viewport/ieditorviewport.h"
#include "viewport/flyspeedsettings.h"

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
    // THE AXIS-VIEW LOCK, gesture 1 of 2 (Alt+LMB orbit). Ignored outright in
    // an axis view: the drag turns nothing and pans nothing (canLeftMouseDrag
    // refuses while Alt is orbiting), which is Unreal's and Maya's answer.
    if (altOrbit && leftMouseDown && rotationLocked) return;
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

    // Gesture 2 of 2: the RMB look. Locked, the right button still means "I am
    // flying" (it is what arms the fly keys and the wheel's speed step) — it
    // just cannot turn the camera any more.
    if(rightMouseDown && !rotationLocked)
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
    if (rightMouseDown && !rotationLocked) updateCameraRot();
}

void EditorCameraController::setAltOrbit(bool active, const iris::Vec3 &pivot)
{
	CameraControllerBase::setAltOrbit(active, pivot);
	// LOCKED (an axis view): ignored, set-up included — see the arcball's
	// setAltOrbit for why the SET-UP is the part that could still be felt.
	if (!active || !camera || rotationLocked) return;
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

// THE WHEEL HAS TWO JOBS (owner request 2026-09-07, Unreal's model). While the
// RIGHT BUTTON IS HELD the camera is flying, and the wheel steps the fly speed
// — the same value the toolbar dropdown sets, persisted, with the toast the
// viewport shows. Otherwise it dollies, exactly as it always has. The two can
// never fight: flying and not flying are disjoint, and dollying while flying
// was never a gesture anyone could make on purpose (it fought the fly keys).
void EditorCameraController::onMouseWheel(int delta)
{
    if (rightMouseDown) {
        if (delta != 0) {
            FlySpeedSettings::step(FlySpeedSettings::Editor, delta > 0 ? 1 : -1);
            if (sceneWidget) sceneWidget->onFlySpeedChanged();
        }
        return;
    }
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
 * @brief EditorCameraController::setAxisView
 *
 * Snap the FREE camera to a canonical axis view. It TURNS the camera AND
 * MOVES IT ONTO THE AXIS — the turn alone was the whole of this function
 * until 2026-09-07, and it is why the axis views showed nothing: the camera
 * stayed wherever it was (the default explorer pose is (0, 5, 14)), so
 *
 *   * `left` / `right` left the camera sitting INSIDE the x=0 plane those
 *     views align to — the view-facing grid was exactly edge-on, and the
 *     scene at the origin was 14 units off to the side;
 *   * `back` left it at z=+14 looking along +Z, with the z=0 grid and the
 *     whole scene BEHIND the near plane;
 *   * only `front` worked, by luck: the default pose is already on +Z.
 *
 * The orbital controller never had the bug (it rebuilds the position from its
 * pivot). Here the pivot is the world origin — the point every canonical view
 * is a view OF — and the standoff is the camera's current distance from it,
 * so an axis view keeps the framing the user had rather than teleporting.
 */
void EditorCameraController::setAxisView(float yawDeg, float pitchDeg)
{
    if (!camera) return;
    yaw = yawDeg;
    pitch = pitchDeg;

    // Distance to the origin, floored so a camera parked at (or very near)
    // the origin still ends up outside the scene instead of inside it.
    const float dist = std::max(camera->getLocalPos().length(), 5.0f);
    const iris::Quat rot = iris::Quat::fromEulerAngles(pitch, yaw, 0);
    camera->setLocalPos(rot.rotatedVector(iris::Vec3(0, 0, 1)) * dist);
    camera->setLocalRot(rot);
    camera->update(0);
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
//
// THE ARROW KEYS ARE ALIASES (owner request 2026-09-07): Up/Down/Left/Right
// are W/S/A/D, not a second movement model. They were the ONLY fly keys in the
// 2016 editor and the muscle memory outlived the KeyboardState path that fed
// them; one lookup table means the two spellings can never drift apart.
//
// SPEED is FlySpeedSettings::speed(Editor) — linearSpeed is the base and the
// user-chosen multiplier rides on it (toolbar dropdown / wheel while flying).
void EditorCameraController::update(float dt)
{
    if (!camera || !rightMouseDown || heldKeys.isEmpty()) return;

    const iris::Quat rot = camera->getLocalRot();
    const iris::Vec3 worldUp(0, 1, 0);
    const iris::Vec3 forward = rot.rotatedVector(iris::Vec3(0, 0, -1));
    const iris::Vec3 camRight = rot.rotatedVector(iris::Vec3(1, 0, 0));
    const iris::Vec3 camUp = rot.rotatedVector(iris::Vec3(0, 1, 0));
    // Strafe stays HORIZONTAL in free flight (right = forward x worldUp), which
    // is the whole point of flying against the world's up rather than the
    // camera's — except that the cross product DEGENERATES when the camera
    // looks straight up or straight down: Vec3::normalized() returns a ZERO
    // vector there, so A and D silently did nothing at the poles (found while
    // building the axis-view lock, 2026-09-08 — a top view is exactly that
    // pose). Falling back to the camera's own right vector is the same
    // direction everywhere else and the only defined one there.
    iris::Vec3 right = iris::Vec3::crossProduct(forward, worldUp).normalized();
    if (right.isNull()) right = camRight;

    const auto held = [this](int a, int b) {
        return heldKeys.contains(a) || heldKeys.contains(b);
    };

    iris::Vec3 move;
    if (rotationLocked) {
        // AXIS VIEW: the fly keys become a PAN of the view plane plus a dolly
        // along its normal, on the camera's own basis (AXIS_VIEW_LOCK, owner
        // report 2026-09-08). W/S pan up and down the screen — "forward" on a
        // map is up the map, and moving along the view normal is invisible in
        // an orthographic view, so mapping W to it would read as a dead key.
        // A/D strafe in-plane exactly as they do in perspective. Q/E become the
        // dolly: E backs the camera out along the view normal (up, in a top
        // view — the direction "up" still means to the person looking), Q
        // pushes it in. Nothing here can change the camera's ROTATION.
        if (held(Qt::Key_W, Qt::Key_Up))    move += camUp;
        if (held(Qt::Key_S, Qt::Key_Down))  move -= camUp;
        if (held(Qt::Key_D, Qt::Key_Right)) move += camRight;
        if (held(Qt::Key_A, Qt::Key_Left))  move -= camRight;
        if (heldKeys.contains(Qt::Key_E)) move -= forward;
        if (heldKeys.contains(Qt::Key_Q)) move += forward;
    } else {
        if (held(Qt::Key_W, Qt::Key_Up))    move += forward;
        if (held(Qt::Key_S, Qt::Key_Down))  move -= forward;
        if (held(Qt::Key_D, Qt::Key_Right)) move += right;
        if (held(Qt::Key_A, Qt::Key_Left))  move -= right;
        if (heldKeys.contains(Qt::Key_E)) move += worldUp;
        if (heldKeys.contains(Qt::Key_Q)) move -= worldUp;
    }
    if (move.isNull()) return;

    const float boost = heldKeys.contains(Qt::Key_Shift) ? 3.0f : 1.0f;
    const float speed = linearSpeed * FlySpeedSettings::multiplier(FlySpeedSettings::Editor);
    camera->setLocalPos(camera->getLocalPos() + move.normalized() * speed * boost * dt);
    camera->update(0);
}
