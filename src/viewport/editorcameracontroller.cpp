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
#include "viewport/flystep.h"
#include "viewport/cameraspeed.h"

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

    pitch += angle;
    pitch = (pitch<-90?-90:(pitch>90?90:pitch));//clamp( pitch,-90,90)

}

/**
 * @brief rotates the camera around the up vector
 * @param angle
 */
void EditorCameraController::pan(float angle)
{
    yaw += angle;
}

/**
 *
 * @param x
 * @param y
 */
void EditorCameraController::onMouseMove(int x,int y)
{
    // Alt+LMB orbit (Maya/Unreal), and it is a ROTATION of the camera's
    // current offset from the pivot — never a re-placement on a sphere
    // (cameracontrollerbase.h's setAltOrbit carries the maths and the owner
    // report §353 it answers). The free camera keeps its own orientation
    // model; this is a temporary orbit for the duration of the drag only.
    // THE AXIS-VIEW LOCK, gesture 1 of 2 (Alt+LMB orbit). Ignored outright in
    // an axis view: the drag turns nothing and pans nothing (canLeftMouseDrag
    // refuses while Alt is orbiting), which is Unreal's and Maya's answer.
    if (altOrbit && leftMouseDown && rotationLocked) return;
    if (altOrbit && leftMouseDown && camera) {
        applyAltOrbit(x / 10.0f, y / 10.0f);
        // The fly and the look continue from the pose the orbit left, so the
        // controller's own (yaw, pitch) are re-read off the node — the same
        // decomposition setCamera does, and just as read-only.
        float roll = 0.0f;
        camera->getLocalRot().getEulerAngles(&pitch, &yaw, &roll);
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

    // ONLY the look drag writes the rotation. This used to run on every mouse
    // move — the viewport has mouse tracking on for gizmo hover, so simply
    // moving the cursor over the viewport rewrote the camera's rotation from
    // (pitch, yaw, 0) and levelled any roll it had. The pan branch above
    // already moved the camera and called update(0); its rotation is unchanged.
    if (rightMouseDown && !rotationLocked) updateCameraRot();
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
		settings->get(settingkeys::mouseControls) == "jahshaka" && // left mouse to drag in jahshaka mouse mode
		!gizmoDragging); // cant pan while dragging gizmo
}

