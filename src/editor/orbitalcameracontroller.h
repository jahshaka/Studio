#ifndef ORBITALCAMERACONTROLLER_H
#define ORBITALCAMERACONTROLLER_H

#include <QPoint>
#include <QVector3D>
#include <QSharedPointer>
//#include "../irisgl/src/core/scenenode.h"
//#include "../irisgl/src/scenegraph/cameranode.h"
#include "cameracontrollerbase.h"

//class CameraPtr;
namespace iris
{
    class CameraNode;
}

class OrbitalCameraController:public CameraControllerBase
{
public:
    float lookSpeed;
    float linearSpeed;

    float yaw;
    float pitch;

    QVector3D pivot;
    float distFromPivot;

    QSharedPointer<iris::CameraNode> camera;

    OrbitalCameraController();

    QSharedPointer<iris::CameraNode>  getCamera();
    void setCamera(QSharedPointer<iris::CameraNode>  cam) override;

    void onMouseMove(int x,int y) override;
    void onMouseWheel(int delta) override;

    void updateCameraRot();
};

#endif // ORBITALCAMERACONTROLLER_H
