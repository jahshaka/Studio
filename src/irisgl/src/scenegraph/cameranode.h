/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef CAMERANODE_H
#define CAMERANODE_H

#include <QMatrix4x4>
#include "../irisglfwd.h"
#include "../core/scenenode.h"

namespace iris
{


class CameraNode:public SceneNode
{
public:
    float fov;//radians
    float aspectRatio;
    float angle;//in degrees
    float nearClip;
    float farClip;

    float vrViewScale;

    QMatrix4x4 viewMatrix;
    QMatrix4x4 projMatrix;

    float getVrViewScale()
    {
        return vrViewScale;
    }

    void setVrViewScale(float viewScale)
    {
        vrViewScale = viewScale;
    }

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
        viewMatrix.setToIdentity();

        QVector3D pos = globalTransform.column(3).toVector3D();
        QVector3D dir = (globalTransform * QVector4D(0,0,-1,1)).toVector3D();
        QVector3D up = (globalTransform * QVector4D(0,1,0,0)).toVector3D();
        viewMatrix.lookAt(pos, dir, up);

        projMatrix.setToIdentity();
        projMatrix.perspective(angle, aspectRatio, nearClip, farClip);

        vrViewScale = 5.0f;
    }

    void update(float dt) override
    {
        SceneNode::update(dt);
        updateCameraMatrices();
    }

    static CameraNodePtr create()
    {
        return QSharedPointer<CameraNode>(new CameraNode());
    }

    /**
     * Calculate picking ray given the screen position.
     * Assumes the ray's origin is the camera's position.
     * @param viewPortWidth
     * @param viewPortHeight
     * @param pos point in screen space
     * @return
     */
    QVector3D calculatePickingDirection(int viewPortWidth, int viewPortHeight, QPointF pos);

private:
    CameraNode()
    {
        angle = 45;
        nearClip = 0.1f;
        farClip = 1000.0f;
        aspectRatio = 1.0f;//assumes a square viewport by default

        updateCameraMatrices();
    }

};

}
#endif // CAMERANODE_H
