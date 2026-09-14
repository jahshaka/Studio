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
#include "viewport/cameracontrollerbase.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "data/settingsmanager.h"
#include <QtGlobal>


CameraControllerBase::CameraControllerBase()
{
	resetMouseStates();
	settings = SettingsManager::getDefaultManager();
}

void CameraControllerBase::setCamera(iris::CameraNodePtr cam)
{
    this->camera = cam;
}

void CameraControllerBase::onMouseDown(Qt::MouseButton button)
{
    switch(button)
    {
        case Qt::LeftButton:
            leftMouseDown = true;
        break;

        case Qt::MiddleButton:
            middleMouseDown = true;
        break;

        case Qt::RightButton:
            rightMouseDown = true;
        break;

    default:
        break;
    }
}

void CameraControllerBase::onMouseUp(Qt::MouseButton button)
{
    switch(button)
    {
        case Qt::LeftButton:
            leftMouseDown = false;
            break;

        case Qt::MiddleButton:
            middleMouseDown = false;
            break;

        case Qt::RightButton:
            rightMouseDown = false;
            break;

        default:
            break;
    }
}

/**
 *
 * issue: when the middle mouse button is down, mouse move events arent registered
 * https://github.com/bjorn/tiled/issues/1079
 * https://bugreports.qt.io/browse/QTBUG-48361
 */
void CameraControllerBase::onMouseMove(int x,int y)
{

}

void CameraControllerBase::onMouseWheel(int val)
{

}

void CameraControllerBase::onKeyPressed(Qt::Key key)
{

}

void CameraControllerBase::onKeyReleased(Qt::Key key)
{

}

void CameraControllerBase::keyReleaseEvent(QKeyEvent * event)
{
    onKeyReleased((Qt::Key)event->key());
}

void CameraControllerBase::setMousePos(int x, int y)
{
    this->mouseX = x;
    this->mouseY = y;
}

void CameraControllerBase::start()
{
    resetMouseStates();
}

void CameraControllerBase::resetMouseStates()
{
    leftMouseDown = false;
    middleMouseDown = false;
    rightMouseDown = false;
    altOrbit = false;
}

void CameraControllerBase::setAltOrbit(bool active, const iris::Vec3 &pivot)
{
    altOrbit = active;
    if (!active) return;
    altOrbitPivot = pivot;
    altOrbitYaw = altOrbitPitch = 0.0f;
    if (!camera) return;
    // The pose the drag differences against. Local, not global: the editor's
    // explorer camera is a child of the scene root and every write below is a
    // setLocalPos/setLocalRot, so one space throughout is the only way the two
    // ends can agree.
    altOrbitStartPos = camera->getLocalPos();
    altOrbitStartRot = camera->getLocalRot().normalized();
    float roll = 0.0f;
    camera->getLocalRot().getEulerAngles(&altOrbitStartPitch, &altOrbitStartYaw, &roll);
}

void CameraControllerBase::applyAltOrbit(float yawDegrees, float pitchDegrees)
{
    if (!altOrbit || !camera) return;
    altOrbitYaw += yawDegrees;
    altOrbitPitch += pitchDegrees;
    // NOTHING DRAGGED, NOTHING WRITTEN. The owner's report is about the frame
    // BEFORE any movement, so the no-movement case is answered by not touching
    // the node at all rather than by writing a value that is merely close.
    if (altOrbitYaw == 0.0f && altOrbitPitch == 0.0f) return;
    // The pole guard is the free camera's, applied to the ACCUMULATED pitch, so
    // the orbit stops at the pole instead of turning over.
    const float pitch = qBound(-89.0f, altOrbitStartPitch + altOrbitPitch, 89.0f);
    const iris::Quat now = iris::Quat::fromEulerAngles(pitch, altOrbitStartYaw + altOrbitYaw, 0);
    const iris::Quat was = iris::Quat::fromEulerAngles(altOrbitStartPitch, altOrbitStartYaw, 0);
    const iris::Quat turn = (now * was.conjugated()).normalized();
    camera->setLocalPos(altOrbitPivot + turn.rotatedVector(altOrbitStartPos - altOrbitPivot));
    camera->setLocalRot((turn * altOrbitStartRot).normalized());
    camera->update(0);
}

void CameraControllerBase::update(float dt)
{

}

void CameraControllerBase::end()
{

}

