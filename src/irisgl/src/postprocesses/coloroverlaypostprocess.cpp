#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QColor>

#include "coloroverlaypostprocess.h"
#include "../graphics/postprocessmanager.h"
#include "../graphics/postprocess.h"
#include "../graphics/graphicshelper.h"
#include "../graphics/texture2d.h"
#include "../materials/propertytype.h"

namespace iris
{

QColor ColorOverlayPostProcess::getOverlayColor() const
{
    return overlayColor;
}

void ColorOverlayPostProcess::setOverlayColor(const QColor &value)
{
    overlayColor = value;
    col = QVector3D(value.redF(), value.greenF(), value.blueF());
}

ColorOverlayPostProcessPtr ColorOverlayPostProcess::create()
{
    return ColorOverlayPostProcessPtr(new ColorOverlayPostProcess());
}

ColorOverlayPostProcess::ColorOverlayPostProcess()
{
    name = "color_overlay";
    displayName = "Color Overlay";

    shader = GraphicsHelper::loadShader(":assets/shaders/postprocesses/default.vs",
                                        ":assets/shaders/postprocesses/coloroverlay.fs");

    //setOverlayColor(QColor(255,200,200));
    setOverlayColor(QColor(255,255,255));

    final = Texture2D::create(100, 100);
}

void ColorOverlayPostProcess::process(iris::PostProcessContext *ctx)
{
    //todo: only resize of the sizes are different
    auto screenWidth = ctx->sceneTexture->texture->width();
    auto screenHeight = ctx->sceneTexture->texture->height();
    final->resize(screenWidth, screenHeight);

    shader->bind();
    shader->setUniformValue("u_colorOverlay", col);
    shader->setUniformValue("u_sceneTexture", 0);
    ctx->manager->blit(ctx->sceneTexture, final, shader);
    shader->release();

    ctx->manager->blit(final, ctx->finalTexture);
}

QList<Property *> ColorOverlayPostProcess::getProperties()
{
    auto props = QList<Property*>();

    auto prop = new ColorProperty();
    prop->displayName = "Color";
    prop->name = "color";
    prop->value = overlayColor;
    props.append(prop);

    return props;
}

void ColorOverlayPostProcess::setProperty(Property *prop)
{
    if(prop->name == "color")
        this->setOverlayColor(prop->getValue().value<QColor>());
}

}


