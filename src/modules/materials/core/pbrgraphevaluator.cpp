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
#include "bakeprogram.h"

#include "irisgl/document/materials/pbrmaterial.h"

namespace
{

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

// The QColor a folded chain value lands as on a color slot. arity 1 splats
// to grayscale (GLSL-exact float->vec3); arity 2 lands (x, y, 0) - the
// audit D5 contract for vector2, generalized to every vec2-valued root
// (documented divergence from GLSL's v.xyy, spec section 1.2); wider
// values land their leading components with w as alpha when present.
QColor colorFromValue(const materials::Value& v)
{
	auto c = [](double d) { return qBound(0.0, d, 1.0); };
	if (v.arity == 1) return QColor::fromRgbF(c(v.x), c(v.x), c(v.x));
	if (v.arity == 2) return QColor::fromRgbF(c(v.x), c(v.y), 0.0);
	return QColor::fromRgbF(c(v.x), c(v.y), c(v.z), v.arity == 4 ? c(v.w) : 1.0);
}

QString rootTypeName(const materials::BakeProgram& program)
{
	if (program.rootOp < 0 || program.rootOp >= program.ops.size()) return QStringLiteral("?");
	return program.ops[program.rootOp].typeName;
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

	const materials::EvalContext ctx; // fake fragment context, t = 0

	for (const auto& spec : specsForMaster(master->typeName)) {
		auto sock = findInSocket(master, spec.socketName);
		if (!sock || !sock->hasConnection()) continue;

		auto program = materials::BakeProgram::compile(sock, resolver);
		auto unsupported = [&](const QString& what) {
			result.unsupportedNodes.append(spec.socketName + " <- " + what);
		};

		using SocketClass = materials::BakeProgram::SocketClass;
		switch (program.classification) {
		case SocketClass::Unconnected:
			break;
		case SocketClass::Unsupported:
			for (const auto& what : program.unsupportedNodes) unsupported(what);
			break;
		case SocketClass::Passthrough:
			if (!spec.mapKey.isEmpty()) result.values[spec.mapKey] = program.passthroughPath;
			else unsupported("texture");
			break;
		case SocketClass::Baked:
			// the per-texel baker (GraphBaker) plugs in here; a plain
			// evaluate() does not bake, so defaults are used and the chain
			// is honestly reported
			unsupported(rootTypeName(program));
			break;
		case SocketClass::Uniform: {
			const materials::Value v = program.evaluate(ctx);
			if (spec.target == SlotSpec::FloatSlot) {
				double s = v.x; // vecN -> float: leading component; double math
				                // end-to-end (uniform folds are exact doubles)
				if (spec.invertToRoughness) {
					double gloss = s > 1.0 ? s / 100.0 : s;
					s = 1.0 - qBound(0.0, gloss, 1.0);
				}
				// every FloatSlot (metallic, roughness, occlusionFactor,
				// alpha, alphaCutoff) is a 0-1 quantity on PbrMaterial;
				// clamp at the landing site (audit D6)
				result.values[spec.valueKey] = qBound(0.0, s, 1.0);
			}
			else if (spec.target == SlotSpec::ColorSlot) {
				result.values[spec.valueKey] = colorToJson(colorFromValue(v));
			}
			else {
				unsupported(rootTypeName(program)); // Normal is a map-only slot
			}
			break;
		}
		}

		if (program.classification == SocketClass::Uniform
		    || program.classification == SocketClass::Baked) {
			for (const auto& name : program.approximatedNodes)
				result.approximatedNodes.append(spec.socketName + " <- " + name);
			result.animated |= program.animated;
		}
	}

	// A connected alpha-cutoff means the author wants cutout transparency.
	if (result.values.contains("alphaCutoff") && result.values["alphaCutoff"].toDouble() > 0.0)
		result.values["alphaMode"] = 1;

	if (!result.unsupportedNodes.isEmpty()) {
		qWarning() << "PbrGraphEvaluator: unsupported inputs on"
		           << graph->settings.name << "- material defaults used for:"
		           << result.unsupportedNodes.join(", ");
	}

	return result;
}

QJsonObject PbrGraphEvaluator::bakeInfo(NodeGraph* graph, TextureResolver resolver)
{
	QJsonObject out;
	QJsonObject perSocket;
	if (!graph || !graph->getMasterNode()) {
		out["perSocket"] = perSocket;
		return out;
	}
	if (!resolver) resolver = [](const QString& value) { return value; };

	auto master = graph->getMasterNode();
	for (const auto& spec : specsForMaster(master->typeName)) {
		auto sock = findInSocket(master, spec.socketName);
		if (!sock) continue;
		if (!sock->hasConnection()) {
			perSocket[spec.socketName] = "unconnected";
			continue;
		}

		auto program = materials::BakeProgram::compile(sock, resolver);
		using SocketClass = materials::BakeProgram::SocketClass;
		QString cls = materials::BakeProgram::classToString(program.classification);

		// per-socket landing rules (spec section 1.3)
		if (spec.target == SlotSpec::NoTarget) {
			cls = "unsupported"; // Vertex Offset / Extrusion: Option C future
		}
		else if (program.classification == SocketClass::Baked && spec.socketName == "Alpha Cutoff") {
			cls = "unsupported"; // a varying cutoff cannot land
		}
		else if (program.classification == SocketClass::Passthrough && spec.mapKey.isEmpty()) {
			cls = "unsupported"; // texture into a value-only slot
		}
		else if (program.classification == SocketClass::Uniform && spec.target == SlotSpec::NormalSlot) {
			cls = "unsupported"; // Normal is a map-only slot
		}
		perSocket[spec.socketName] = cls;
	}
	out["perSocket"] = perSocket;
	return out;
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
