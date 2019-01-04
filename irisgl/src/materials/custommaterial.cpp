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

#include <QOpenGLShaderProgram>

#include "custommaterial.h"
#include "../graphics/texture2d.h"
#include "../graphics/shader.h"
#include "../graphics/graphicsdevice.h"
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
	if (onValueChange(name, value)) {
		for (auto prop : this->properties) {
			if (prop->name == name) {

				if (prop->type == iris::PropertyType::Texture) {
					auto _prop = static_cast<TextureProperty*>(prop);
					_prop->toggle = !value.toString().isEmpty();
					if (!value.toString().isEmpty()) {
						prop->setValue(value.toString());
						setTextureWithUniform(prop->uniform, value.toString());
					}
				}

				else {
					prop->setValue(value);
				}
			}
		}
	}
}

void CustomMaterial::setUniformValues(GraphicsDevicePtr device, Property *prop)
{
	auto program = getProgram();
    if (prop->type == PropertyType::Bool) {
		device->setShaderUniform(prop->uniform.toStdString().c_str(), prop->getValue().toBool());
    }

    if (prop->type == PropertyType::Float) {
		device->setShaderUniform(prop->uniform.toStdString().c_str(), prop->getValue().toFloat());
    }

    // TODO, figure out a way for the default material to mix values... the ambient for one
    if (prop->type == PropertyType::Color) {
		device->setShaderUniform(prop->uniform.toStdString().c_str(),
                                 QVector3D(prop->getValue().value<QColor>().redF(),
                                           prop->getValue().value<QColor>().greenF(),
                                           prop->getValue().value<QColor>().blueF()));
    }

    if (prop->type == iris::PropertyType::Texture) {
        auto tprop = static_cast<TextureProperty*>(prop);
		device->setShaderUniform(tprop->toggleValue.toStdString().c_str(), tprop->toggle);
		//device->setShaderUniform(tprop->toggleValue.toStdString().c_str(), true);
    }
}

QString CustomMaterial::firstTextureSlot() const
{
    for (auto prop : properties) {
        if (prop->type == PropertyType::Texture) {
            return prop->name;
        }
    }

    return QString();
}

QJsonObject CustomMaterial::loadShaderFromDisk(const QString &filePath)
{
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);
    auto data = file.readAll();
    file.close();
    return QJsonDocument::fromJson(data).object();
}

void CustomMaterial::begin(GraphicsDevicePtr device, ScenePtr scene)
{
    Material::begin(device, scene);

    for (auto prop : this->properties) {
        setUniformValues(device, prop);
    }
}

void CustomMaterial::end(GraphicsDevicePtr device, ScenePtr scene)
{
    Material::end(device, scene);
}

void CustomMaterial::generate(const QString &fileName, bool project)
{
	materialPath = fileName;
    auto fileInfo = QFileInfo(fileName);
    //auto shaderName = fileName;
    //if (fileInfo.suffix().isEmpty()) shaderName += ".shader";
    auto jahShader = loadShaderFromDisk(fileInfo.absoluteFilePath());

    setName(jahShader["name"].toString());
    setGuid(jahShader["guid"].toString());

    auto vertPath = jahShader["vertex_shader"].toString();
    auto fragPath = jahShader["fragment_shader"].toString();

    setBaseMaterialProperties(jahShader);

    if (!vertPath.startsWith(":")) vertPath = IrisUtils::getAbsoluteAssetPath(vertPath);
    if (!fragPath.startsWith(":")) fragPath = IrisUtils::getAbsoluteAssetPath(fragPath);

    createProgramFromShaderSource(vertPath, fragPath);
	parseProperties(jahShader["uniforms"].toArray());
}

void CustomMaterial::generate(const QJsonObject &object)
{
    setName(object["name"].toString());
    setGuid(object["guid"].toString());

    auto vertPath = object["vertex_shader"].toString();
    auto fragPath = object["fragment_shader"].toString();

	if (vertPath == "") {
		vertPath = object["vertexShaderSource"].toString();
		fragPath = object["fragmentShaderSource"].toString();
	}

    setBaseMaterialProperties(object);

    createProgramFromShaderSource(vertPath, fragPath);
	parseProperties(object["uniforms"].toArray());
}

