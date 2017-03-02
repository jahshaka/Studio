/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATHHELPER_H
#define MATHHELPER_H

#include <QMatrix4x4>
#include <QVector3D>
#include <QQuaternion>

namespace iris
{

class MathHelper
{
public:

    static void decomposeMatrix(const QMatrix4x4& matrix, QVector3D& pos, QQuaternion& rot, QVector3D& scale)
    {
        pos = matrix.column(3).toVector3D();
        rot = QQuaternion::fromRotationMatrix(matrix.normalMatrix());

        scale.setX(matrix.column(0).toVector3D().length());
        scale.setY(matrix.column(1).toVector3D().length());
        scale.setZ(matrix.column(2).toVector3D().length());
    }

    static float lerp(float norm, float min, float max) {
        return (max - min) * norm + min;
    }
};

}

#endif // MATHHELPER_H
