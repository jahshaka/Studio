#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QColor>

#include "greyscalepostprocess.h"
#include "../graphics/postprocessmanager.h"
#include "../graphics/postprocess.h"
#include "../graphics/graphicshelper.h"
#include "../graphics/texture2d.h"

namespace iris
{

GreyscalePostProcess::GreyscalePostProcess()
{
    name = "greyscale";
    displayName = "GreyScale";
    shader = GraphicsHelper::loadShader(":assets/shaders/postprocesses/default.vs",
                                        ":assets/shaders/postprocesses/greyscale.fs");
    final = Texture2D::create(100, 100);
}

void GreyscalePostProcess::process(iris::PostProcessContext *ctx)
{
    shader->bind();
    shader->setUniformValue("u_sceneTexture", 0);
    ctx->manager->blit(ctx->sceneTexture, final, shader);
    shader->release();

    ctx->manager->blit(final, ctx->finalTexture, shader);
}

GreyscalePostProcessPtr GreyscalePostProcess::create()
{
    return GreyscalePostProcessPtr(new GreyscalePostProcess());
}

}


