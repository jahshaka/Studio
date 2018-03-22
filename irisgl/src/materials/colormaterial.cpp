#include "colormaterial.h"
#include "../graphics/material.h"
#include "../graphics/shader.h"
#include "../graphics/graphicsdevice.h"
#include <QOpenGLShaderProgram>

namespace iris {

QColor ColorMaterial::getColor() const
{
    return color;
}

void ColorMaterial::setColor(const QColor &value)
{
    color = value;
}

void ColorMaterial::begin(GraphicsDevicePtr device, ScenePtr scene)
{
    Material::begin(device, scene);
    //getProgram()->setUniformValue("color", color);
	device->setShaderUniform("color", color);
}

void ColorMaterial::end(GraphicsDevicePtr device, ScenePtr scene)
{
    Material::end(device, scene);
}

ColorMaterial::ColorMaterial()
{
    this->acceptsLighting = false;
    this->setRenderLayer((int)RenderLayer::Opaque);
    color = QColor(255, 255, 255);

    createProgramFromShaderSource(":/assets/shaders/color.vert",
                                  ":/assets/shaders/color.frag");
}



}
