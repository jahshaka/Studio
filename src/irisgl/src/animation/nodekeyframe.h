#ifndef NODEKEYFRAME_H
#define NODEKEYFRAME_H

#include "QVector3D"
#include "QQuaternion"
#include "keyframeanimation.h"

namespace iris
{

class NodeKeyFrame
{
public:
    QVector3D pos;
    QVector3D scale;
    QQuaternion rot;
    float time;

    NodeKeyFrame()
    {
        pos = QVector3D();
        scale = QVector3D(1,1,1);
        rot = QQuaternion();
        time = 0;
    }
};

}

#endif // NODEKEYFRAME_H
