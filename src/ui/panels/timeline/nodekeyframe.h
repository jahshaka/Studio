/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef NODEKEYFRAME_H
#define NODEKEYFRAME_H

#include <QQuaternion>
#include "QVector3D"
#include "QQuaternion"
#include "ui/panels/timeline/timelinekeys.h"

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
