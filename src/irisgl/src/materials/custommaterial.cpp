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
    for (auto prop : this->properties) {
        if (prop->uniform == "u_diffuseTexture") {
            prop->setValue(uniform);
        }
    }

//    textureToggleUniforms[index].value = !uniform.isEmpty();
//    textureUniforms[index].value = uniform;
    setTextureWithUniform("u_diffuseTexture", uniform);
//    setTextureWithUniform(textureUniforms[index].uniform, uniform);

//    for (auto prop :this->properties) {
//        if (prop->uniform == "u_diffuseTexture") {
//            qDebug() << prop->getValue();
//        }
//    }
}

void CustomMaterial::updateTextureAndToggleUniform(QString uni, QString uniform)
{
    for (auto prop : this->properties) {
        if (prop->uniform == uni) {
            prop->setValue(uniform);
            if (prop->type == iris::PropertyType::Texture) {
                auto _prop = static_cast<TextureProperty*>(prop);
                _prop->toggle = !uniform.isEmpty();
            }
        }
    }

    setTextureWithUniform(uni, uniform);
}

void CustomMaterial::updateShaderUniform(QString uniform, QVariant value)
{
//    for (auto prop : this->properties) {
//        if (prop->uniform == uniform) {
//            if (value.type() == QVariant::Bool) {
//                prop->setValue(value.toBool());
//            } else if (value.type() == QVariant::Double) {
//                prop->setValue(value.toFloat());
//            } else if (value.type() == QVariant::Color) {
//                prop->setValue(value.value<QColor>());
//            } else if (value.type() == QVariant::String) {
//                prop->setValue(value.toString());
//                setTextureWithUniform(uniform, value.toString());
//            }
//        }
//    }
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

//void setUniValue(const QString &str, QVariant var) {
//    decltype val(var);
//    program->setValue(str.toStdString().c_str(), val);
//}

void CustomMaterial::begin(QOpenGLFunctions_3_2_Core *gl, ScenePtr scene)
{
    Material::begin(gl, scene);

    for (auto prop : this->properties) {
        if (prop->type == iris::PropertyType::Texture) {
            auto _prop = static_cast<TextureProperty*>(prop);
            program->setUniformValue(_prop->toggleValue.toStdString().c_str(), _prop->toggle);
        }

        if (prop->type == PropertyType::Float) {
            program->setUniformValue(prop->uniform.toStdString().c_str(), prop->getValue().toFloat());
        }

        if (prop->type == PropertyType::Bool) {
            program->setUniformValue(prop->uniform.toStdString().c_str(), prop->getValue().toBool());
        }

        // TODO, figure out a way for the default material to mix values... the ambient for one
        if (prop->type == PropertyType::Color) {
            program->setUniformValue(prop->uniform.toStdString().c_str(),
                                     QVector3D(prop->getValue().value<QColor>().redF(),
                                               prop->getValue().value<QColor>().greenF(),
                                               prop->getValue().value<QColor>().blueF()));
        }
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

    // DO THE MAGIC HERE
    for (int i = 0; i < widgetProps.size(); i++) {
        auto prop = widgetProps[i].toObject();

        if (prop["type"] == "slider") {
            auto fltProp = new iris::FloatProperty;
            fltProp->id             = i;
            fltProp->displayName    = prop["displayName"].toString();
            fltProp->name           = prop["name"].toString();
            fltProp->minValue       = prop["start"].toDouble();
            fltProp->maxValue       = prop["end"].toDouble();
            fltProp->uniform        = prop["uniform"].toString();
            fltProp->value          = prop["value"].toDouble();

            if (properties.size() < widgetProps.size()) {
                this->properties.append(fltProp);
            }
        }

        if (prop["type"] == "checkbox") {
            auto blProp = new iris::BoolProperty;
            blProp->id              = i;
            blProp->displayName     = prop["displayName"].toString();
            blProp->name            = prop["name"].toString();
            blProp->uniform         = prop["uniform"].toString();
            blProp->value           = prop["value"].toBool();

            if (properties.size() < widgetProps.size()) {
                this->properties.append(blProp);
            }
        }

        if (prop["type"] == "texture") {
            auto texProp = new iris::TextureProperty;
            texProp->id             = i;
            texProp->displayName    = prop["displayName"].toString();
            texProp->name           = prop["name"].toString();
            texProp->uniform        = prop["uniform"].toString();
            texProp->toggleValue    = prop["toggle"].toString();
            texProp->value          = prop["value"].toString();

            if (properties.size() < widgetProps.size()) {
                this->properties.append(texProp);
            }
        }

        if (prop["type"] == "color") {
            auto clrProp = new iris::ColorProperty;
            clrProp->id             = i;
            clrProp->displayName    = prop["displayName"].toString();
            clrProp->name           = prop["name"].toString();
            clrProp->uniform        = prop["uniform"].toString();

            QColor col;
            col.setNamedColor(prop["value"].toString());
            clrProp->value          = col;

            if (properties.size() < widgetProps.size()) {
                this->properties.append(clrProp);
            }
        }
    }
    // END MAGIC

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

    jahShaderMaster = jahShader;
    // not magic, the widget heights won't change for a while so lazy ok for now or fetch them...
    // the 30 is for the blade padding, the one is for the mandatory slider, 6 is spacing, 9 padd
    finalSize = 30 + (sliderSize + boolSize + colorSize + 1) * 28
                   + (textureSize * 108) + ((widgetProps.size() + 1) * 6) + 9 + 9;


    // can we simply get the uniforms from the file... toggle as well
    // we don't need to read the value in the material property widget...
    // we just need the uniforms to match the objects being returned
}

void CustomMaterial::purge()
{
    this->properties.clear();
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
