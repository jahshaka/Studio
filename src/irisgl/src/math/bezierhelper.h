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
};


#endif // BEZIERHELPER_H
