/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef INTERSECTIONHELPER_H
#define INTERSECTIONHELPER_H

#include <QVector3D>
#include <qmath.h>

namespace iris
{

struct Plane {
    QVector3D n; // Plane normal. Points x on the plane satisfy Dot(n,x) = d
    float d; // d = dot(n,p) for a given point p on the plane
};

class IntersectionHelper
{
public:
    // Given three noncollinear points (ordered ccw), compute plane equation
    static Plane computePlaneND(QVector3D a, QVector3D b, QVector3D c) {
        Plane p = { QVector3D::crossProduct(b - a, c - a).normalized(),
                    QVector3D::dotProduct(p.n, a) };
        return p;
    }

    // realtime collision detection page 178
    // assumes dir is normalized
    // returns whether or not a collision was made
    static bool raySphereIntersects(QVector3D p,
                                    QVector3D d,
                                    QVector3D spherePos,
                                    float radius,
                                    float& t,
                                    QVector3D& hitPoint)
    {
        QVector3D m = p - spherePos;
        float b = QVector3D::dotProduct(m, d);
        float c = QVector3D::dotProduct(m, m) - radius * radius;

        if (c > 0 && b > 0) return false;

        float discr = b * b - c;

        if (discr < 0) return false;

        t = -b -qSqrt(discr);

        if (t < 0) t = 0;

        hitPoint = p + t * d;

        return true;
    }

    // realtime collision detection page 175
    static int intersectSegmentPlane(QVector3D a, QVector3D b, Plane p, float &t, QVector3D &q) {
        // Compute the t value for the directed line ab intersecting the plane
        QVector3D ab = b - a;
        t = (p.d - QVector3D::dotProduct(p.n, a)) / QVector3D::dotProduct(p.n, ab);
        // If t in [0..1] compute and return intersection point
        if (t >= 0.0f && t <= 1.0f) {
            q = a + t * ab;
            return 1;
        }
        // Else no intersection
        return 0;
    }
};

}

#endif // INTERSECTIONHELPER_H
