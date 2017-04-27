#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QColor>

#include "coloroverlaypostprocess.h"
#include "../graphics/postprocessmanager.h"
#include "../graphics/postprocess.h"
#include "../graphics/graphicshelper.h"
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
}

void ColorOverlayPostProcess::process(iris::PostProcessContext *ctx)
{
    shader->bind();
    shader->setUniformValue("u_colorOverlay", col);
    shader->setUniformValue("u_sceneTexture", 0);
    ctx->manager->blit(ctx->sceneTexture, ctx->finalTexture, shader);
    shader->release();
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


