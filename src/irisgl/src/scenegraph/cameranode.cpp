/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "cameranode.h"
#include <QVector3D>
#include <QPoint>
#include <QMatrix4x4>
#include "../math/mathhelper.h"

namespace iris
{

void CameraNode::lookAt(QVector3D target)
{
    //todo: use global matrices
    QMatrix4x4 matrix;
    matrix.setToIdentity();
    matrix.lookAt(pos, target, QVector3D(0,1,0));
    matrix = matrix.inverted();
    MathHelper::decomposeMatrix(matrix, pos, rot, scale);
}

QVector3D CameraNode::calculatePickingDirection(int viewPortWidth, int viewPortHeight,QPointF pos)
{
    float x = ((2.0f * pos.x()) / viewPortWidth) - 1.0f;
    float y = 1.0f - ((2.0f * pos.y()) / viewPortHeight);

    QVector4D ray = projMatrix.inverted() * QVector4D(x, y, -1.0f, 1.0f);
    ray.setZ(-1.0f);
    ray.setW(0.0f);
    ray = viewMatrix.inverted() * ray;
    return ray.toVector3D().normalized();
}

}