// THE WHEEL HAS TWO JOBS (owner request 2026-09-07, Unreal's model). While the
// RIGHT BUTTON IS HELD the camera is flying, and the wheel steps THE camera
// speed — the one integer the toolbar button shows and the VR fly rides on
// too (CameraSpeed, owner R15), persisted, with the toast the viewport shows.
// Otherwise it dollies, exactly as it always has. The two can never fight:
// flying and not flying are disjoint, and dollying while flying was never a
// gesture anyone could make on purpose (it fought the fly keys).
//
// ONE NOTCH IS ONE, AND SHIFT IS FIVE: the dial is 32 steps wide now rather
// than a six-rung ladder, so crossing it a notch at a time is a long scroll —
// and Shift is already this gesture's "more of that" key on the fly itself.
// A NOTCH, not an event: `delta` is eighths of a degree and a trackpad sends
// fractions of one (WheelNotches).
void EditorCameraController::onMouseWheel(int delta)
{
    if (rightMouseDown) {
        const int notches = speedWheel.accumulate(delta);
        if (notches != 0) {
            const int stride = heldKeys.contains(Qt::Key_Shift) ? 5 : 1;
            CameraSpeed::step(notches * stride);
            if (sceneWidget) sceneWidget->onCameraSpeedChanged();
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

// THE HELD SET LIVES EXACTLY AS LONG AS THE RIGHT BUTTON (owner report
// 2026-09-15, ledger §356: "the arrows stop flying after a console script run").
//
// THE DEFECT, measured by the rig: `heldKeys` was cleared in exactly ONE place —
// the viewport's focusOutEvent — so a key that went in and never came out stayed
// in for the rest of the session. It does not come out when Qt's XCB auto-repeat
// classification misfires, and that classification is a LOOKAHEAD HEURISTIC over
// the X queue which misfires in both directions once the UI thread stalls long
// enough to back the queue up: a console script run of 3-20 seconds is exactly
// that. Measured 1 stuck key in ~30 attempts. The symptom is silent — Left and
// Right both in the set cancel to `move.isNull()` and no movement at all — and a
// click INSIDE the viewport cures nothing (it is already focused, so there is no
// focus event); only a click on another widget and back did.
//
// The cure is not to trust the classification more (that is the part that cannot
// be trusted) but to bound the set's lifetime by the gesture that reads it:
// update() looks at `heldKeys` only while the right button is down, so dropping
// it on the button's way down AND on its way up is behaviour-neutral and leaves
// no window in which a stale key can survive. The one corner it changes: a key
// already physically held when the right button goes down is not flown until it
// is pressed again — which is the correct reading of "the fly starts now".
void EditorCameraController::onMouseDown(Qt::MouseButton button)
{
	CameraControllerBase::onMouseDown(button);
	if (button == Qt::RightButton) { clearKeys(); speedWheel.reset(); }
}

void EditorCameraController::onMouseUp(Qt::MouseButton button)
{
	CameraControllerBase::onMouseUp(button);
	// THE FLY IS OVER: drop the wheel's leftover fraction, and give the dial's
	// deferred store write its gesture end (CameraSpeed::flush) so the value
	// the user settled on is on disk without a single notch of the scroll
	// having waited for one.
	if (button == Qt::RightButton) { clearKeys(); speedWheel.reset(); CameraSpeed::flush(); }
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

    camera->setLocalRot(iris::Quat::fromEulerAngles(pitch,yaw,0));
    camera->update(0);
}

// The editor fly: while the right mouse button is held, the ARROW KEYS move
// the camera — Up/Down along the view direction, Left/Right along the
// horizontal right vector, PageUp/PageDown up and down the world axis — and
// Shift boosts 3x. Frame-rate independent (dt).
//
// THE ARROWS ARE THE FLY KEYS, AND W/A/S/D/Q/E ARE NOT (owner decision
// 2026-09-09). The editor shipped with WASD (Unreal's spelling) and gained the
// arrows as aliases in 2026-09-07; both spellings on one surface meant six
// letter keys could never be anything else while the right button was down,
// and the letters are the scarce resource in an editor — every tool shortcut
// wants one. So the aliasing is INVERTED here: the arrows are the whole
// editor fly and W/A/S/D/Q/E are FREE. The PLAYER still answers to both
// (playermousecontroller.cpp), because a player is a game surface and WASD is
// what a game player's hand expects.
//
// PageUp/PageDown replace Q/E for vertical movement: they are the only other
// keys on the arrow cluster, so the whole fly stays under one hand without
// crossing back to the letters.
//
// SPEED is CameraSpeed::editorSpeed() — linearSpeed is this surface's base
// (8 u/s) and the user's one camera-speed dial rides on it as the factor n/10
// (toolbar speed button / wheel while flying).
void EditorCameraController::update(float dt)
{
    if (!camera || !rightMouseDown || heldKeys.isEmpty()) return;
    // NOT WHILE A WEARER HAS THE KEYS (VR-4-FIX finding 5). The editor's VR
    // preview redirects this one gesture to the rig, and the suppression lives
    // HERE rather than at the call site so that everything else update() does
    // — the orbit controller's axis-view lerp above all — goes on running while
    // a session is on. (It used to be a skipped `update(dt)` in the viewport,
    // which froze a Views-dropdown snap for the length of a session.)
    if (flySuppressed) return;

    // NO SINGLE FLY STEP IS LONGER THAN kMaxFlyStep (ledger §356's collateral
    // defect, measured on the rig): the host charges the WALL CLOCK of the
    // frame just gone, and a UI-thread block — a console script run — hands the
    // whole stall over as one dt. A fly key held across a 13 second block moved
    // the camera 110 units in a single frame. The clamp lives here rather than
    // at the call site because it is this controller's invariant and it has to
    // hold for every caller of update(); the DOCUMENT clock still gets the real
    // dt, so nothing else is slowed. The cost is stated: below 15 fps the fly
    // moves at 15 fps's rate.
    dt = qMin(dt, flystep::kMaxFlyStep);

    const iris::Quat rot = camera->getLocalRot();
    const iris::Vec3 forward = rot.rotatedVector(iris::Vec3(0, 0, -1));
    const iris::Vec3 camRight = rot.rotatedVector(iris::Vec3(1, 0, 0));
    const iris::Vec3 camUp = rot.rotatedVector(iris::Vec3(0, 1, 0));

    const auto held = [this](int key) { return heldKeys.contains(key); };

    iris::Vec3 move;
    if (rotationLocked) {
        // AXIS VIEW: the fly keys become a PAN of the view plane plus a dolly
        // along its normal, on the camera's own basis (AXIS_VIEW_LOCK, owner
        // report 2026-09-08). Up/Down pan up and down the screen — "forward"
        // on a map is up the map, and moving along the view normal is
        // invisible in an orthographic view, so mapping Up to it would read as
        // a dead key. Left/Right strafe in-plane exactly as they do in
        // perspective. PageUp/PageDown become the dolly: PageUp backs the
        // camera out along the view normal (up, in a top view — the direction
        // "up" still means to the person looking), PageDown pushes it in.
        // Nothing here can change the camera's ROTATION.
        if (held(Qt::Key_Up))       move += camUp;
        if (held(Qt::Key_Down))     move -= camUp;
        if (held(Qt::Key_Right))    move += camRight;
        if (held(Qt::Key_Left))     move -= camRight;
        if (held(Qt::Key_PageUp))   move -= forward;
        if (held(Qt::Key_PageDown)) move += forward;
    } else {
        // FREE FLIGHT is viewport/flystep.h — the one definition of "which way
        // do the fly keys move a camera", shared with the Assets preview
        // (smoke S7). The horizontal strafe and its pole fallback live there.
        flystep::Keys keys;
        keys.forward = held(Qt::Key_Up);
        keys.back    = held(Qt::Key_Down);
        keys.right   = held(Qt::Key_Right);
        keys.left    = held(Qt::Key_Left);
        keys.up      = held(Qt::Key_PageUp);
        keys.down    = held(Qt::Key_PageDown);
        move = flystep::direction(rot, keys);
    }
    if (move.isNull()) return;

    const float boost = heldKeys.contains(Qt::Key_Shift) ? flystep::kBoost : 1.0f;
    const float speed = CameraSpeed::applyTo(linearSpeed);
    camera->setLocalPos(camera->getLocalPos() + move.normalized() * speed * boost * dt);
    camera->update(0);
}
