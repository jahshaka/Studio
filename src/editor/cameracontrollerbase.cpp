/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "cameracontrollerbase.h"
#include "core/settingsmanager.h"


CameraControllerBase::CameraControllerBase()
{
	resetMouseStates();
	settings = SettingsManager::getDefaultManager();
}

void CameraControllerBase::setCamera(QSharedPointer<iris::CameraNode>  cam)
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

void CameraControllerBase::start()
{
    resetMouseStates();
}

void CameraControllerBase::resetMouseStates()
{
    leftMouseDown = false;
    middleMouseDown = false;
    rightMouseDown = false;
}

void CameraControllerBase::update(float dt)
{

}

void CameraControllerBase::end()
{

}

