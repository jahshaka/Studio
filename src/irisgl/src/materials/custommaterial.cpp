#include "custommaterial.h"
#include "../core/irisutils.h"

namespace iris
{

CustomMaterial::CustomMaterial()
{
    createProgramFromShaderSource(":assets/shaders/custom.vert",
                                  ":assets/shaders/custom.frag");
    //program->bind();
    this->setRenderLayer((int)RenderLayer::Opaque);
}

void CustomMaterial::setTexture(Texture2DPtr tex)
{
    texture = tex;
    if(!!tex)
        this->addTexture("tex",tex);
    else
        this->removeTexture("tex");
}

Texture2DPtr CustomMaterial::getTexture()
{
    return texture;
}

void CustomMaterial::begin(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene)
{
    Material::begin(gl,scene);
    program->setUniformValue("color", QVector3D(120, 120, 15));
}

void CustomMaterial::end(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene)
{
    Material::end(gl,scene);
}

CustomMaterialPtr CustomMaterial::create()
{
    return CustomMaterialPtr(new CustomMaterial());
}

}
