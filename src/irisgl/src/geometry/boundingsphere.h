#ifndef BOUNDINGSPHERE_H
#define BOUNDINGSPHERE_H

#include <QVector3D>

namespace iris
{

class BoundingSphere
{
public:
    QVector3D pos;
    float radius;

    void expand(QVector3D point)
    {
        auto dist = pos.distanceToPoint(point);
        if (dist > radius)
            radius =dist;
    }

    bool intersects(BoundingSphere* other)
    {
        return pos.distanceToPoint(other->pos) < radius + other->radius;
    }
};

}

#endif // BOUNDINGSPHERE_H
