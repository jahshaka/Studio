#include "defaultskymaterial.h"


namespace jah3d
{

DefaultSkyMaterial::DefaultSkyMaterial()
{
    createProgramFromShaderSource("app/shaders/defaultsky.vs","app/shaders/defaultsky.vs");
}

void DefaultSkyMaterial::setSkyTexture(Texture2DPtr tex)
{
    texture = tex;
    this->addTexture("texture",tex);
}

void DefaultSkyMaterial::clearSkyTexture()
{
    texture.clear();
    removeTexture("texture");
}

void DefaultSkyMaterial::setSkyColor(QColor color)
{
    this->color = color;
}

QColor DefaultSkyMaterial::getSkyColor()
{
    return color;
}

void DefaultSkyMaterial::begin(QOpenGLFunctions_3_2_Core* gl)
{
    Material::begin(gl);
    this->setUniformValue("color",color);

}

void DefaultSkyMaterial::end(QOpenGLFunctions_3_2_Core* gl)
{
    Material::end(gl);
}

DefaultSkyMaterialPtr DefaultSkyMaterial::create()
{
    return QSharedPointer<DefaultSkyMaterial>(new DefaultSkyMaterial());
}

}
