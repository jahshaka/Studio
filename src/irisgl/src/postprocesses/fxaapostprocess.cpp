#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QColor>

#include "fxaapostprocess.h"
#include "../graphics/postprocessmanager.h"
#include "../graphics/postprocess.h"
#include "../graphics/graphicshelper.h"
#include "../graphics/texture2d.h"
#include "../core/property.h"


namespace iris
{

FxaaPostProcess::FxaaPostProcess()
{
    name = "fxaa";
    displayName = "Fxaa Post Processing";

    tonemapShader = GraphicsHelper::loadShader(":assets/shaders/postprocesses/default.vs",
                                        ":assets/shaders/postprocesses/tonemapping.fs");

    fxaaShader = GraphicsHelper::loadShader(":assets/shaders/postprocesses/default.vs",
                                        ":assets/shaders/postprocesses/aa.fs");

    tonemapTex = Texture2D::create(100, 100);
    fxaaTex = Texture2D::create(100, 100);
}

void FxaaPostProcess::process(PostProcessContext *ctx)
{
    auto screenWidth = ctx->sceneTexture->texture->width();
    auto screenHeight = ctx->sceneTexture->texture->height();
    tonemapTex->resize(screenWidth, screenHeight);
    fxaaTex->resize(screenWidth, screenHeight);

    tonemapShader->bind();
    tonemapShader->setUniformValue("u_screenTex", 0);
    ctx->manager->blit(ctx->sceneTexture, tonemapTex, tonemapShader);
    tonemapShader->release();

    tonemapTex->texture->generateMipMaps();


    fxaaShader->bind();
    fxaaShader->setUniformValue("u_screenTex", 0);
    fxaaShader->setUniformValue("u_screenSize", QVector2D(1.0f/screenWidth, 1.0/screenHeight));
    ctx->manager->blit(tonemapTex, fxaaTex, fxaaShader);
    fxaaShader->release();

    ctx->manager->blit(fxaaTex, ctx->finalTexture);
}

FxaaPostProcessPtr FxaaPostProcess::create()
{
    return FxaaPostProcessPtr(new FxaaPostProcess());
}



}
