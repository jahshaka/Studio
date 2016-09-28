/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef GLOBALS_H
#define GLOBALS_H

#include <Qt3DRender/QLayer>

class Project;

class Globals
{
public:
    static float animFrameTime;
    static Project* project;
};

class RenderLayers
{
public:
    static Qt3DRender::QLayer* defaultLayer;
    static Qt3DRender::QLayer* billboardLayer;
    static Qt3DRender::QLayer* gizmoLayer;

    static void initRenderLayers()
    {
        defaultLayer= new Qt3DRender::QLayer();

        billboardLayer= new Qt3DRender::QLayer();

        gizmoLayer= new Qt3DRender::QLayer();
    }
};

#endif // GLOBALS_H
