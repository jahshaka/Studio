#include "linecolormaterial.h"
#include "../graphics/material.h"
#include "../graphics/shader.h"
#include "../graphics/graphicsdevice.h"
#include <QOpenGLShaderProgram>

namespace iris {

QColor LineColorMaterial::getColor() const
{
    return color;
}

void LineColorMaterial::setColor(const QColor &value)
{
    color = value;
}

void LineColorMaterial::setDepthBias(float offset)
{
	this->renderStates.rasterState.depthBias = offset;
}

void LineColorMaterial::begin(GraphicsDevicePtr device, ScenePtr scene)
{
    Material::begin(device, scene);
    //getProgram()->setUniformValue("color", color);
	device->setShaderUniform("color", color);
}

void LineColorMaterial::end(GraphicsDevicePtr device, ScenePtr scene)
{
    Material::end(device, scene);
}

LineColorMaterial::LineColorMaterial()
{
    this->acceptsLighting = false;
    this->setRenderLayer((int)RenderLayer::Opaque);
    color = QColor(255, 255, 255);

    createProgramFromShaderSource(":/assets/shaders/linecolor.vert",
                                  ":/assets/shaders/linecolor.frag");
}



}
