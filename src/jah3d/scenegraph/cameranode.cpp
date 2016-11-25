#include "cameranode.h"
#include <QVector3D>
#include <QPoint>

namespace jah3d
{

QVector3D CameraNode::calculatePickingDirection(int viewPortWidth, int viewPortHeight,QPointF pos)
{
    float x = ((2.0f * pos.x()) / viewPortWidth) - 1.0f;
    float y = 1.0f - ((2.0f * pos.y()) / viewPortHeight);

    QVector4D ray = projMatrix.inverted() * QVector4D(x, y, -1.0f, 1.0f);
    ray.setZ(-1.0f);
    ray.setW(0.0f);
    ray = viewMatrix.inverted() * ray;
    return ray.toVector3D().normalized();
}

}
