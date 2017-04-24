#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QColor>

#include "greyscalepostprocess.h"
#include "../graphics/postprocessmanager.h"
#include "../graphics/postprocess.h"
#include "../graphics/graphicshelper.h"

namespace iris
{

GreyscalePostProcess::GreyscalePostProcess()
{
    name = "greyscale";
    shader = GraphicsHelper::loadShader(":assets/shaders/postprocesses/default.vs",
                                        ":assets/shaders/postprocesses/greyscale.fs");
}

void GreyscalePostProcess::process(iris::PostProcessContext *ctx)
{
    shader->bind();
    shader->setUniformValue("u_sceneTexture", 0);
    ctx->manager->blit(ctx->sceneTexture, ctx->finalTexture, shader);
    shader->release();
}

}


