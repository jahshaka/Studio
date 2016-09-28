/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include <Qt3DRender/qviewport.h>
#include <Qt3DRender/qcameraselector.h>
#include <Qt3DRender/qclearbuffers.h>
#include <Qt3DRender/qfilterkey.h>
#include <Qt3DRender/qlayerfilter.h>
#include <Qt3DRender/qfrustumculling.h>
#include <Qt3DRender/qrendersurfaceselector.h>
#include <Qt3DRender/qtechniquefilter.h>
#include <Qt3DRender/qrenderpassfilter.h>
#include "jahrenderer.h"
#include "../globals.h"


using namespace Qt3DRender;


JahRenderer::JahRenderer(Qt3DCore::QNode* parent):
    Qt3DRender::QTechniqueFilter(parent)
{
    //
    renderSurface = new QRenderSurfaceSelector(this);
    viewport = new QViewport(renderSurface);
    viewport->setNormalizedRect(QRectF(0,0,1,1));

    camera = new QCameraSelector(viewport);

    clearBuffers = new QClearBuffers(camera);
    clearBuffers->setClearColor(Qt::white);
    //clearBuffers->setClearDepthValue(1);
    clearBuffers->setBuffers(QClearBuffers::ColorDepthBuffer);

    frustumCulling = new QFrustumCulling(clearBuffers);

    forwardPass = new QTechniqueFilter(frustumCulling);
    auto key = new QFilterKey();
    key->setName("renderingStyle");
    key->setValue("forward");
    forwardPass->addMatch(key);


/*
    //overlay rendering
    overlayFilter = new QLayerFilter(camera);
    clearDepth = new QClearBuffers(overlayFilter);
    clearDepth->setClearDepthValue(1);
    clearDepth->setBuffers(QClearBuffers::DepthBuffer);
    overlayFilter->addLayer(RenderLayers::gizmoLayer);
*/
}

void JahRenderer::setCamera(Qt3DCore::QEntity* cam)
{
    this->camera->setCamera(cam);
}

void JahRenderer::setSurface(QObject* surface)
{
    renderSurface->setSurface(surface);
}
