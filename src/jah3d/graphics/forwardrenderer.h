#ifndef FORWARDRENDERER_H
#define FORWARDRENDERER_H

#include <QOpenGLContext>
#include <QSharedPointer>

class QOpenGLShaderProgram;

namespace jah3d
{

class Scene;
class SceneNode;
class RenderData;
class Viewport;
class Mesh;
class BillboardMaterial;
class Billboard;

class ForwardRenderer
{
    QOpenGLFunctions* gl;
    RenderData* renderData;

public:
    //all scenenodes' transform should be updated before calling this functions
    void renderScene(Viewport* vp,QSharedPointer<Scene> scene);
    static QSharedPointer<ForwardRenderer> create(QOpenGLFunctions* gl);

private:
    ForwardRenderer(QOpenGLFunctions* gl);


    void renderNode(RenderData* renderData,QSharedPointer<SceneNode> node);
    void renderBillboardIcons(RenderData* renderData);

    //editor-specific
    jah3d::Billboard* billboard;
    //void createBillboardIconAssets();

};

}

#endif // FORWARDRENDERER_H
