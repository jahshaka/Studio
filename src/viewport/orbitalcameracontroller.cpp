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
#include <QPoint>
#include <QSharedPointer>
#include <QtMath>
#include <cmath>

#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "viewport/cameracontrollerbase.h"
#include "viewport/orbitalcameracontroller.h"

#include "data/settingsmanager.h"
#include "viewport/gizmo.h"
#include "viewport/ieditorviewport.h"

float lerp(float a, float b, float t)
{
	return a * (1 - t) + b * t;
}

OrbitalCameraController::OrbitalCameraController(IEditorViewport* sceneWidget)
{
    distFromPivot = 15;
    rotationSpeed = 1.f / 10.f;

	previewMode = false;

	pitch = yaw = 0;
	targetPitch = targetYaw = 0;

	this->sceneWidget = sceneWidget;
}

iris::CameraNodePtr OrbitalCameraController::getCamera()
{
    return camera;
}

/**
 * Adopts a camera: derives the pivot and the (yaw, pitch) the arcball steers
 * with. DECOMPOSE ONLY, never write — see EditorCameraController::setCamera for
 * the whole rationale (this one used to rebuild the pose from pivot + distance
 * as well, so it dropped roll AND could shift the camera by a euler round trip).
 * Nothing is pending afterwards: the targets are matched to the derived angles,
 * so update() has nothing to lerp and leaves the node alone.
 */
void OrbitalCameraController::setCamera(iris::CameraNodePtr  cam)
{
    this->camera = cam;

    //calculate the location of the pivot
    auto viewVec = cam->getLocalRot().rotatedVector(iris::Vec3(0,0,-1));//default forward is -z
    pivot = cam->getLocalPos() + (viewVec*distFromPivot);

    float roll;
    cam->getLocalRot().getEulerAngles(&pitch,&yaw,&roll);
	targetYaw = yaw;
	targetPitch = pitch;
	navPending = false;
}

void OrbitalCameraController::setRotationSpeed(float rotationSpeed)
{
    this->rotationSpeed = rotationSpeed;
}

void OrbitalCameraController::onMouseMove(int x,int y)
{
	// Which branch (if any) fired decides whether the node is written at all.
	// The viewport tracks the mouse for gizmo hover, so onMouseMove arrives on
	// every cursor move with no button down: writing unconditionally rebuilt
	// the pose from (pitch, yaw, 0) and levelled any roll the camera had.
	bool navigated = false;
	if (previewMode && (leftMouseDown || rightMouseDown)) {
		// in case lerping is still in progress, match the values with their targets
		yaw = targetYaw;
		pitch = targetPitch;
		this->yaw	+= x * rotationSpeed;
		this->pitch += y * rotationSpeed;

		// keep pitch and yaw in sync
		targetYaw = yaw;
		targetPitch = pitch;
		navigated = true;
	}
	else if (!previewMode && altOrbit && leftMouseDown) {
		// Alt+LMB orbit: the arcball already orbits — just route Alt+LMB
		// into the same branch, around the pivot the viewport handed us
		// (the selection's centre).
		yaw = targetYaw;
		pitch = targetPitch;
		this->yaw   += x * rotationSpeed;
		this->pitch += y * rotationSpeed;
		targetYaw = yaw;
		targetPitch = pitch;
		navigated = true;
	}
	else if (!previewMode && rightMouseDown) {
		// in case lerping is still in progress, match the values with their targets
		yaw = targetYaw;
		pitch = targetPitch;

		this->yaw	+= x * rotationSpeed;
		this->pitch += y * rotationSpeed;

		// keep pitch and yaw in sync
		targetYaw = yaw;
		targetPitch = pitch;
		navigated = true;
	}

    if (middleMouseDown ||
		canLeftMouseDrag()) {
        //translate camera
        float dragSpeed = 0.01f;
        auto dir = camera->getLocalRot().rotatedVector(iris::Vec3(x*dragSpeed,-y*dragSpeed,0));
        pivot += dir;
        navigated = true;   // the pivot moved: the camera has to follow it
    }

    if (navigated) updateCameraRot();
}

