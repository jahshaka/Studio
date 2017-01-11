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
#include <QSharedPointer>
#include "../libovr/Include/OVR_CAPI_GL.h"

#define OUTLINE_STENCIL_CHANNEL 1

class QOpenGLShaderProgram;
class QOpenGLFunctions_3_2_Core;
class QOpenGLContext;

namespace iris
{

class Scene;
class SceneNode;
class RenderData;
class Viewport;
class Mesh;
class BillboardMaterial;
class Billboard;
class FullScreenQuad;
class VrDevice;

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
    QSharedPointer<Scene> scene;

    /**
     * Sets the selected scene node.
     * This is only relevant to the editor.
     */
    QSharedPointer<SceneNode> selectedSceneNode;
    QOpenGLShaderProgram* lineShader;

    VrDevice* vrDevice;

public:

    /**
     * Sets selected scene node. If this node is a mesh, it is rendered in wireframe mode
     * as an overlay
     * @param activeNode
     */
    void setSelectedSceneNode(QSharedPointer<SceneNode> activeNode)
    {
        this->selectedSceneNode = activeNode;
    }

    void setScene(QSharedPointer<Scene> scene)
    {
        this->scene = scene;
    }

    //all scenenodes' transform should be updated before calling this functions
    void renderScene(QOpenGLContext* ctx, Viewport* vp, QMatrix4x4& vM, QMatrix4x4& pM);
    void renderSceneVr(QOpenGLContext* ctx, Viewport* vp);

    QOpenGLFunctions_3_2_Core* GLA;

    static QSharedPointer<ForwardRenderer> create(QOpenGLFunctions_3_2_Core* gl);

    bool isVrSupported();


    ~ForwardRenderer();

private:
    ForwardRenderer(QOpenGLFunctions_3_2_Core* gl);

    void renderNode(RenderData* renderData,QSharedPointer<SceneNode> node);
    void renderSky(RenderData* renderData);
    void renderBillboardIcons(RenderData* renderData);
    void renderSelectedNode(RenderData* renderData,QSharedPointer<SceneNode> node);


    void createLineShader();

    //editor-specific
    iris::Billboard* billboard;
    FullScreenQuad* fsQuad;

};

}

#endif // FORWARDRENDERER_H
