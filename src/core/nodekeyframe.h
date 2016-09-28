/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef NODEKEYFRAME_H
#define NODEKEYFRAME_H

#include "QVector3D"
#include "QQuaternion"
#include "keyframeanimation.h"

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

#endif // NODEKEYFRAME_H
