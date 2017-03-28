/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "custommaterial.h"
#include "../graphics/texture2d.h"
#include "../core/irisutils.h"

namespace iris
{

CustomMaterial::CustomMaterial()
{
    setRenderLayer((int) RenderLayer::Opaque);
}

void CustomMaterial::setTexture(Texture2DPtr tex)
{
//    texture = tex;
//    if (!!tex) {
//        addTexture("skybox", tex);
//    } else {
//        this->removeTexture("skybox");
//    }
}

// @TODO GET TEXTURE FROM MAP BY NAME...
Texture2DPtr CustomMaterial::getTexture()
{
//    return texture;
}

// @TODO clean up how this is set and used
void CustomMaterial::setTextureWithUniform(QString textureUniformName, QString texturePath)
{
    intermediateTexture = iris::Texture2D::load(texturePath);

    if (!!intermediateTexture) {
        addTexture(textureUniformName, intermediateTexture);
    } else {
        removeTexture(textureUniformName);
    }
}

void CustomMaterial::setTextureWithBool(QString uniform, bool value)
{
    textureToggleUniforms.push_back(
                iris::make_mat_struct(textureToggleUniforms.size(),
                                      uniform,
                                      value));

//    if (!textureValue.isEmpty()) {
//        setTextureWithUniform(uniform, textureValue);
//    }
}

void CustomMaterial::updateTextureAndToggleUniform(int index, QString textureUni)
{
    textureUniforms[index].value = textureUni;
    setTextureWithUniform(textureUniforms[index].uniform, textureUni);
}

void CustomMaterial::begin(QOpenGLFunctions_3_2_Core *gl, ScenePtr scene)
{
     Material::begin(gl, scene);

    // @todo, figure out a way for the default material to mix values... the ambient for one
    auto cit = colorUniforms.begin();
    while (cit != colorUniforms.end()) {
//        QColor color;
//        color.setNamedColor(cit->value);
//        qDebug() << cit->uniform << " " << cit->value.name();
        program->setUniformValue(cit->uniform.toStdString().c_str(),
                                 QVector3D(cit->value.redF(),
                                           cit->value.greenF(),
                                           cit->value.blueF()));
        cit++;
    }

    // set slider uniforms
    auto sit = sliderUniforms.begin();
    while (sit != sliderUniforms.end()) {
        program->setUniformValue(sit->uniform.toStdString().c_str(), sit->value);
        ++sit;
    }

    // set bool uniforms
//    std::map<QString, bool>::iterator bit = boolUniforms.begin();
//    while (bit != boolUniforms.end()) {
//        program->setUniformValue(bit->first.toStdString().c_str(), bit->second);
//        bit++;
//    }

//    program->setUniformValue("u_useDiffuseTex", true);

    // set texture toggle uniforms
    auto ttit = textureToggleUniforms.begin();
    while (ttit != textureToggleUniforms.end()) {
        setUniformValue(ttit->uniform.toStdString().c_str(), ttit->value);
        ++ttit;
    }

//    bindTextures(gl);

//    program->setUniformValue("u_material.diffuse",QVector3D(diffuseColor.redF(),diffuseColor.greenF(),diffuseColor.blueF()));
//    const QColor& sceneAmbient = scene->ambientColor;
//    auto finalAmbient = QVector3D(ambientColor.redF() + sceneAmbient.redF(),
//                                  ambientColor.greenF() + sceneAmbient.greenF(),
//                                  ambientColor.blueF() + sceneAmbient.blueF());
//    program->setUniformValue("u_material.ambient",finalAmbient);
//    program->setUniformValue("u_material.specular",QVector3D(specularColor.redF(),specularColor.greenF(),specularColor.blueF()));
//    program->setUniformValue("u_material.shininess",shininess);

//    program->setUniformValue("u_textureScale", this->textureScale);
//    program->setUniformValue("u_normalIntensity",normalIntensity);
//    program->setUniformValue("u_reflectionInfluence",reflectionInfluence);

//    program->setUniformValue("u_useDiffuseTex",useDiffuseTex);
//    program->setUniformValue("u_useNormalTex",useNormalTex);
//    program->setUniformValue("u_useSpecularTex",useSpecularTex);
//    program->setUniformValue("u_useReflectionTex",useReflectionTex);
}

void CustomMaterial::end(QOpenGLFunctions_3_2_Core *gl, ScenePtr scene)
{
    Material::end(gl, scene);
}

/*
 * some notes about this function, the material isn't destroyed until the object is
 * this function will get called everytime we cast a material when we select a node
 * using a map keeps our uniforms the same size so we don't add to it again
 *
 */
void CustomMaterial::generate(const QJsonObject &jahShader)
{
    auto useBuiltinShader = jahShader["builtin"].toBool();

    auto vertPath = jahShader["vertex_shader"].toString();
    auto fragPath = jahShader["fragment_shader"].toString();

    if (useBuiltinShader) {
        createProgramFromShaderSource(vertPath, fragPath);
    } else {
        createProgramFromShaderSource(IrisUtils::getAbsoluteAssetPath(vertPath),
                                      IrisUtils::getAbsoluteAssetPath(fragPath));
    }

    auto widgetProps = jahShader["uniforms"].toArray();

    /// TODO remove this when the default material is deleted.
    int sliderMax = 0, textureMax = 0, colorMax = 0;

    for (int i = 0; i < widgetProps.size(); i++) {
        auto prop = widgetProps[i].toObject();
        if (prop["type"] == "slider") {
            sliderMax++;
        }

        if (prop["type"] == "color") {
            colorMax++;
        }

        if (prop["type"] == "texture") {
            textureMax++;
        }
    }

    for (int propIndex = 0; propIndex < widgetProps.size(); propIndex++) {

        auto prop = widgetProps[propIndex].toObject();
        auto uniform    = prop["uniform"].toString();

        if (sliderUniforms.size() < sliderMax) {
            if (prop["type"] == "slider") {
                auto value = (float) prop["value"].toDouble();
                sliderUniforms.push_back(make_mat_struct(textureUniforms.size(), uniform, value));
            }
        }

        if (colorUniforms.size() < colorMax) {
            if (prop["type"] == "color") {
                QColor col;
                col.setNamedColor(prop["value"].toString());
                colorUniforms.push_back(iris::make_mat_struct(textureUniforms.size(), uniform, col));
            }
        }

        /// TODO - find a way to set default textures? do we even want to do this?
        if (textureUniforms.size() < textureMax) {
//            qDebug() << textureUniforms.size();
            if (prop["type"] == "texture") {
                auto textureValue = prop["value"].toString();

                textureUniforms.push_back(iris::make_mat_struct(textureUniforms.size(), uniform, textureValue));
                textureToggleUniforms.push_back(
                            iris::make_mat_struct(textureUniforms.size(),
                                                  prop["toggle"].toString(),
                                                  !textureValue.isEmpty()));

                if (!textureValue.isEmpty()) {
                    setTextureWithUniform(uniform, textureValue);
                }
            }
        }
    }

    this->setRenderLayer((int) RenderLayer::Opaque);
}

CustomMaterialPtr CustomMaterial::create()
{
    return CustomMaterialPtr(new CustomMaterial());
}

}
