/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef BEZIERHELPER_H
#define BEZIERHELPER_H

#include <QVector2D>

class BezierHelper
{
public:
    //http://devmag.org.za/2011/04/05/bzier-curves-a-tutorial/
    QVector2D CalculateBezierPoint(float t,
      QVector2D p0, QVector2D p1, QVector2D p2, QVector2D p3)
    {
      float u = 1.0f - t;
      float tt = t*t;
      float uu = u*u;
      float uuu = uu * u;
      float ttt = tt * t;

      QVector2D p = uuu * p0;
      p += 3 * uu * t * p1;
      p += 3 * u * tt * p2;
      p += ttt * p3;

      return p;
    }

    static float calculateBezier(float p0, float p1, float p2, float p3, float t)
    {
        auto p01 = lerp(p0, p1, t);
        auto p12 = lerp(p1, p2, t);
        auto p23 = lerp(p2, p3, t);
        auto p012 = lerp(p01, p12, t);
        auto p123 = lerp(p12, p23, t);
        auto final = lerp(p012, p123, t);

        return final;
    }

    template<class T, class U>
    static float lerp(const T& a,const T& b, const U& t)
    {
        return a + ((b - a) * t);
    }
};


#endif // BEZIERHELPER_H
