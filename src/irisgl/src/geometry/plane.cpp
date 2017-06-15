#include "plane.h"
#include "boundingsphere.h"


namespace iris {

SphereClassification Plane::classifySphere(BoundingSphere *sphere)
{
    auto& spherePos = sphere->pos;

    auto dist = normal.x() * spherePos.x() +
                normal.y() * spherePos.y() +
                normal.z() * spherePos.z() +
                d;

    if (dist < -sphere->radius)
        return SphereClassification::Behind;
    if (dist > sphere->radius)
        return SphereClassification::InFront;

    return SphereClassification::Intersects;

}

Plane::Plane(QVector3D planeNormal, float distance)
{
    normal = planeNormal;
    d = distance;
}



}
