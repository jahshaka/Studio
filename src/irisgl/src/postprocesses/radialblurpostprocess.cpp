#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QColor>

#include "radialblurpostprocess.h"
#include "../graphics/postprocessmanager.h"
#include "../graphics/postprocess.h"
#include "../graphics/graphicshelper.h"

namespace iris
{

RadialBlurPostProcess::RadialBlurPostProcess()
{
    shader = GraphicsHelper::loadShader(":assets/shaders/postprocesses/default.vs",
                                        ":assets/shaders/postprocesses/radial_blur.fs");
}

void RadialBlurPostProcess::process(PostProcessContext *ctx)
{
    shader->bind();
    shader->setUniformValue("u_sceneTexture", 0);
    ctx->manager->blit(ctx->sceneTexture, ctx->finalTexture, shader);
    shader->release();
}

}
