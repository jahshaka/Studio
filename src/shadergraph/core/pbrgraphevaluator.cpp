/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "pbrgraphevaluator.h"

#include <QColor>
#include <QDebug>
#include <QFileInfo>
#include <QJsonObject>

#include "../graph/nodegraph.h"
#include "../models/connectionmodel.h"
#include "../models/nodemodel.h"
#include "../models/properties.h"
#include "../models/socketmodel.h"
#include "../nodes/test.h" // PropertyNode, TextureNode

#include "irisgl/src/materials/pbrmaterial.h"

namespace
{

// What one evaluated connection folded down to.
struct InputValue
{
	enum Kind { None, Scalar, Color, TexturePath, Unsupported };
	Kind kind = None;
	float scalar = 0.0f;
	QColor color;
	QString path;
	QString describe; // node typeName for unsupported reporting
};

QJsonObject colorToJson(const QColor& c)
{
	QJsonObject obj;
	obj["r"] = c.redF();
	obj["g"] = c.greenF();
	obj["b"] = c.blueF();
	obj["a"] = c.alphaF();
	return obj;
}

QColor colorFromJson(const QJsonObject& obj)
{
	return QColor::fromRgbF(obj["r"].toDouble(), obj["g"].toDouble(),
	                        obj["b"].toDouble(), obj["a"].toDouble(1.0));
}

// Folds the node feeding `socket` to a constant, a color or a texture path.
// This is the phase-2 seam: a chain this cannot fold is where the per-texel
// baker plugs in. TODO(bake).
InputValue evaluateInput(SocketModel* socket, const PbrGraphEvaluator::TextureResolver& resolve)
{
	InputValue result;
	if (!socket || !socket->hasConnection()) return result;

	auto con = socket->getConnection();
	auto leftSock = con->leftSocket;
	auto node = leftSock->getNode();
	if (!node) return result;
	int outIndex = node->outSockets.indexOf(leftSock);

	const auto& type = node->typeName;

	if (type == "float") {
		result.kind = InputValue::Scalar;
		result.scalar = (float)node->serializeWidgetValue().toDouble();
		return result;
	}

	if (type == "color") {
		auto obj = node->serializeWidgetValue().toObject();
		QColor c = QColor::fromRgbF(obj["r"].toDouble(), obj["g"].toDouble(),
		                            obj["b"].toDouble(), obj["a"].toDouble(1.0));
		// out socket 0 is RGBA; 1-4 are the R,G,B,A channels as floats
		if (outIndex <= 0) {
			result.kind = InputValue::Color;
			result.color = c;
		}
		else {
			result.kind = InputValue::Scalar;
			switch (outIndex) {
			case 1: result.scalar = (float)c.redF(); break;
			case 2: result.scalar = (float)c.greenF(); break;
			case 3: result.scalar = (float)c.blueF(); break;
			default: result.scalar = (float)c.alphaF(); break;
			}
		}
		return result;
	}

	if (type == "vector3" || type == "vector4") {
		auto obj = node->serializeWidgetValue().toObject();
		result.kind = InputValue::Color;
		result.color = QColor::fromRgbF(qBound(0.0, obj["x"].toDouble(), 1.0),
		                                qBound(0.0, obj["y"].toDouble(), 1.0),
		                                qBound(0.0, obj["z"].toDouble(), 1.0),
		                                type == "vector4" ? qBound(0.0, obj["w"].toDouble(), 1.0) : 1.0);
		return result;
	}

	if (type == "property") {
		auto prop = ((PropertyNode*)node)->getProperty();
		if (!prop) return result;
		switch (prop->type) {
		case PropertyType::Float:
		case PropertyType::Int:
			result.kind = InputValue::Scalar;
			result.scalar = prop->getValue().toFloat();
			return result;
		case PropertyType::Color:
			result.kind = InputValue::Color;
			result.color = prop->getValue().value<QColor>();
			return result;
		case PropertyType::Texture: {
			auto stored = prop->getValue().toString();
			auto path = resolve ? resolve(stored) : stored;
			if (path.isEmpty()) return result; // empty slot, not an error
			result.kind = InputValue::TexturePath;
			result.path = path;
			return result;
		}
		default:
			break;
		}
		result.kind = InputValue::Unsupported;
		result.describe = QString("property(%1)").arg(prop->displayName);
		return result;
	}

	if (type == "texture") {
		auto path = ((TextureNode*)node)->getTexturePath();
		if (path.isEmpty()) return result;
		result.kind = InputValue::TexturePath;
		result.path = path;
		return result;
	}

	// TODO(bake): phase 2 - evaluate PURE/UV chains per texel into a baked
	// texture and return it as a TexturePath. Until then: unsupported.
	result.kind = InputValue::Unsupported;
	result.describe = type;
	return result;
}

// How one master socket lands on the PbrMaterial.
struct SlotSpec
{
	enum Target { ColorSlot, FloatSlot, NormalSlot, NoTarget };
	QString socketName;
	Target target;
	QString valueKey; // constant lands here ("" = constants unsupported)
	QString mapKey;   // texture lands here ("" = textures unsupported)
	bool invertToRoughness = false; // legacy Shininess -> roughness
};

QVector<SlotSpec> specsForMaster(const QString& masterType)
{
	if (masterType == "PbrMaterial") {
		return {
			{ "Base Color", SlotSpec::ColorSlot, "baseColor", "baseColorMap" },
			{ "Metallic", SlotSpec::FloatSlot, "metallic", "metallicMap" },
			{ "Roughness", SlotSpec::FloatSlot, "roughness", "roughnessMap" },
			{ "Normal", SlotSpec::NormalSlot, "", "normalMap" },
			{ "Occlusion", SlotSpec::FloatSlot, "occlusionFactor", "occlusionMap" },
			{ "Emissive", SlotSpec::ColorSlot, "emissiveColor", "emissiveMap" },
			{ "Alpha", SlotSpec::FloatSlot, "alpha", "" },
			{ "Alpha Cutoff", SlotSpec::FloatSlot, "alphaCutoff", "" },
			{ "Vertex Offset", SlotSpec::NoTarget, "", "" },
			{ "Vertex Extrusion", SlotSpec::NoTarget, "", "" },
		};
	}

	// Legacy SurfaceMasterNode (typeName "Material"): approximate the
	// Blinn-Phong sockets onto PBR keys. Specular/Ambient have no
	// HlmsPbs-compatible target; they fall through as unsupported when fed.
	return {
		{ "Diffuse", SlotSpec::ColorSlot, "baseColor", "baseColorMap" },
		{ "Specular", SlotSpec::NoTarget, "", "" },
		// Shininess is a gloss value; roughness is its inverse. Heuristic
		// pending the phase-2 parity pass: values above 1 are treated as the
		// classic 0-100 Blinn exponent range.
		{ "Shininess", SlotSpec::FloatSlot, "roughness", "", true },
		{ "Normal", SlotSpec::NormalSlot, "", "normalMap" },
		{ "Ambient", SlotSpec::NoTarget, "", "" },
		{ "Emission", SlotSpec::ColorSlot, "emissiveColor", "emissiveMap" },
		{ "Alpha", SlotSpec::FloatSlot, "alpha", "" },
		{ "Alpha Cutoff", SlotSpec::FloatSlot, "alphaCutoff", "" },
		{ "Vertex Offset", SlotSpec::NoTarget, "", "" },
		{ "Vertex Extrusion", SlotSpec::NoTarget, "", "" },
	};
}

SocketModel* findInSocket(NodeModel* node, const QString& name)
{
	for (auto sock : node->inSockets)
		if (sock->name == name) return sock;
	return nullptr;
}

} // namespace

