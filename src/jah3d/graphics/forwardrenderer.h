#ifndef FORWARDRENDERER_H
#define FORWARDRENDERER_H


//#include "../core/scene.h"
//#include "../core/scenenode.h"
#include <QOpenGLContext>
#include <QSharedPointer>

namespace jah3d
{

class Scene;
class SceneNode;
class RenderData;
class Viewport;

class ForwardRenderer
{
    QOpenGLFunctions* gl;
    RenderData* renderData;

public:
    //all scene's transform should be updated
    void renderScene(Viewport* vp,QSharedPointer<Scene> scene);
    static QSharedPointer<ForwardRenderer> create(QOpenGLFunctions* gl);

private:
    void renderNode(RenderData* renderData,QSharedPointer<SceneNode> node);
    ForwardRenderer(QOpenGLFunctions* gl);
};

}

#endif // FORWARDRENDERER_H
