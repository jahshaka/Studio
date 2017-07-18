#include "colormaterial.h"
#include "../graphics/material.h"

namespace iris {

QColor ColorMaterial::getColor() const
{
    return color;
}

void ColorMaterial::setColor(const QColor &value)
{
    color = value;
}

void ColorMaterial::begin(QOpenGLFunctions_3_2_Core *gl, ScenePtr scene)
{
    Material::begin(gl, scene);

    program->setUniformValue("color", color);
}

void ColorMaterial::end(QOpenGLFunctions_3_2_Core *gl, ScenePtr scene)
{
    Material::end(gl, scene);
}

ColorMaterial::ColorMaterial()
{
    this->acceptsLighting = false;
    this->setRenderLayer((int)RenderLayer::Opaque);
    color = QColor(255, 255, 255);
}



}
