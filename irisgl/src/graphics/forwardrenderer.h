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
//#include "../libovr/Include/OVR_CAPI_GL.h"
#include "../irisglfwd.h"

#include "particle.h"
#include "particlerender.h"

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
class PostProcessManager;
class PostProcessContext;
class PerformanceTimer;

struct LightUniformNames
{
	std::string color;
	std::string type;
	std::string position;
	std::string distance;
	std::string direction;
	std::string cutOffAngle;
	std::string cutOffSoftness;
	std::string intensity;
	std::string shadowColor;
	std::string shadowAlpha;
	std::string constantAtten;
	std::string linearAtten;
	std::string quadAtten;
	std::string shadowType;
	std::string shadowMap;
	std::string shadowMatrix;
};

/**
 * This is a basic forward renderer.
 * It currently has features specific for the editor which will be taken out in a future version.
 */
class ForwardRenderer
{
    QOpenGLFunctions_3_2_Core* gl;
    GraphicsDevicePtr graphics;

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
    QOpenGLShaderProgram* skinnedLineShader;
    QOpenGLShaderProgram* shadowShader;
    QOpenGLShaderProgram* skinnedShadowShader;
    ShaderPtr particleShader;
    QOpenGLShaderProgram* emitterShader;

    PostProcessManagerPtr postMan;
    PostProcessContext* postContext;

    VrDevice* vrDevice;

    RenderTargetPtr renderTarget;
    Texture2DPtr sceneRenderTexture;
    Texture2DPtr depthRenderTexture;
    Texture2DPtr finalRenderTexture;

    PerformanceTimer* perfTimer;
	QVector<LightUniformNames> lightUniformNames;

public:

    bool renderLightBillboards;

    GraphicsDevicePtr getGraphicsDevice();

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
    void renderSceneToRenderTarget(RenderTargetPtr rt, CameraNodePtr cam, bool clearRenderLists = false, bool applyPostProcesses = true);
    void renderScene(float delta, Viewport* vp);
    void renderSceneVr(float delta, Viewport* vp, bool useViewer = true);

    PostProcessManagerPtr getPostProcessManager();

    static ForwardRendererPtr create(bool useVr = true, bool physicsEnabled = false);

    bool isVrSupported();

    ~ForwardRenderer();

private:
    ForwardRenderer(bool supportsVr = true, bool physicsEnabled = false);

    void renderNode(RenderData* renderData, ScenePtr node);
    void renderSky(RenderData* renderData);
    void renderBillboardIcons(RenderData* renderData);
    void renderSelectedNode(RenderData* renderData, SceneNodePtr node);

    void renderOutlineNode(RenderData* renderData, SceneNodePtr node);
    void renderOutlineLine(RenderData* renderData, SceneNodePtr node);

    void createLineShader();
    void createParticleShader();
    void createEmitterShader();

    GLuint shadowFBO;
    GLuint shadowDepthMap;

    void createShadowShader();
    void renderShadows(ScenePtr node);
    void renderDirectionalShadow(LightNodePtr lightNode,ScenePtr node);
    void renderSpotlightShadow(LightNodePtr lightNode,ScenePtr node);
    void generateShadowBuffer(GLuint size = 1024);

	void generateLightUnformNames();

    //editor-specific
    iris::Billboard* billboard;
    FullScreenQuad* fsQuad;
};

}

#endif // FORWARDRENDERER_H