void OrbitalCameraController::setAltOrbit(bool active, const iris::Vec3 &newPivot)
{
	CameraControllerBase::setAltOrbit(active, newPivot);
	if (!active || !camera) return;
	// Orbit around the requested point, keeping the camera where it is: the
	// distance is re-derived so the first drag frame cannot jump.
	pivot = newPivot;
	distFromPivot = camera->getGlobalPosition().distanceToPoint(newPivot);
	yaw = targetYaw;
	pitch = targetPitch;
}

bool OrbitalCameraController::canLeftMouseDrag()
{
	// Refuse camera drags while a gizmo drag is in progress (step-14 fix: the
	// old guard dynamic_cast to the deleted legacy widget and was dead in
	// engine mode, letting the camera pan mid-gizmo-drag).
	auto gizmo = sceneWidget ? sceneWidget->activeGizmo() : nullptr;
	bool gizmoDragging = gizmo && gizmo->isDragging();

	return (leftMouseDown && // left mouse must be down
		!altOrbit && // Alt+LMB orbits; it must not also pan in jahshaka mouse mode
		settings->getValue("mouse_controls", "default").toString() == "jahshaka" && // left mouse to drag in jahshaka mouse mode
		!gizmoDragging); // cant pan while dragging gizmo
}

/**
 * Zooms camera in
 */
void OrbitalCameraController::onMouseWheel(int delta)
{
    //qDebug()<<delta;
    auto zoomSpeed = 0.01f;
    distFromPivot += -delta * zoomSpeed;
    //distFromPivot += delta;

    //todo: remove magic numbers
    if(distFromPivot<0)
        distFromPivot = 0;


	if (camera->projMode == iris::CameraProjection::Orthogonal) {
		if (distFromPivot <= 0.1f) distFromPivot = .01f;
		camera->setOrthagonalZoom(distFromPivot);
	}else{
		updateCameraRot();   // navigation: the orbit radius changed

	}

}

// The lerp that animates an axis-view snap — and NOTHING ELSE. It used to write
// the camera on every single frame the arcball was the active controller, which
// meant a camera the viewport had merely adopted was rebuilt from
// (pitch, yaw, 0) + pivot 60 times a second: roll levelled, and any pose set by
// something other than this controller (a socket, an animation, a verb) fought
// for the node. Now a write needs a NAVIGATION to have asked for it, and the
// run ends when the lerp arrives.
void OrbitalCameraController::update(float dt)
{
	if (!navPending || !camera) return;

	yaw = lerp(yaw, targetYaw, 0.8);
	pitch = lerp(pitch, targetPitch, 0.8);
	// The 0.8 lerp only ever approaches its target, so the run needs an end:
	// within a thousandth of a degree, land exactly on it and stop. (Before,
	// "stopping" simply meant re-writing the same pose forever.)
	if (std::abs(yaw - targetYaw) < 1e-3f && std::abs(pitch - targetPitch) < 1e-3f) {
		yaw = targetYaw;
		pitch = targetPitch;
		navPending = false;
	}
	updateCameraRot();
}

void OrbitalCameraController::onKeyPressed(Qt::Key key)
{
	
}

void OrbitalCameraController::onKeyReleased(Qt::Key key)
{

}

void OrbitalCameraController::keyReleaseEvent(QKeyEvent *event)
{
	// The old raw X/Y/Z axis-view keys that lived here moved to the
	// ShortcutRegistry (view.top/bottom/left/right/front/back): registered
	// shortcuts are remappable, appear in Preferences -> Shortcuts, and work
	// in BOTH camera modes. They land in setAxisView() below.
	onKeyReleased((Qt::Key)event->key());
}

void OrbitalCameraController::setAxisView(float yawDeg, float pitchDeg)
{
	targetYaw = yawDeg;
	targetPitch = pitchDeg;
	navPending = true;   // update()'s lerp is what flies there
}

void OrbitalCameraController::focusOnNode(iris::SceneNodePtr sceneNode)
{
	auto nodePos = sceneNode->getGlobalPosition();
	camera->lookAt(nodePos);
	distFromPivot = camera->getGlobalPosition().distanceToPoint(nodePos);
	this->setCamera(camera);
}

void OrbitalCameraController::updateCameraRot()
{
    auto rot = iris::Quat::fromEulerAngles(pitch,yaw,0);
    auto localPos = rot.rotatedVector(iris::Vec3(0,0,1));

    camera->setLocalPos(pivot+(localPos*distFromPivot));
    camera->setLocalRot(rot);
    camera->update(0);


}
