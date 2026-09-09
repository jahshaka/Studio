/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "pbrgraphevaluator.h"

#include <QJsonArray>

#include <QColor>
#include <QDebug>
#include <QJsonObject>

#include "graphbaker.h"

#include "irisgl/document/materials/pbrmaterial.h"

namespace
{

QColor colorFromJson(const QJsonObject& obj)
{
	return QColor::fromRgbF(obj["r"].toDouble(), obj["g"].toDouble(),
	                        obj["b"].toDouble(), obj["a"].toDouble(1.0));
}

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
			material->setValue(key, values[key].toObject().isEmpty()
			                            ? QVariant(QColor())
			                            : QVariant(colorFromJson(values[key].toObject())));
		else if (mapKeys.contains(key)) {
			// baked maps store project-relative paths (BakedMaps/...); the
			// resolver seam re-absolutizes them at material-build time
			const QString stored = values[key].toString();
			material->setValue(key, resolver ? resolver(stored) : stored);
		}
		else if (key == "alphaMode")
			material->setValue(key, values[key].toInt());
		// THE FOLDED UV TRANSFORM (MATERIAL_UV_NODES_SPEC 3.2). `textureScale`
		// is a two-element array when the axes differ and a plain number when
		// they do not — and every material written before per-axis tiling
		// existed carries the number, so both spellings are read here forever.
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
				material->setValue(QStringLiteral("textureScale"), u);   // sets both axes
				material->setValue(QStringLiteral("textureScaleV"), v);
			}
			else {
				material->setValue(QStringLiteral("textureOffsetU"), u);
				material->setValue(QStringLiteral("textureOffsetV"), v);
			}
		}
		else
			material->setValue(key, values[key].toDouble());
	}

	return material;
}

iris::PbrMaterialPtr PbrGraphEvaluator::createMaterial(NodeGraph* graph, TextureResolver resolver)
{
	return materialFromValues(evaluate(graph, resolver).values, resolver);
}
