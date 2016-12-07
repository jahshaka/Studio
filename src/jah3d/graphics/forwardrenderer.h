#ifndef FORWARDRENDERER_H
#define FORWARDRENDERER_H

#include <QOpenGLContext>
#include <QSharedPointer>
#include "../libovr/Include/OVR_CAPI_GL.h"

class QOpenGLShaderProgram;
class QOpenGLFunctions_3_2_Core;

namespace jah3d
{

class Scene;
class SceneNode;
class RenderData;
class Viewport;
class Mesh;
class BillboardMaterial;
class Billboard;

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
    void renderScene(Viewport* vp);
    void renderSceneVr(Viewport* vp);

    static QSharedPointer<ForwardRenderer> create(QOpenGLFunctions_3_2_Core* gl);

private:
    ForwardRenderer(QOpenGLFunctions_3_2_Core* gl);


    void renderNode(RenderData* renderData,QSharedPointer<SceneNode> node);
    void renderSky(RenderData* renderData);
    void renderBillboardIcons(RenderData* renderData);
    void renderSelectedNode(RenderData* renderData,QSharedPointer<SceneNode> node);

    //VR
    ovrSession session;
    ovrGraphicsLuid luid;
    ovrHmdDesc hmdDesc;

    void initOVR();
    GLuint createDepthTexture(int width,int height);
    ovrTextureSwapChain createTextureChain(ovrSession session,ovrTextureSwapChain &swapChain,int width,int height);
    GLuint createMirrorFbo();

    GLuint vr_depthTexture[2];
    ovrTextureSwapChain vr_textureChain[2];
    GLuint vr_Fbo[2];

    int eyeWidth;
    int eyeHeight;
    long long frameIndex;

    //FullscreenQuad* fsQuad;
    GLuint vr_mirrorFbo;
    GLuint vr_mirrorTexId;

    //quick bool to enable/disable vr rendering
    bool renderVr;


    void createLineShader();

    //editor-specific
    jah3d::Billboard* billboard;

};

}

#endif // FORWARDRENDERER_H
