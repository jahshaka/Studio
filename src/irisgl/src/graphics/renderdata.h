/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef RENDERDATA_H
#define RENDERDATA_H

#include "../irisglfwd.h"
#include <QColor>

namespace iris
{

struct RenderData
{
    ScenePtr scene;
    MaterialPtr material;

    QMatrix4x4 viewMatrix;
    QMatrix4x4 projMatrix;

    QVector3D eyePos;

    //fog properties
    QColor fogColor;
    float fogStart;
    float fogEnd;
    bool fogEnabled;
};

}

#endif // RENDERDATA_H
