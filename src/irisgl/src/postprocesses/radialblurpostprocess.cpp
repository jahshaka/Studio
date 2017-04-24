#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QColor>

#include "radialblurpostprocess.h"
#include "../graphics/postprocessmanager.h"
#include "../graphics/postprocess.h"
#include "../graphics/graphicshelper.h"
#include "../materials/propertytype.h"
#include "../graphics/texture2d.h"

namespace iris
{

RadialBlurPostProcess::RadialBlurPostProcess()
{
    name = "radial_blur";

    shader = GraphicsHelper::loadShader(":assets/shaders/postprocesses/default.vs",
                                        ":assets/shaders/postprocesses/radial_blur.fs");
    blurSize = 1.0f;

    final = Texture2D::create(100, 100);
}

void RadialBlurPostProcess::process(PostProcessContext *ctx)
{
    auto screenWidth = ctx->sceneTexture->texture->width();
    auto screenHeight = ctx->sceneTexture->texture->height();
    final->resize(screenWidth, screenHeight);

    shader->bind();
    shader->setUniformValue("u_sceneTexture", 0);
    shader->setUniformValue("u_blurSize", blurSize);
    ctx->manager->blit(ctx->finalTexture, final, shader);

    ctx->manager->blit(final, ctx->finalTexture, shader);

    shader->release();
}

QList<Property *> RadialBlurPostProcess::getProperties()
{
    auto props = QList<Property*>();

    auto prop = new FloatProperty();
    prop->displayName = "Blur Scale";
    prop->name = "blur_scale";
    prop->value = 1.0f;
    prop->minValue = 0.1f;
    prop->maxValue = 10.0f;
    //prop->id = 1;
    props.append(prop);

    return props;
}

void RadialBlurPostProcess::setProperty(Property *prop)
{
    if(prop->name == "blur_scale") {
        blurSize = ((FloatProperty*)prop)->value;
    }
}

RadialBlurPostProcessPtr RadialBlurPostProcess::create()
{
    return RadialBlurPostProcessPtr(new RadialBlurPostProcess());
}

}
