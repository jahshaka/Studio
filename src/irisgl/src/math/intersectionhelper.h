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

class IntersectionHelper
{
public:
    //realtime collision detection page 178
    //assumes dir is normalized
    //returns whether or not a collision was made
    static bool raySphereIntersects(QVector3D p, QVector3D d,
                             QVector3D spherePos, float radius, float& t,
                             QVector3D& hitPoint)
    {
        QVector3D m = p - spherePos;
        float b = QVector3D::dotProduct(m, d);
        float c = QVector3D::dotProduct(m, m) - radius * radius;

        if(c>0 && b>0)
            return false;

        float discr = b*b-c;

        if(discr<0)
            return false;

        t = -b-qSqrt(discr);
        if(t<0)
            t = 0;
        hitPoint = p+t*d;

        return true;


    }
};

}

#endif // INTERSECTIONHELPER_H
