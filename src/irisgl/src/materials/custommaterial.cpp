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

void CustomMaterial::updateTextureAndToggleUniform(int index, QString uniform)
{
    textureToggleUniforms[index].value = !uniform.isEmpty() || !uniform.isNull();
    textureUniforms[index].value = uniform;
    setTextureWithUniform(textureUniforms[index].uniform, uniform);
}

void CustomMaterial::updateFloatAndUniform(int index, float value)
{
    sliderUniforms[index].value = value;
}

void CustomMaterial::updateColorAndUniform(int index, QColor color)
{
    colorUniforms[index].value = color;
}

void CustomMaterial::begin(QOpenGLFunctions_3_2_Core *gl, ScenePtr scene)
{
     Material::begin(gl, scene);

    // @todo, figure out a way for the default material to mix values... the ambient for one
    auto colorIterator = colorUniforms.begin();
    while (colorIterator != colorUniforms.end()) {
        program->setUniformValue(colorIterator->uniform.toStdString().c_str(),
                                 QVector3D(colorIterator->value.redF(),
                                           colorIterator->value.greenF(),
                                           colorIterator->value.blueF()));
        colorIterator++;
    }

    // set slider uniforms
    auto sliderIterator = sliderUniforms.begin();
    while (sliderIterator != sliderUniforms.end()) {
        program->setUniformValue(sliderIterator->uniform.toStdString().c_str(),
                                 sliderIterator->value);
        ++sliderIterator;
    }

    // set texture toggle uniforms
    auto textureToggleIterator = textureToggleUniforms.begin();
    while (textureToggleIterator != textureToggleUniforms.end()) {
        setUniformValue(textureToggleIterator->uniform.toStdString().c_str(),
                        textureToggleIterator->value);
        ++textureToggleIterator;
    }
}

void CustomMaterial::end(QOpenGLFunctions_3_2_Core *gl, ScenePtr scene)
{
    Material::end(gl, scene);
}

/*
 * some notes about this function, the material isn't destroyed until the object is.
 * this function will get called everytime we cast a material when we select a node
 */
void CustomMaterial::generate(const QJsonObject &jahShader)
{
    name = jahShader["name"].toString();

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

    /// TODO see if this can be removed this when the default material is deleted.
    unsigned sliderSize, textureSize, colorSize;
    sliderSize = textureSize = colorSize = 0;

    for (int i = 0; i < widgetProps.size(); i++) {
        auto prop = widgetProps[i].toObject();
        if (prop["type"] == "slider")   sliderSize++;
        if (prop["type"] == "color")    colorSize++;
        if (prop["type"] == "texture")  textureSize++;
    }

    for (int propIndex = 0; propIndex < widgetProps.size(); propIndex++) {

        auto prop = widgetProps[propIndex].toObject();
        auto uniform = prop["uniform"].toString();

        if (sliderUniforms.size() < sliderSize) {
            if (prop["type"] == "slider") {
                auto value = (float) prop["value"].toDouble();
                sliderUniforms.push_back(make_mat_struct(textureUniforms.size(), uniform, value));
            }
        }

        if (colorUniforms.size() < colorSize) {
            if (prop["type"] == "color") {
                QColor col;
                col.setNamedColor(prop["value"].toString());
                colorUniforms.push_back(iris::make_mat_struct(textureUniforms.size(), uniform, col));
            }
        }

        // TODO - find an efficient way to set default textures. Do we even want to do this?
        if (textureUniforms.size() < textureSize) {
            if (prop["type"] == "texture") {
                auto textureValue = prop["value"].toString();

                textureUniforms.push_back(iris::make_mat_struct(textureUniforms.size(),
                                                                uniform,
                                                                textureValue));

                textureToggleUniforms.push_back(
                            iris::make_mat_struct(textureUniforms.size(),
                                                  prop["toggle"].toString(),
                                                  !textureValue.isEmpty()));

                // this will be set to false most if not all the time, see TODO above
                if (!textureValue.isEmpty()) {
                    setTextureWithUniform(uniform, textureValue);
                }
            }
        }
    }

    jahShaderMaster = jahShader;

    this->setRenderLayer((int) RenderLayer::Opaque);
}

void CustomMaterial::purge()
{
    sliderUniforms.clear();
    colorUniforms.clear();
    textureUniforms.clear();
    textureToggleUniforms.clear();
}

CustomMaterialPtr CustomMaterial::create()
{
    return CustomMaterialPtr(new CustomMaterial());
}

}
