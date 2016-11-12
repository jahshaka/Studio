#ifndef ORBITALCAMERACONTROLLER_H
#define ORBITALCAMERACONTROLLER_H

#include <QPoint>
#include <QVector3D>
#include <QSharedPointer>
//#include "../jah3d/core/scenenode.h"
//#include "../jah3d/scenegraph/cameranode.h"
#include "cameracontrollerbase.h"

//class CameraPtr;
namespace jah3d
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

    QSharedPointer<jah3d::CameraNode> camera;

    OrbitalCameraController();

    QSharedPointer<jah3d::CameraNode>  getCamera();
    void setCamera(QSharedPointer<jah3d::CameraNode>  cam) override;

    void onMouseMove(int x,int y) override;
    void onMouseWheel(int delta) override;

    void updateCameraRot();
};

#endif // ORBITALCAMERACONTROLLER_H
