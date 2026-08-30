/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#pragma once

#include <QJsonObject>
#include <QStringList>
#include <functional>

#include "irisgl/irisglfwd.h"

class NodeGraph;
class NodeModel;
class SocketModel;

// Option B phase 1: CPU evaluation of a shader graph into iris::PbrMaterial
// inputs. No GL, no engine calls - pure graph walking, so it is safe in any
// viewport mode and in headless tests.
//
// Covers the subset the 15 shipped .effect presets actually use (audit
// MATERIALS_EFFECTS_AUDIT.md section 0.4): constants ("float", "color",
// "vector3"/"vector4") and "property" nodes (float/int/color/texture) feeding
// the master sockets directly, plus the inline "texture" node. Anything else
// (math chains, procedural, animated or view-dependent nodes) falls back to
// the material's defaults and is reported in Result::unsupportedNodes.
//
// TODO(bake): phase 2 replaces the "unsupported" fallback for PURE/UV node
// chains with a per-texel bake to a texture asset, which then feeds the same
// map keys this evaluator emits. The seam is evaluateInput() below - a chain
// it cannot fold becomes a bake request there.
class PbrGraphEvaluator
{
public:
	// Maps a texture property's stored value (an asset GUID inside Studio, a
	// plain path in tests/standalone) to an image path. The default resolver
	// returns file paths unchanged and leaves anything else as-is.
	using TextureResolver = std::function<QString(const QString&)>;

	struct Result
	{
		// Keys match iris::PbrMaterial::setValue: baseColor, metallic,
		// roughness, occlusionFactor, emissiveColor, alpha, alphaCutoff,
		// alphaMode, and the maps baseColorMap, metallicMap, roughnessMap,
		// normalMap, occlusionMap, emissiveMap (as paths). Colors are stored
		// as {r,g,b,a} objects with 0-1 floats.
		QJsonObject values;

		// "Socket <- nodeType" entries for connections that could not be
		// folded to a constant or a map. Defaults were used for these.
		QStringList unsupportedNodes;

		// True when the graph's master is a PbrMasterNode (typeName
		// "PbrMaterial"); false for the legacy SurfaceMasterNode ("Material"),
		// whose Blinn-Phong sockets are approximated onto PBR keys.
		bool hasPbrMaster = false;
	};

	static Result evaluate(NodeGraph* graph, TextureResolver resolver = {});

	// Builds an iris::PbrMaterial from an evaluation result / a stored
	// "pbrMaterial.values" object, applying every key through setValue so the
	// editor panel's Property list stays in step.
	static iris::PbrMaterialPtr materialFromValues(const QJsonObject& values);
	static iris::PbrMaterialPtr createMaterial(NodeGraph* graph, TextureResolver resolver = {});
};
