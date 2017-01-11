/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ICAMERACONTROLLER_H
#define ICAMERACONTROLLER_H

#include <Qt>
#include <QSharedPointer>

namespace iris
{
    class CameraNode;
}

class CameraControllerBase
{
public:
    CameraControllerBase()
    {
        resetMouseStates();
    }

    virtual void setCamera(QSharedPointer<iris::CameraNode>  cam);

    virtual void onMouseDown(Qt::MouseButton button);
    virtual void onMouseUp(Qt::MouseButton button);
    virtual void onMouseMove(int x,int y);
    virtual void onMouseWheel(int val);

    virtual void update(float dt);

    void resetMouseStates();


protected:
    QSharedPointer<iris::CameraNode> camera;

    bool leftMouseDown;
    bool middleMouseDown;
    bool rightMouseDown;
};

#endif // ICAMERACONTROLLER_H
