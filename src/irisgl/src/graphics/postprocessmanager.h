#ifndef POSTPROCESSMANAGER_H
#define POSTPROCESSMANAGER_H

#include "../irisglfwd.h"

class QOpenGLShaderProgram;
class QOpenGLFunctions_3_2_Core;

namespace iris {

class PostProcess;
class FullScreenQuad;
class PostProcessManager;

class PostProcessContext
{
public:
    Texture2DPtr depthTexture;
    Texture2DPtr sceneTexture;

    Texture2DPtr finalTexture;

    PostProcessManager* manager;
};

class PostProcessManager
{
    bool enabled;
    QList<PostProcess*> postProcesses;
    RenderTargetPtr renderTarget;
    bool rtInitialized;

    QOpenGLFunctions_3_2_Core* gl;
    FullScreenQuad* fsQuad;

public:
    PostProcessManager();

    void addPostProcess(PostProcess* process);
    QList<PostProcess*> getPostProcesses();

    void blit(Texture2DPtr source, Texture2DPtr dest, QOpenGLShaderProgram* program = nullptr);

    void process(PostProcessContext* context);

private:
    void initRenderTarget();
};
}

#endif // POSTPROCESSMANAGER_H
