#ifndef CAMERANODE_H
#define CAMERANODE_H

#include <QMatrix4x4>
#include <QSharedPointer>
#include "../core/scenenode.h"

namespace jah3d
{

class CameraNode;
typedef QSharedPointer<jah3d::CameraNode> CameraNodePtr;

class CameraNode:public SceneNode
{
    float fov;//radians
    float aspectRatio;
public:

    QMatrix4x4 viewMatrix;
    QMatrix4x4 projMatrix;

    void setAspectRatio(float aspect)
    {
        this->aspectRatio = aspect;
    }

    //radians
    void setFieldOfView(float fov)
    {
        this->fov = fov;
    }

    void setFieldOfViewDegrees(float fov)
    {
        this->fov = fov;
    }

    //update view and proj matrices
    void updateCameraMatrices()
    {

    }


    static CameraNodePtr create()
    {
        return QSharedPointer<CameraNode>(new CameraNode());
    }

private:
    CameraNode()
    {
        viewMatrix.setToIdentity();
        //viewMatrix = QMatrix4x4();
        viewMatrix.lookAt(QVector3D(0,10,10),QVector3D(0,0,0),QVector3D(0,1,0));

        projMatrix.setToIdentity();
        projMatrix.perspective(45,800.0f/600.0f,1.0f,100.0f);
    }

};

}
#endif // CAMERANODE_H
