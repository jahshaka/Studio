#ifndef ICAMERACONTROLLER_H
#define ICAMERACONTROLLER_H

#include <Qt>
#include <QSharedPointer>

namespace jah3d
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

    virtual void setCamera(QSharedPointer<jah3d::CameraNode>  cam);

    virtual void onMouseDown(Qt::MouseButton button);
    virtual void onMouseUp(Qt::MouseButton button);
    virtual void onMouseMove(int x,int y);
    virtual void onMouseWheel(int val);

    void resetMouseStates();

protected:
    QSharedPointer<jah3d::CameraNode> camera;

    bool leftMouseDown;
    bool middleMouseDown;
    bool rightMouseDown;
};

#endif // ICAMERACONTROLLER_H
