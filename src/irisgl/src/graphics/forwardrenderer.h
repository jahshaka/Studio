/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef FORWARDRENDERER_H
#define FORWARDRENDERER_H

#include <QOpenGLContext>
#include "../irisglfwd.h"
#include "../libovr/Include/OVR_CAPI_GL.h"

class QOpenGLShaderProgram;
class QOpenGLFunctions_3_2_Core;
class QOpenGLContext;

namespace iris
{

/**
 * This is a basic forward renderer.
 * It currently has features specific for the editor which will be taken out in a future version.
 */
class ForwardRenderer
{
    QOpenGLFunctions_3_2_Core* gl;
    RenderData* renderData;

    /**
     * The scene to be rendered
     */
    ScenePtr scene;

    /**
     * Sets the selected scene node.
     * This is only relevant to the editor.
     */
    SceneNodePtr selectedSceneNode;
    QOpenGLShaderProgram* lineShader;

    VrDevice* vrDevice;

public:

    /**
     * Sets selected scene node. If this node is a mesh, it is rendered in wireframe mode
     * as an overlay
     * @param activeNode
     */
    void setSelectedSceneNode(SceneNodePtr activeNode)
    {
        this->selectedSceneNode = activeNode;
    }

    void setScene(ScenePtr scene)
    {
        this->scene = scene;
    }

    //all scenenodes' transform should be updated before calling this functions
    void renderScene(QOpenGLContext* ctx,Viewport* vp);
    void renderSceneVr(QOpenGLContext* ctx,Viewport* vp);

    static ForwardRendererPtr create(QOpenGLFunctions_3_2_Core* gl);

    bool isVrSupported();


    ~ForwardRenderer();

private:
    ForwardRenderer(QOpenGLFunctions_3_2_Core* gl);



    void renderNode(RenderData* renderData,SceneNodePtr node);
    void renderSky(RenderData* renderData);
    void renderBillboardIcons(RenderData* renderData);
    void renderSelectedNode(RenderData* renderData,SceneNodePtr node);


    void createLineShader();

    //editor-specific
    iris::Billboard* billboard;
    FullScreenQuad* fsQuad;

};

}

#endif // FORWARDRENDERER_H
