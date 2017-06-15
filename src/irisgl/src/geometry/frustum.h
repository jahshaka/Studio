#ifndef FRUSTUM_H
#define FRUSTUM_H

#include "plane.h"

namespace iris {

class BoundingSphere;
class Frustum
{
public:
    QList<Plane> planes;

    // projection x view
    void build(QMatrix4x4 viewProj);

    // checks if the sphere is inside or touches the bounding sphere
    bool isSphereInside(BoundingSphere* sphere);
};

}

#endif // FRUSTUM_H