PbrGraphEvaluator::Result PbrGraphEvaluator::evaluate(NodeGraph* graph, TextureResolver resolver)
{
	Result result;
	if (!graph) return result;

	auto master = graph->getMasterNode();
	if (!master) return result;
	result.hasPbrMaster = (master->typeName == "PbrMaterial");

	if (!resolver) {
		// Default: the stored value is already a path (tests, standalone
		// builds). Studio passes a TextureManager-backed resolver that maps
		// asset GUIDs to files - see MaterialHelper.
		resolver = [](const QString& value) { return value; };
	}

	for (const auto& spec : specsForMaster(master->typeName)) {
		auto sock = findInSocket(master, spec.socketName);
		if (!sock || !sock->hasConnection()) continue;

		auto input = evaluateInput(sock, resolver);
		auto unsupported = [&](const QString& what) {
			result.unsupportedNodes.append(spec.socketName + " <- " + what);
		};

		switch (input.kind) {
		case InputValue::None:
			break;
		case InputValue::Unsupported:
			unsupported(input.describe);
			break;
		case InputValue::TexturePath:
			if (!spec.mapKey.isEmpty()) result.values[spec.mapKey] = input.path;
			else unsupported("texture");
			break;
		case InputValue::Color:
			if (spec.target == SlotSpec::ColorSlot) result.values[spec.valueKey] = colorToJson(input.color);
			else unsupported("color");
			break;
		case InputValue::Scalar:
			if (spec.target == SlotSpec::FloatSlot) {
				float v = input.scalar;
				if (spec.invertToRoughness) {
					float gloss = v > 1.0f ? v / 100.0f : v;
					v = 1.0f - qBound(0.0f, gloss, 1.0f);
				}
				result.values[spec.valueKey] = v;
			}
			else if (spec.target == SlotSpec::ColorSlot) {
				// a bare float feeding a color slot: grayscale
				result.values[spec.valueKey] = colorToJson(QColor::fromRgbF(
					qBound(0.0f, input.scalar, 1.0f), qBound(0.0f, input.scalar, 1.0f),
					qBound(0.0f, input.scalar, 1.0f)));
			}
			else unsupported("float");
			break;
		}
	}

	// A connected alpha-cutoff means the author wants cutout transparency.
	if (result.values.contains("alphaCutoff") && result.values["alphaCutoff"].toDouble() > 0.0)
		result.values["alphaMode"] = 1;

	if (!result.unsupportedNodes.isEmpty()) {
		qWarning() << "PbrGraphEvaluator: unsupported inputs on"
		           << graph->settings.name << "- material defaults used for:"
		           << result.unsupportedNodes.join(", ")
		           << "(per-texel baking is Option B phase 2)";
	}

	return result;
}

iris::PbrMaterialPtr PbrGraphEvaluator::materialFromValues(const QJsonObject& values)
{
	auto material = iris::PbrMaterial::create();

	static const QStringList colorKeys = { "baseColor", "emissiveColor" };
	static const QStringList mapKeys = { "baseColorMap", "metallicMap", "roughnessMap",
	                                     "normalMap", "occlusionMap", "emissiveMap" };

	for (auto it = values.begin(); it != values.end(); ++it) {
		const auto& key = it.key();
		if (colorKeys.contains(key))
			material->setValue(key, values[key].toObject().isEmpty()
			                            ? QVariant(QColor())
			                            : QVariant(colorFromJson(values[key].toObject())));
		else if (mapKeys.contains(key))
			material->setValue(key, values[key].toString());
		else if (key == "alphaMode")
			material->setValue(key, values[key].toInt());
		else
			material->setValue(key, values[key].toDouble());
	}

	return material;
}

iris::PbrMaterialPtr PbrGraphEvaluator::createMaterial(NodeGraph* graph, TextureResolver resolver)
{
	return materialFromValues(evaluate(graph, resolver).values);
}
