#ifndef FORWARDRENDERER_H
#define FORWARDRENDERER_H

#include <QOpenGLContext>
#include <QSharedPointer>

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

    static QSharedPointer<ForwardRenderer> create(QOpenGLFunctions_3_2_Core* gl);

private:
    ForwardRenderer(QOpenGLFunctions_3_2_Core* gl);


    void renderNode(RenderData* renderData,QSharedPointer<SceneNode> node);
    void renderBillboardIcons(RenderData* renderData);
    void renderSelectedNode(RenderData* renderData,QSharedPointer<SceneNode> node);

    void createLineShader();

    //editor-specific
    jah3d::Billboard* billboard;
    //void createBillboardIconAssets();

};

}

#endif // FORWARDRENDERER_H
