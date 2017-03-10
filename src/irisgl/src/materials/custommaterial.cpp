#include "custommaterial.h"
#include "../graphics/texture2d.h"
#include "../core/irisutils.h"

namespace iris
{

CustomMaterial::CustomMaterial()
{
    createProgramFromShaderSource(":assets/shaders/custom.vert",
                                  ":assets/shaders/custom.frag");

//    QString x1 = IrisUtils::getAbsoluteAssetPath("app/content/skies/alternative/ame_desert/left.png");
//    QString x2 = IrisUtils::getAbsoluteAssetPath("app/content/skies/alternative/ame_desert/right.png");
//    QString y1 = IrisUtils::getAbsoluteAssetPath("app/content/skies/alternative/ame_desert/top.png");
//    QString y2 = IrisUtils::getAbsoluteAssetPath("app/content/skies/alternative/ame_desert/bottom.png");
//    QString z1 = IrisUtils::getAbsoluteAssetPath("app/content/skies/alternative/ame_desert/front.png");
//    QString z2 = IrisUtils::getAbsoluteAssetPath("app/content/skies/alternative/ame_desert/back.png");

//    auto skyTexture = Texture2D::createCubeMap(x1, x2, y1, y2, z1, z2);
//    setTexture(skyTexture);

    allocated = 0;

    sliderValues[0] = 0.5;
    sliderValues[1] = 0.5;

    this->setRenderLayer((int)RenderLayer::Opaque);
}

void CustomMaterial::setTexture(Texture2DPtr tex)
{
    texture = tex;
    if (!!tex)  this->addTexture("skybox", tex);
    else        this->removeTexture("skybox");
}

void CustomMaterial::initializeDefaultValues(const QJsonObject &jahShader)
{
    auto uniforms = jahShader["uniforms"].toObject();

    allocated = 0;
    for (auto childObj : uniforms) {
        if (childObj.toObject()["type"] == "slider") {
            uniformName[allocated] = childObj.toObject()["uniform"].toString();;
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
    for (int i = 0; i < 2; i++) {
        this->setUniformValue(uniformName[i], sliderValues[i]);
    }
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