void CustomMaterial::parseProperties(const QJsonArray &widgetProps)
{
    for (int i = 0; i < widgetProps.size(); i++) {
        auto prop           = widgetProps[i].toObject();
        auto displayName    = prop["displayName"].toString();
        auto name           = prop["name"].toString();
        auto uniform        = prop["uniform"].toString();

        if (prop["type"] == "float") {
            auto fltProp = new iris::FloatProperty;
            fltProp->id             = i;
            fltProp->displayName    = displayName;
            fltProp->name           = name;
            fltProp->minValue       = prop["min"].toDouble();
            fltProp->maxValue       = prop["max"].toDouble();
            fltProp->uniform        = uniform;
            fltProp->value          = prop["value"].toDouble();

            if (properties.size() < widgetProps.size()) this->properties.append(fltProp);
        }

        if (prop["type"] == "bool") {
            auto blProp             = new iris::BoolProperty;
            blProp->id          = i;
            blProp->displayName = displayName;
            blProp->name        = name;
            blProp->uniform     = uniform;
            blProp->value       = prop["value"].toBool();

            if (properties.size() < widgetProps.size()) this->properties.append(blProp);
        }

        if (prop["type"] == "texture") {
            auto texProp = new iris::TextureProperty;
            texProp->id             = i;
            texProp->displayName    = displayName;
            texProp->name           = name;
            texProp->uniform        = uniform;
            texProp->toggleValue    = prop["toggle"].toString();
            texProp->value          = prop["value"].toString();

            if (properties.size() < widgetProps.size()) this->properties.append(texProp);
        }

        if (prop["type"] == "color") {
            auto clrProp = new iris::ColorProperty;
            clrProp->id             = i;
            clrProp->displayName    = displayName;
            clrProp->name           = name;
            clrProp->uniform        = uniform;

            QColor col;
            col.setNamedColor(prop["value"].toString());
            clrProp->value = col;

            if (properties.size() < widgetProps.size()) this->properties.append(clrProp);
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

void CustomMaterial::setGuid(const QString &guid)
{
    materialGuid = guid;
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

    renderStates.fogEnabled         = fog;
    renderStates.castShadows        = castShadows;
    renderStates.receiveShadows     = receiveShadows;
    renderStates.receiveLighting    = lighting;

    if (renderLayer == "alphaTested") {
        setRenderLayer(RenderLayer::AlphaTested);
    } else if (renderLayer == "opaque") {
        setRenderLayer(RenderLayer::Background);
    } else if (renderLayer == "overlay") {
        setRenderLayer(RenderLayer::Overlay);
    } else if (renderLayer == "transparent") {
        setRenderLayer(RenderLayer::Transparent);
    } else {
        setRenderLayer(RenderLayer::Opaque);
    }

    if (cullMode == "front") {
        renderStates.rasterState = RasterizerState::CullClockwise;
    } else if (cullMode == "back") {
        renderStates.rasterState = RasterizerState::CullCounterClockwise;
    } else if (cullMode == "none") {
        renderStates.rasterState = RasterizerState::CullNone;
    } else {
        renderStates.rasterState = RasterizerState::CullCounterClockwise;
    }

    if (blendType == "normal" || blendType == "opaque") {
        renderStates.blendState = BlendState::Opaque;
    } else if (blendType == "add" || blendType == "additive") {
        renderStates.blendState = BlendState::Additive;
    } else if (blendType == "blend") {
        renderStates.blendState = BlendState::AlphaBlend;
    } else {
        renderStates.blendState = BlendState::Opaque;
    }

    renderStates.depthState.depthWriteEnabled = zwrite;
    renderStates.depthState.depthBufferEnabled = depthTest;
}

MaterialPtr CustomMaterial::duplicate()
{
	//qDebug() << "duplicating";
	//qDebug() << materialDefinitions;

	//auto mat = CustomMaterial::create();
	iris::CustomMaterialPtr mat;
	if (materialDefinitions.isEmpty()) {
		//mat->generate(materialPath, true);
		// old style
		mat = iris::CustomMaterial::createFromShaderPath(materialPath);
	}
	else {
		if (version == 1) {
			// v1 material spec
			//mat->generate(materialDefinitions);
			mat = iris::CustomMaterial::createFromShaderJson(materialDefinitions);
		}
		else {
			// v2 material spec
			// todo: user materialhelper to generate this instead
			auto vertSource = materialDefinitions["vertexShaderSource"].toString();
			auto fragSource = materialDefinitions["fragmentShaderSource"].toString();
			auto shader = iris::Shader::create(vertSource, fragSource);

			mat = CustomMaterial::create();
			mat->setShader(shader);
			mat->renderStates = this->renderStates;
			mat->renderLayer = this->renderLayer;
			mat->setName(this->getName());
			mat->setGuid(this->getGuid());
			mat->setVersion(this->getVersion());

			// should theoretically still work
			mat->parseProperties(materialDefinitions["properties"].toArray());

		}
		mat->setMaterialDefinition(materialDefinitions);
	}

	// pass values to newly created shader
	for (auto prop : this->properties) {
		mat->setValue(prop->name, prop->getValue());
	}

	return mat;
}

CustomMaterialPtr CustomMaterial::createFromShader(iris::ShaderPtr shader)
{
	auto mat = CustomMaterial::create();
	mat->setShader(shader);
	return mat;
}

CustomMaterialPtr CustomMaterial::createFromShaderPath(const QString& shaderPath)
{
	auto data = QFile(shaderPath).readAll();
	auto shaderObject = QJsonDocument::fromJson(data).object();
	return createFromShaderJson(shaderObject);
}

CustomMaterialPtr CustomMaterial::createFromShaderJson(const QJsonObject &object)
{
	auto mat = CustomMaterial::create();
	mat->setName(object["name"].toString());
	mat->setGuid(object["guid"].toString());

	auto vertPath = object["vertex_shader"].toString();
	auto fragPath = object["fragment_shader"].toString();

	if (vertPath == "") {
		vertPath = object["vertexShaderSource"].toString();
		fragPath = object["fragmentShaderSource"].toString();
	}

	auto shader = iris::Shader::load(vertPath, fragPath);
	mat->setShader(shader);

	mat->setBaseMaterialProperties(object);
	mat->parseProperties(object["uniforms"].toArray());

	return mat;
}

QString CustomMaterial::getName()
{
    return materialName;
}

QString CustomMaterial::getGuid()
{
    return materialGuid;
}

CustomMaterialPtr CustomMaterial::create()
{
    return CustomMaterialPtr(new CustomMaterial());
}

void CustomMaterial::setProperties(QList<Property*> props)
{
    this->properties = props;
}

QList<Property *> CustomMaterial::getProperties()
{
    return this->properties;
}

}
