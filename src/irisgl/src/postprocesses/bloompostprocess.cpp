#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QColor>

#include "bloompostprocess.h"
#include "../graphics/postprocessmanager.h"
#include "../graphics/postprocess.h"
#include "../graphics/graphicshelper.h"
#include "../graphics/texture2d.h"
#include "../materials/propertytype.h"

#define BLUR_HORIZONTAL 0
#define BLUR_VERTICAL 1

// https://learnopengl.com/#!Advanced-Lighting/Bloom
namespace iris
{

BloomPostProcess::BloomPostProcess()
{
    thresholdShader = GraphicsHelper::loadShader(":assets/shaders/postprocesses/default.vs",
                                        ":assets/shaders/postprocesses/bloom_threshold.fs");

    blurShader = GraphicsHelper::loadShader(":assets/shaders/postprocesses/default.vs",
                                        ":assets/shaders/postprocesses/bloom_blur.fs");

    combineShader = GraphicsHelper::loadShader(":assets/shaders/postprocesses/default.vs",
                                        ":assets/shaders/postprocesses/bloom_combine.fs");

    threshold = Texture2D::create(100, 100);
    hBlur = Texture2D::create(100, 100);
    vBlur = Texture2D::create(100, 100);
    final = Texture2D::create(100, 100);
}

void BloomPostProcess::process(iris::PostProcessContext *ctx)
{
    //todo: only resize of the sizes are different
    auto screenWidth = ctx->sceneTexture->texture->width();
    auto screenHeight = ctx->sceneTexture->texture->height();

    final->resize(screenWidth, screenHeight);
    threshold->resize(screenWidth/4, screenHeight/4);
    hBlur->resize(screenWidth/4, screenHeight/4);
    vBlur->resize(screenWidth/4, screenHeight/4);

    // THRESHOLD
    ctx->sceneTexture->bind();
    thresholdShader->bind();
    thresholdShader->setUniformValue("u_sceneTexture", 0);
    thresholdShader->setUniformValue("threshold", 0.5f);
    ctx->manager->blit(ctx->sceneTexture, threshold, thresholdShader);
    //ctx->manager->blit(ctx->finalTexture, threshold, thresholdShader);

    // HORIZONTAL BLUR
    //threshold->bind();
    blurShader->bind();
    blurShader->setUniformValue("u_sceneTexture", 0);
    blurShader->setUniformValue("u_blurMode", BLUR_HORIZONTAL);
    ctx->manager->blit(threshold, hBlur, blurShader);

    // VERTICAL BLUR
    //threshold->bind();
    blurShader->bind();
    blurShader->setUniformValue("u_sceneTexture", 0);
    blurShader->setUniformValue("u_blurMode", BLUR_VERTICAL);
    ctx->manager->blit(hBlur, vBlur, blurShader);

    for(int i=0;i<5;i++)
    {
        // HORIZONTAL BLUR
        //threshold->bind();
        blurShader->bind();
        blurShader->setUniformValue("u_sceneTexture", 0);
        blurShader->setUniformValue("u_blurMode", BLUR_HORIZONTAL);
        ctx->manager->blit(vBlur, hBlur, blurShader);

        // VERTICAL BLUR
        //threshold->bind();
        blurShader->bind();
        blurShader->setUniformValue("u_sceneTexture", 0);
        blurShader->setUniformValue("u_blurMode", BLUR_VERTICAL);
        ctx->manager->blit(hBlur, vBlur, blurShader);
    }

    // COMBINE
    combineShader->bind();
    ctx->sceneTexture->bind(0);
    //ctx->finalTexture->bind(0);
    combineShader->setUniformValue("u_sceneTexture", 0);

    hBlur->bind(1);
    combineShader->setUniformValue("u_hBlurTexture", 1);

    vBlur->bind(2);
    combineShader->setUniformValue("u_vBlurTexture", 2);
    ctx->manager->blit(Texture2D::null(), final, combineShader);
    //ctx->manager->blit(hBlur, ctx->finalTexture, nullptr);
    combineShader->release();

    ctx->manager->blit(final, ctx->finalTexture);
}

QList<Property *> BloomPostProcess::getProperties()
{
    auto props = QList<Property*>();

    auto prop = new FloatProperty();
    prop->displayName = "Float Prop";
    prop->name = "float";
    props.append(prop);

    return props;
}

void BloomPostProcess::setProperty(Property *prop)
{

}

BloomPostProcessPtr BloomPostProcess::create()
{
    return BloomPostProcessPtr(new BloomPostProcess());
}

}
