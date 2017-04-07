#ifndef POSTPROCESSMANAGER_H
#define POSTPROCESSMANAGER_H

#include "../irisglfwd.h"

class QOpenGLShaderProgram;

namespace iris {

class PostProcess;

class PostProcessContext
{
public:
    Texture2DPtr sceneTexture;
    Texture2DPtr finalTexture;
};

class PostProcessManager
{
    bool enabled;
    QList<PostProcess*> postProcesses;
    RenderTargetPtr renderTarget;

public:
    PostProcessManager()
    {

    }

    void blit(Texture2DPtr source, Texture2DPtr dest, QOpenGLShaderProgram* program);

    void apply();
};
}

#endif // POSTPROCESSMANAGER_H
