#include "translationgizmo.h"

bool TranslationHandle::isHit(QVector3D rayPos, QVector3D rayDir)
{
    auto gizmoTrans = gizmo->getTransform();

    // calculate world space position of the segment representing the handle
    auto p1 = gizmoTrans * QVector3D(0,0,0);
    auto q1 = gizmoTrans * handleExtent;

    auto p2 = rayPos;
    auto q2 = rayPos + rayDir * 1000;

    float s,t;
    QVector3D c1, c2;
    auto dist = iris::MathHelper::closestPointBetweenSegments(p1, q1, p2, q2, s,t,c1,c2);
    if (dist < 1.0f) {
        return true;
    }

    return false;
}

QVector3D TranslationHandle::getHitPos(QVector3D rayPos, QVector3D rayDir)
{
    auto gizmoTrans = gizmo->getTransform();

    // calculate world space position of the segment representing the handle
    auto p1 = gizmoTrans * (-handleExtent * 10000);
    // extend the handle a lot further
    auto q1 = gizmoTrans * (handleExtent * 10000);

    auto p2 = rayPos;
    auto q2 = rayPos + rayDir * 1000;

    float s,t;
    QVector3D c1, c2;
    auto dist = iris::MathHelper::closestPointBetweenSegments(p1, q1, p2, q2, s,t,c1,c2);

    return c1;
}
