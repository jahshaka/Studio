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

    // https://github.com/MonoGame/MonoGame/blob/develop/MonoGame.Framework/Matrix.cs#L1455
    static void decomposeMatrix(const QMatrix4x4& matrix, QVector3D& pos, QQuaternion& rot, QVector3D& scale)
    {
        pos = matrix.column(3).toVector3D();
        rot = QQuaternion::fromRotationMatrix(matrix.normalMatrix());

        auto col = matrix.column(0);
        auto sx = sign(col.x() * col.y() * col.z() * col.w()) < 0 ? -1 : 1;

        col = matrix.column(1);
        auto sy = sign(col.x() * col.y() * col.z() * col.w()) < 0 ? -1 : 1;

        col = matrix.column(2);
        auto sz = sign(col.x() * col.y() * col.z() * col.w()) < 0 ? -1 : 1;

        scale.setX(sx * matrix.column(0).toVector3D().length());
        scale.setY(sy * matrix.column(1).toVector3D().length());
        scale.setZ(sz * matrix.column(2).toVector3D().length());
    }

    static float lerp(float norm, float min, float max) {
        return (max - min) * norm + min;
    }

    template <typename T>
    static int sign(T val) {
        return (T(0) < val) - (val < T(0));
    }
};

}

#endif // MATHHELPER_H
