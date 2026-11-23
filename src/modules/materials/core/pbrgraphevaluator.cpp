/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "pbrgraphevaluator.h"
#include "app/qtstd.h"
#include "app/qtstdgui.h"
#include "irisgl/document/properties/property.h"
#include <QFileInfo>

#include <QJsonArray>

#include <QColor>
#include <QDebug>
#include <QJsonObject>

#include "graphbaker.h"

#include "irisgl/document/materials/pbrmaterial.h"

namespace
{

} // namespace

PbrGraphEvaluator::Result PbrGraphEvaluator::evaluate(NodeGraph* graph, TextureResolver resolver)
{
	// The landing rules live exactly once, in GraphBaker; evaluating is a
	// bake run with map baking disabled (Baked chains report unsupported).
	materials::GraphBaker::Options opts;
	opts.bakeMaps = false;
	return materials::GraphBaker::run(graph, opts, resolver).eval;
}

QJsonObject PbrGraphEvaluator::bakeInfo(NodeGraph* graph, TextureResolver resolver)
{
	return materials::GraphBaker::classify(graph, resolver);
}

iris::PbrMaterialPtr PbrGraphEvaluator::materialFromValues(const QJsonObject& values,
                                                           TextureResolver resolver)
{
	auto material = iris::PbrMaterial::create();

	static const QStringList colorKeys = { "baseColor", "emissiveColor" };
	static const QStringList mapKeys = { "baseColorMap", "metallicMap", "roughnessMap",
	                                     "normalMap", "emissiveMap" };

	for (auto it = values.begin(); it != values.end(); ++it) {
		const auto& key = it.key();
		if (colorKeys.contains(key))
			// The colour's Object straight in (its shape is the file's {r,g,b,a}):
			// an empty object, or a value that is not one, reads as the invalid
			// colour exactly as before (valueshapes.h's absent).
			material->setValue(key.toStdString(), qtstd::toValue(QJsonValue(values[key].toObject())));
		else if (mapKeys.contains(key)) {
			// A stored map is the texture's ASSET GUID (a baked map is a
			// member Texture row of the bundle); the resolver turns it into the
			// file, and the row keeps the guid beside it (TEX-REF-1) so a node
			// wearing this material saves the reference, not the path. A value
			// that is already a file (a graph texture node a session loaded)
			// has no identity to carry.
			const QString stored = values[key].toString();
			if (stored.isEmpty() || QFileInfo::exists(stored)) {
				material->setValue(key.toStdString(), iris::Value(stored.toStdString()));
			} else {
				const QString path = resolver ? resolver(stored) : QString();
				material->setValue(key.toStdString(), iris::Material::textureRef(
					path == stored ? std::string() : path.toStdString(), stored.toStdString()));
			}
		}
		else if (key == "alphaMode")
			material->setValue("alphaMode", iris::Value(values[key].toInt()));
		// THE FOLDED UV TRANSFORM (MATERIAL_UV_NODES_SPEC 3.2). `textureScale`
		// is a two-element array when the axes differ and a plain number when
		// they do not — and every material written before per-axis tiling
		// existed carries the number, so both spellings are read here forever.
		// THE SCROLL (TORNADO-1): the animated UV fold's velocity, a two-element
		// array in UV units per second of the shader clock.
		else if (key == "textureVelocity") {
			const auto arr = values[key].toArray();
			material->setValue("textureVelocityU", iris::Value(arr.size() > 0 ? arr[0].toDouble() : 0.0));
			material->setValue("textureVelocityV", iris::Value(arr.size() > 1 ? arr[1].toDouble() : 0.0));
		}
		else if (key == "textureScale" || key == "textureOffset") {
			const auto val = values[key];
			const bool isScale = key == "textureScale";
			double u = isScale ? 1.0 : 0.0, v = u;
			if (val.isArray()) {
				const auto arr = val.toArray();
				u = arr.size() > 0 ? arr[0].toDouble(u) : u;
				v = arr.size() > 1 ? arr[1].toDouble(u) : u;
			}
			else {
				u = v = val.toDouble(u);
			}
			if (isScale) {
				material->setValue("textureScale", iris::Value(u));   // sets both axes
				material->setValue("textureScaleV", iris::Value(v));
			}
			else {
				material->setValue("textureOffsetU", iris::Value(u));
				material->setValue("textureOffsetV", iris::Value(v));
			}
		}
		else {
			// Every other value is a NUMBER in the file, converted BY THE ROW's
			// KIND at this edge (qtstd::toRowValue): a Bool row's toBool, an Int /
			// List row's toInt, a Float row's toFloat, as the rows' QVariant setters
			// did; a field with no row (the scroll's U / V) takes the Number.
			const std::string name = key.toStdString();
			const QVariant number(values[key].toDouble());
			std::optional<iris::Value> value = iris::Value(number.toDouble());
			for (const iris::Property *prop : material->properties())
				if (prop->name == name) { value = qtstd::toRowValue(prop->type, number); break; }
			// A number converts for every numeric kind (a refusal cannot happen here: the file holds numbers).
			if (value) material->setValue(name, *value);
		}
	}

	return material;
}

iris::PbrMaterialPtr PbrGraphEvaluator::createMaterial(NodeGraph* graph, TextureResolver resolver)
{
	return materialFromValues(evaluate(graph, resolver).values, resolver);
}
