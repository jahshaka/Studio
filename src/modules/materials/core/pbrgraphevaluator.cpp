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
	                                     "normalMap", "occlusionMap", "emissiveMap" };

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
		else
			material->setValue(key, values[key].toDouble());
	}

	return material;
}

iris::PbrMaterialPtr PbrGraphEvaluator::createMaterial(NodeGraph* graph, TextureResolver resolver)
{
	return materialFromValues(evaluate(graph, resolver).values, resolver);
}
