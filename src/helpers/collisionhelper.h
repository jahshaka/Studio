/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#ifndef COLLISIONHELPER_H
#define COLLISIONHELPER_H

#include <QVector3D>


struct Plane
{
    QVector3D normal;
    float d;

    Plane(QVector3D normal,float d)
    {
        this->normal = normal;
        this->d = d;
    }

    Plane()
    {
    }

    QVector3D rayIntersect(const QVector3D &rayOrigin, const QVector3D &ray,float &t)
    {
        float divisor = QVector3D::dotProduct(ray, normal);
        if (qFuzzyCompare(1.0f, 1.0f + divisor)) {
            t = -1.0f;
            return rayOrigin;
        }

        t = -(QVector3D::dotProduct(rayOrigin, normal) - d) / divisor;

        return rayOrigin + ray * t;

    }
};

//https://github.com/CedricGuillemet/LibGizmo/blob/master/src/libgizmo/ZCollisionsUtils.h
class CollisionHelper
{
public:
    static bool CollisionClosestPointOnSegment(QVector3D point, QVector3D vertPos1, QVector3D vertPos2,QVector3D& res )
    {

        auto c = point - vertPos1;
        QVector3D V = (vertPos2 - vertPos1).normalized();

        float d = (vertPos2 - vertPos1).length();
        float t = QVector3D::dotProduct(V,c);

        if (t < 0)
        {
            res = vertPos1;
            return false;//vertPos1;
        }

        if (t > d)
        {
            res = vertPos2;
            return false;//vertPos2;
        }

        res = vertPos1 + V * t;
        return true;
    }
};

#endif // COLLISIONHELPER_H
