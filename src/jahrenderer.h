/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#ifndef OVERLAYRENDERER_H
#define OVERLAYRENDERER_H

#include <Qt3DRender/qviewport.h>
#include <Qt3DRender/qcameraselector.h>
#include <Qt3DRender/qclearbuffers.h>
#include <Qt3DRender/qfilterkey.h>
#include <Qt3DRender/qlayerfilter.h>
#include <Qt3DRender/qfrustumculling.h>
#include <Qt3DRender/qrendersurfaceselector.h>
#include <Qt3DRender/qtechniquefilter.h>
#include <Qt3DRender/qrenderpassfilter.h>


using namespace Qt3DRender;

class JahRenderer:public Qt3DRender::QTechniqueFilter
{

    QRenderSurfaceSelector* renderSurface;
    QViewport* viewport;
    QCameraSelector* camera;

    //forward rendering
    QLayerFilter* forwardFilter;
    QClearBuffers* clearBuffers;
    QFrustumCulling* frustumCulling;
    QTechniqueFilter* forwardPass;

    //overlay
    QLayerFilter* overlayFilter;
    QClearBuffers* clearDepth;
    QTechniqueFilter* overlayPass;
public:
    JahRenderer(Qt3DCore::QNode* parent = nullptr);

    void setCamera(Qt3DCore::QEntity* cam);

    void setSurface(QObject* surface);
};

#endif // OVERLAYRENDERER_H
