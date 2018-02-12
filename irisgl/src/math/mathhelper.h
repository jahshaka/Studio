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
#include <QtMath>

namespace iris
{

class MathHelper
{
public:

    // https://github.com/MonoGame/MonoGame/blob/develop/MonoGame.Framework/Matrix.cs#L1455
	// look into doing better decomposition:
	// https://github.com/qt/qt3d/blob/fb18ee8f05ff36f517ef2248539fda8a79c33f0e/src/core/transforms/qmath3d_p.h
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

	//https://stackoverflow.com/questions/1903954/is-there-a-standard-sign-function-signum-sgn-in-c-c
    template <typename T>
    static int sign(T val) {
        return (T(0) < val) - (val < T(0));
    }


    // Realtime Collision Detection, page 149
    static float closestPointBetweenSegments(QVector3D p1, QVector3D q1, QVector3D p2, QVector3D q2,
                                             float& s, float& t, QVector3D& c1, QVector3D& c2)
    {
        const float EPILSON = 0.000001f;
        QVector3D d1 = q1 - p1;
        QVector3D d2 = q2 - p2;
        QVector3D r  = p1 - p2;
        float a = QVector3D::dotProduct(d1, d1);
        float e = QVector3D::dotProduct(d2, d2);
        float f = QVector3D::dotProduct(d2, r);

        if (a <= EPILSON && e <= EPILSON) {
            s = 0;
            t = 0;
            c1 = p1;
            c2 = p2;
            return QVector3D::dotProduct(c1-c2, c1-c2);
        }
        if (a <= EPILSON) {
            s = 0;
            t = f/e;
            t = qBound(0.0f, t, 1.0f);
        } else {
            float c = QVector3D::dotProduct(d1, r);
            if (e <= EPILSON) {
                t = 0;
                s = qBound(0.0f, -c/a, 1.0f);
            } else {
                float b = QVector3D::dotProduct(d1, d2);
                float denom = a*e - b*b;

                if (denom != 0) {
                    s = qBound(0.0f, (b*f - c*e)/denom, 1.0f);
                } else {
                    s = 0;
                }

                t = (b*s + f) / e;

                if (t < 0) {
                    t = 0;
                    s = qBound(0.0f, -c/a, 1.0f);
                } else if (t > 1) {
                    t = 1.0f;
                    s = qBound(0.0f, (b-c)/a, 1.0f);
                }
            }
        }

        c1 = p1 + d1 * s;
        c2 = p2 + d2 * t;

        return QVector3D::dotProduct(c1 - c2, c1 - c2);
    }
};

}

#endif // MATHHELPER_H
