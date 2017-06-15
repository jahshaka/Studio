#include "frustum.h"
#include "plane.h"
#include "boundingsphere.h"

namespace iris {

// https://github.com/playcanvas/engine/blob/master/src/shape/frustum.js#L27
void Frustum::build(QMatrix4x4 viewProj)
{
    planes.clear();

    // left
    auto plane = viewProj.row(3) + viewProj.row(0);
    planes.append(Plane(plane.toVector3D().normalized(), plane.w()/plane.toVector3D().length()));

    // right
    plane = viewProj.row(3) - viewProj.row(0);
    planes.append(Plane(plane.toVector3D().normalized(), plane.w()/plane.toVector3D().length()));

    // top
    plane = viewProj.row(3) + viewProj.row(1);
    planes.append(Plane(plane.toVector3D().normalized(), plane.w()/plane.toVector3D().length()));

    // bottom
    plane = viewProj.row(3) - viewProj.row(1);
    planes.append(Plane(plane.toVector3D().normalized(), plane.w()/plane.toVector3D().length()));

    // front
    plane = viewProj.row(3) + viewProj.row(2);
    planes.append(Plane(plane.toVector3D().normalized(), plane.w()/plane.toVector3D().length()));

    // back
    plane = viewProj.row(3) - viewProj.row(2);
    planes.append(Plane(plane.toVector3D().normalized(), plane.w()/plane.toVector3D().length()));

}

bool Frustum::isSphereInside(BoundingSphere *sphere)
{
    for(auto& plane : planes) {
        if (plane.classifySphere(sphere) == SphereClassification::Behind)
            return false;
    }

    return true;
}



}
