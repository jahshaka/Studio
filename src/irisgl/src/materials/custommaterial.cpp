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

// TODO clean up how this is set and used
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
    textureToggleUniforms[index].value = !uniform.isEmpty();
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

QJsonObject CustomMaterial::getShaderFile() const
{
    return jahShaderMaster;
}

void CustomMaterial::begin(QOpenGLFunctions_3_2_Core *gl, ScenePtr scene)
{
     Material::begin(gl, scene);

    // TODO, figure out a way for the default material to mix values... the ambient for one
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

    // set bool uniforms
    auto boolIterator = boolUniforms.begin();
    while (boolIterator != boolUniforms.end()) {
        program->setUniformValue(boolIterator->uniform.toStdString().c_str(),
                                 boolIterator->value);
        ++boolIterator;
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
    setMaterialName(jahShader["name"].toString());

    auto useBuiltinShader = jahShader["builtin"].toBool();

    auto vertPath = jahShader["vertex_shader"].toString();
    auto fragPath = jahShader["fragment_shader"].toString();

    setBaseMaterialProperties(jahShader);

    if(!vertPath.startsWith(":"))
        vertPath = IrisUtils::getAbsoluteAssetPath(vertPath);

    if(!fragPath.startsWith(":"))
        fragPath = IrisUtils::getAbsoluteAssetPath(fragPath);

    createProgramFromShaderSource(vertPath, fragPath);

    auto widgetProps = jahShader["uniforms"].toArray();

    /// TODO see if this can be removed this when the default material is deleted.
    unsigned sliderSize, textureSize, colorSize, boolSize;
    sliderSize = textureSize = colorSize = boolSize = 0;

    for (int i = 0; i < widgetProps.size(); i++) {
        auto prop = widgetProps[i].toObject();
        if (prop["type"] == "slider")   sliderSize++;
        if (prop["type"] == "color")    colorSize++;
        if (prop["type"] == "checkbox") boolSize++;
        if (prop["type"] == "texture")  textureSize++;
    }

    for (int propIndex = 0; propIndex < widgetProps.size(); propIndex++) {

        auto prop = widgetProps[propIndex].toObject();
        auto uniform = prop["uniform"].toString();
        auto name = prop["name"].toString();

        if (sliderUniforms.size() < sliderSize) {
            if (prop["type"] == "slider") {
                auto value = (float) prop["value"].toDouble();
                sliderUniforms.push_back(make_mat_struct(textureUniforms.size(),
                                                         name, uniform, value));
            }
        }

        if (colorUniforms.size() < colorSize) {
            if (prop["type"] == "color") {
                QColor col;
                col.setNamedColor(prop["value"].toString());
                colorUniforms.push_back(iris::make_mat_struct(textureUniforms.size(),
                                                              name, uniform, col));
            }
        }

        if (boolUniforms.size() < boolSize) {
            if (prop["type"] == "checkbox") {
                auto state = prop["value"].toBool();
                boolUniforms.push_back(iris::make_mat_struct(boolUniforms.size(),
                                                             name, uniform, state));
            }
        }

        // TODO - find an efficient way to set default textures. Do we even want to do this?
        if (textureUniforms.size() < textureSize) {
            if (prop["type"] == "texture") {
                auto textureValue = prop["value"].toString();

                textureUniforms.push_back(iris::make_mat_struct(textureUniforms.size(),
                                                                name, uniform, textureValue));

                textureToggleUniforms.push_back(
                            iris::make_mat_struct(textureUniforms.size(),
                                                  name,
                                                  prop["toggle"].toString(),
                                                  !textureValue.isEmpty()));

                // this will be set to false most if not all the time, see TODO above
                //  if (!textureValue.isEmpty()) {
                //      setTextureWithUniform(uniform, textureValue);
                //  }
            }
        }
    }

    jahShaderMaster = jahShader;
    // not magic, the widget heights won't change for a while so lazy ok for now or fetch them...
    // the 30 is for the blade padding, the one is for the mandatory slider, 6 is spacing, 9 padd
    finalSize = 30 + (sliderSize + boolSize + colorSize + 1) * 28
                   + (textureSize * 108) + ((widgetProps.size() + 1) * 6) + 9 + 9;
}

void CustomMaterial::purge()
{
    sliderUniforms.clear();
    colorUniforms.clear();
    boolUniforms.clear();
    textureUniforms.clear();
    textureToggleUniforms.clear();
}

void CustomMaterial::setMaterialName(const QString &name)
{
    materialName = name;
}

void CustomMaterial::setBaseMaterialProperties(const QJsonObject &jahShader)
{
    auto renderLayer    = jahShader["renderLayer"].toString("opaque");
    auto blendType      = jahShader["blendMode"].toString();
    auto zwrite         = jahShader["zWrite"].toBool(true);
    auto depthTest      = jahShader["depthTest"].toBool(true);
    auto cullMode       = jahShader["cullMode"].toString("back");
    auto fog            = jahShader["fog"].toBool(true);
    auto castShadows    = jahShader["castShadows"].toBool(true);
    auto receiveShadows = jahShader["receiveShadows"].toBool(true);
    auto lighting       = jahShader["lighting"].toBool(true);

    //renderStates.blendType = !blendType.isEmpty() ? blendType : "alphablend";
    renderStates.zWrite = zwrite;
    renderStates.depthTest = depthTest;
    renderStates.fogEnabled = fog;
    renderStates.castShadows = castShadows;
    renderStates.receiveShadows = receiveShadows;
    renderStates.receiveLighting = lighting;

    if (renderLayer == "alphaTested") {
        setRenderLayer((int) RenderLayer::AlphaTested);
    } else if (renderLayer == "opaque") {
        setRenderLayer((int) RenderLayer::Background);
    } else if (renderLayer == "overlay") {
        setRenderLayer((int) RenderLayer::Overlay);
    } else if (renderLayer == "transparent") {
        setRenderLayer((int) RenderLayer::Transparent);
    } else {
        setRenderLayer((int) RenderLayer::Opaque);
    }

    if (cullMode == "front") {
        renderStates.cullMode = FaceCullingMode::Front;
    } else if (cullMode == "back") {
        renderStates.cullMode = FaceCullingMode::Back;
    } else if (cullMode == "frontAndBack") {
        renderStates.cullMode = FaceCullingMode::FrontAndBack;
    } else {
        renderStates.cullMode = FaceCullingMode::None;
    }

    if (blendType == "normal") {
        renderStates.blendType = BlendType::Normal;
    } else if (blendType == "add") {
        renderStates.blendType = BlendType::Add;
    } else {
        renderStates.blendType = BlendType::None;
    }
}

QString CustomMaterial::getMaterialName() const
{
    return materialName;
}

int CustomMaterial::getCalculatedPropHeight() const
{
    return finalSize;
}

CustomMaterialPtr CustomMaterial::create()
{
    return CustomMaterialPtr(new CustomMaterial());
}

}
