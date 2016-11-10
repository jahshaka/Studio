#ifndef ICAMERACONTROLLER_H
#define ICAMERACONTROLLER_H

#include <Qt>

class CameraControllerBase
{
public:
    virtual void onMouseDown(Qt::MouseButton button);
    virtual void onMouseUp(Qt::MouseButton button);
    virtual void onMouseMove(int x,int y);
    virtual void onMouseWheel(int val);

    void resetMouseStates();

private:
    bool leftMouseDown;
    bool middleMouseDown;
    bool rightMouseDown;
};

#endif // ICAMERACONTROLLER_H
