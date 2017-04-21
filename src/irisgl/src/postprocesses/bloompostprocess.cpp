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

    bloomThreshold = 0.5f;
    bloomStrength = 0.5f;
    dirtStrength = 2.0f;
}

void BloomPostProcess::process(iris::PostProcessContext *ctx)
{
    //todo: only resize of the sizes are different
    auto screenWidth = ctx->sceneTexture->texture->width();
    auto screenHeight = ctx->sceneTexture->texture->height();

    int div = 16;
    final->resize(screenWidth, screenHeight);
    threshold->resize(screenWidth/div, screenHeight/div);
    hBlur->resize(screenWidth/div, screenHeight/div);
    vBlur->resize(screenWidth/div, screenHeight/div);

    // THRESHOLD
    ctx->sceneTexture->bind();
    thresholdShader->bind();
    thresholdShader->setUniformValue("u_sceneTexture", 0);
    thresholdShader->setUniformValue("threshold", bloomThreshold);
    //ctx->manager->blit(ctx->sceneTexture, threshold, thresholdShader);
    ctx->manager->blit(ctx->finalTexture, threshold, thresholdShader);

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

    for(int i=0;i<10;i++)
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
    //ctx->sceneTexture->bind(0);
    ctx->finalTexture->bind(0);
    combineShader->setUniformValue("u_sceneTexture", 0);

    hBlur->bind(1);
    combineShader->setUniformValue("u_hBlurTexture", 1);

    vBlur->bind(2);
    combineShader->setUniformValue("u_vBlurTexture", 2);

    if (!!dirtyLens) {
        dirtyLens->bind(3);
        combineShader->setUniformValue("u_dirtTexture", 3);
        combineShader->setUniformValue("u_useDirt", true);
        combineShader->setUniformValue("u_dirtStrength", dirtStrength);
    } else {
        combineShader->setUniformValue("u_useDirt", false);
    }

    combineShader->setUniformValue("u_bloomStrength", bloomStrength);
    ctx->manager->blit(Texture2D::null(), final, combineShader);
    //ctx->manager->blit(hBlur, ctx->finalTexture, nullptr);
    combineShader->release();

    ctx->manager->blit(final, ctx->finalTexture);
}

QList<Property *> BloomPostProcess::getProperties()
{
    auto props = QList<Property*>();

    auto prop = new FloatProperty();
    prop->displayName = "Threshold";
    prop->name = "threshold";
    prop->value = bloomThreshold;
    prop->minValue = 0.0f;
    prop->maxValue = 1.0f;
    props.append(prop);

    prop = new FloatProperty();
    prop->displayName = "Bloom Intensity";
    prop->name = "bloom_intensity";
    prop->value = bloomStrength;
    prop->minValue = 0.0f;
    prop->maxValue = 1.0f;
    props.append(prop);

    auto texProp = new TextureProperty();
    texProp->displayName = "Dirty Lens";
    texProp->name = "dirty_lens";
    texProp->value = "";
    props.append(texProp);

    prop = new FloatProperty();
    prop->displayName = "Dirt Intensity";
    prop->name = "dirt_intensity";
    prop->value = dirtStrength;
    prop->minValue = 0.0f;
    prop->maxValue = 5.0f;
    props.append(prop);

    return props;
}

void BloomPostProcess::setProperty(Property *prop)
{
    if(prop->name == "threshold")
        bloomThreshold = prop->getValue().toFloat();
    else if(prop->name == "bloom_intensity")
        bloomStrength = prop->getValue().toFloat();
    else if(prop->name == "dirt_intensity")
        dirtStrength = prop->getValue().toFloat();
    else if(prop->name == "dirty_lens") {
        if(!prop->name.isEmpty())
            dirtyLens = Texture2D::load(prop->getValue().toString());
        else
            dirtyLens.clear();
    }
}

BloomPostProcessPtr BloomPostProcess::create()
{
    return BloomPostProcessPtr(new BloomPostProcess());
}

}
