#include "custommaterial.h"
#include "../graphics/texture2d.h"
#include "../core/irisutils.h"

namespace iris
{

CustomMaterial::CustomMaterial()
{
    createProgramFromShaderSource(":assets/shaders/custom.vert",
                                  ":assets/shaders/custom.frag");

    QString x1 = IrisUtils::getAbsoluteAssetPath("app/content/textures/left.jpg");
    QString x2 = IrisUtils::getAbsoluteAssetPath("app/content/textures/right.jpg");
    QString y1 = IrisUtils::getAbsoluteAssetPath("app/content/textures/top.jpg");
    QString y2 = IrisUtils::getAbsoluteAssetPath("app/content/textures/bottom.jpg");
    QString z1 = IrisUtils::getAbsoluteAssetPath("app/content/textures/front.jpg");
    QString z2 = IrisUtils::getAbsoluteAssetPath("app/content/textures/back.jpg");

    auto skyTexture = Texture2D::createCubeMap(x1, x2, y1, y2, z1, z2);
    setTexture(skyTexture);

    this->setRenderLayer((int)RenderLayer::Opaque);
}

void CustomMaterial::setTexture(Texture2DPtr tex)
{
    texture = tex;
    if(!!tex)
        this->addTexture("skybox", tex);
    else
        this->removeTexture("skybox");
}

void CustomMaterial::initializeDefaultValues(const QJsonObject &jahShader)
{
    auto uniforms = jahShader["uniforms"].toObject();

    int allocated = 0;
    for (auto childObj : uniforms) {
        if (childObj.toObject()["type"] == "slider") {
            sliderValues[allocated] = (float) childObj.toObject()["value"].toDouble();
            allocated++;
        }
    }
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
