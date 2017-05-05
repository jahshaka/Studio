/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include "custommaterial.h"
#include "../graphics/texture2d.h"
#include "../core/irisutils.h"

namespace iris
{

void CustomMaterial::setTextureWithUniform(const QString &uniform, const QString &texturePath)
{
    auto texture = iris::Texture2D::load(texturePath);
    if (!!texture) {
        addTexture(uniform, texture);
    } else {
        removeTexture(uniform);
    }
}

void CustomMaterial::setValue(const QString &name, const QVariant &value)
{
    for (auto prop : this->properties) {
        if (prop->name == name) {

            if (prop->type == iris::PropertyType::Texture) {
                auto _prop = static_cast<TextureProperty*>(prop);
                _prop->toggle = !value.toString().isEmpty();
                prop->setValue(value.toString());
                setTextureWithUniform(prop->uniform, value.toString());
            }

            else {
                prop->setValue(value);
            }
        }
    }
}

void CustomMaterial::setUniformValues(Property *prop)
{
    if (prop->type == PropertyType::Bool) {
        program->setUniformValue(prop->uniform.toStdString().c_str(), prop->getValue().toBool());
    }

    if (prop->type == PropertyType::Float) {
        program->setUniformValue(prop->uniform.toStdString().c_str(), prop->getValue().toFloat());
    }

    // TODO, figure out a way for the default material to mix values... the ambient for one
    if (prop->type == PropertyType::Color) {
        program->setUniformValue(prop->uniform.toStdString().c_str(),
                                 QVector3D(prop->getValue().value<QColor>().redF(),
                                           prop->getValue().value<QColor>().greenF(),
                                           prop->getValue().value<QColor>().blueF()));
    }

    if (prop->type == iris::PropertyType::Texture) {
        auto tprop = static_cast<TextureProperty*>(prop);
        program->setUniformValue(tprop->toggleValue.toStdString().c_str(), tprop->toggle);
    }
}

QJsonObject CustomMaterial::loadShaderFromDisk(const QString &filePath)
{
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);
    auto data = file.readAll();
    file.close();
    return QJsonDocument::fromJson(data).object();
}

void CustomMaterial::begin(QOpenGLFunctions_3_2_Core *gl, ScenePtr scene)
{
    Material::begin(gl, scene);

    for (auto prop : this->properties) {
        setUniformValues(prop);
    }
}

void CustomMaterial::end(QOpenGLFunctions_3_2_Core *gl, ScenePtr scene)
{
    Material::end(gl, scene);
}

void CustomMaterial::generate(const QString &fileName)
{
    auto jahShader = loadShaderFromDisk(fileName);

    setName(jahShader["name"].toString());

    auto vertPath = jahShader["vertex_shader"].toString();
    auto fragPath = jahShader["fragment_shader"].toString();

    setBaseMaterialProperties(jahShader);

    if (!vertPath.startsWith(":")) vertPath = IrisUtils::getAbsoluteAssetPath(vertPath);
    if (!fragPath.startsWith(":")) fragPath = IrisUtils::getAbsoluteAssetPath(fragPath);

    createProgramFromShaderSource(vertPath, fragPath);

    auto widgetProps = jahShader["uniforms"].toArray();

    for (int i = 0; i < widgetProps.size(); i++) {
        auto prop = widgetProps[i].toObject();

        auto displayName    = prop["displayName"].toString();
        auto name           = prop["name"].toString();
        auto uniform        = prop["uniform"].toString();

        if (prop["type"] == "slider") {
            auto fltProp = new iris::FloatProperty;
            fltProp->id             = i;
            fltProp->displayName    = displayName;
            fltProp->name           = name;
            fltProp->minValue       = prop["start"].toDouble();
            fltProp->maxValue       = prop["end"].toDouble();
            fltProp->uniform        = uniform;
            fltProp->value          = prop["value"].toDouble();

            if (properties.size() < widgetProps.size()) {
                this->properties.append(fltProp);
            }
        }

        if (prop["type"] == "checkbox") {
            auto blProp = new iris::BoolProperty;
            blProp->id              = i;
            blProp->displayName     = displayName;
            blProp->name            = name;
            blProp->uniform         = uniform;
            blProp->value           = prop["value"].toBool();

            if (properties.size() < widgetProps.size()) {
                this->properties.append(blProp);
            }
        }

        if (prop["type"] == "texture") {
            auto texProp = new iris::TextureProperty;
            texProp->id             = i;
            texProp->displayName    = displayName;
            texProp->name           = name;
            texProp->uniform        = uniform;
            texProp->toggleValue    = prop["toggle"].toString();
            texProp->value          = prop["value"].toString();

            if (properties.size() < widgetProps.size()) {
                this->properties.append(texProp);
            }
        }

        if (prop["type"] == "color") {
            auto clrProp = new iris::ColorProperty;
            clrProp->id             = i;
            clrProp->displayName    = displayName;
            clrProp->name           = name;
            clrProp->uniform        = uniform;

            QColor col;
            col.setNamedColor(prop["value"].toString());
            clrProp->value          = col;

            if (properties.size() < widgetProps.size()) {
                this->properties.append(clrProp);
            }
        }
    }
}

void CustomMaterial::purge()
{
    this->properties.clear();
}

void CustomMaterial::setName(const QString &name)
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

    renderStates.zWrite             = zwrite;
    renderStates.depthTest          = depthTest;
    renderStates.fogEnabled         = fog;
    renderStates.castShadows        = castShadows;
    renderStates.receiveShadows     = receiveShadows;
    renderStates.receiveLighting    = lighting;

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

QString CustomMaterial::getName() const
{
    return materialName;
}

CustomMaterialPtr CustomMaterial::create()
{
    return CustomMaterialPtr(new CustomMaterial());
}

void CustomMaterial::setProperties(QList<Property*> props)
{
    this->properties = props;
}

}
