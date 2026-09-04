/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef COLLISIONHELPER_H
#define COLLISIONHELPER_H

#include "irisgl/core/math/vec.h"


struct Plane
{
    iris::Vec3 normal;
    float d;

    Plane(iris::Vec3 normal,float d)
    {
        this->normal = normal;
        this->d = d;
    }

    Plane()
    {
    }

    iris::Vec3 rayIntersect(const iris::Vec3 &rayOrigin, const iris::Vec3 &ray,float &t)
    {
        float divisor = iris::Vec3::dotProduct(ray, normal);
        if (qFuzzyCompare(1.0f, 1.0f + divisor)) {
            t = -1.0f;
            return rayOrigin;
        }

        t = -(iris::Vec3::dotProduct(rayOrigin, normal) - d) / divisor;

        return rayOrigin + ray * t;

    }
};

//https://github.com/CedricGuillemet/LibGizmo/blob/master/src/libgizmo/ZCollisionsUtils.h
class CollisionHelper
{
public:
    static bool CollisionClosestPointOnSegment(iris::Vec3 point, iris::Vec3 vertPos1, iris::Vec3 vertPos2,iris::Vec3& res )
    {

        auto c = point - vertPos1;
        iris::Vec3 V = (vertPos2 - vertPos1).normalized();

        float d = (vertPos2 - vertPos1).length();
        float t = iris::Vec3::dotProduct(V,c);

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
