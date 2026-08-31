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

// CPU evaluation of a shader graph into iris::PbrMaterial inputs. No GL, no
// engine calls - pure graph walking, so it is safe in any viewport mode and
// in headless tests.
//
// Since the Materials Evaluator program (SPECS/MATERIALS_EVALUATOR_SPEC.md)
// this is the front end over materials::BakeProgram: every pure chain folds
// to a uniform value (float -> add -> Base Color folds), bare texture chains
// bind their source image (Passthrough), and UV-varying chains classify as
// Baked - the per-texel baker's input (GraphBaker). Approximated/animated
// nodes (worldNormal, fresnel, depth, time, pulsate, ...) evaluate against
// the fake fragment context and are named in Result::approximatedNodes;
// nothing silently produces a wrong value without being listed.
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
		// (Until the per-texel baker runs, UV-varying chains land here too.)
		QStringList unsupportedNodes;

		// "Socket <- nodeType" entries for chains that evaluated against the
		// fake fragment context (worldNormal/fresnel/depth/time/pulsate...).
		// Info, not an error: the value is a documented approximation.
		QStringList approximatedNodes;

		// True when any time-dependent node fed a connected master socket
		// (evaluated at the bake parameter t, default 0).
		bool animated = false;

		// True when the graph's master is a PbrMasterNode (typeName
		// "PbrMaterial"); false for the legacy SurfaceMasterNode ("Material"),
		// whose Blinn-Phong sockets are approximated onto PBR keys.
		bool hasPbrMaster = false;
	};

	static Result evaluate(NodeGraph* graph, TextureResolver resolver = {});

	// The classifier exposed (graph.bakeInfo): master socket name ->
	// "uniform" | "passthrough" | "baked" | "unsupported" | "unconnected",
	// after the per-socket landing rules (a varying Alpha Cutoff or a fed
	// Vertex Offset/Extrusion socket reports "unsupported").
	static QJsonObject bakeInfo(NodeGraph* graph, TextureResolver resolver = {});

	// Builds an iris::PbrMaterial from an evaluation result / a stored
	// "pbrMaterial.values" object, applying every key through setValue so the
	// editor panel's Property list stays in step. The resolver re-absolutizes
	// project-relative baked-map paths (BakedMaps/...) when given.
	static iris::PbrMaterialPtr materialFromValues(const QJsonObject& values,
	                                               TextureResolver resolver = {});
	static iris::PbrMaterialPtr createMaterial(NodeGraph* graph, TextureResolver resolver = {});
};
