#include "defaultskymaterial.h"


namespace iris
{

DefaultSkyMaterial::DefaultSkyMaterial()
{
    createProgramFromShaderSource("app/shaders/defaultsky.vert","app/shaders/defaultsky.frag");
    setTextureCount(1);
}

void DefaultSkyMaterial::setSkyTexture(Texture2DPtr tex)
{
    texture = tex;
    if(!!tex)
        this->addTexture("texture",tex);
    else
        this->removeTexture("texture");
}

void DefaultSkyMaterial::clearSkyTexture()
{
    texture.clear();
    removeTexture("texture");
}

Texture2DPtr DefaultSkyMaterial::getSkyTexture()
{
    return texture;
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
