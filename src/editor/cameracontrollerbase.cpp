#include "cameracontrollerbase.h"

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

void CameraControllerBase::onMouseMove(int x,int y)
{

}

void CameraControllerBase::onMouseWheel(int val)
{

}

void CameraControllerBase::resetMouseStates()
{
    leftMouseDown = false;
    middleMouseDown = false;
    rightMouseDown = false;
}
