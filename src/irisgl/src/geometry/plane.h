#ifndef PLANE_H
#define PLANE_H

#include <QVector3D>
#include <QVector4D>

namespace iris
{
class BoundingSphere;

enum SphereClassification
{
    InFront,
    Intersects,
    Behind
};

class Plane
{
public:
    QVector3D normal;
    float d;

    Plane(QVector3D planeNormal, float distance);

    SphereClassification classifySphere(BoundingSphere* sphere);
};

}

#endif // PLANE_H
