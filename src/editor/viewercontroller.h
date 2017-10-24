#ifndef VIEWERCONTROLLER_H
#define VIEWERCONTROLLER_H

#include "../../../irisgl/src/irisglfwd.h"
#include <QQuaternion>
#include <QVector3D>
#include "cameracontrollerbase.h"

class ViewerCameraController : public CameraControllerBase
{
    iris::ViewerNodePtr viewer;
    float pitch;
    float yaw;

    // pos and rot of editor camera before being assigned
    // to this controller
    QQuaternion camRot;
    QVector3D camPos;

public:

    ViewerCameraController();

    void update(float dt) override;
    void onMouseMove(int dx,int dy) override;
    void onMouseWheel(int delta) override;

    void updateCameraTransform();
    void setViewer(iris::ViewerNodePtr &value);
    void start() override;
    void end() override;
    void clearViewer();
};

#endif // VIEWERCONTROLLER_H
