/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#pragma once

// Materials Evaluator phase 2 (SPECS/MATERIALS_EVALUATOR_SPEC.md sections
// 1.3, 1.6, 2): the per-texel baker. UV-varying chains evaluate over [0,1]^2
// at bake resolution into PNGs per master map slot; uniform chains fold to
// values; bare texture chains pass through. GraphBaker::run IS the evaluator
// (PbrGraphEvaluator::evaluate delegates here with baking disabled) so the
// landing rules exist exactly once.
//
// Output storage (section 1.6): <outputDir>/<mapKey>-<hash16>.png where
// hash16 = first 16 hex of SHA-1 over (compiled-chain signature + resolution
// + time + source-image stamps). An existing file is a cache hit - not
// rewritten. Stale files in outputDir are pruned after a successful bake.
// Emitted map values carry relativePrefix + filename; the TextureResolver
// seam re-absolutizes them at material-build time.
//
// Engine-interplay landing rules (the factors MULTIPLY the sampled maps in
// HlmsPbs): a baked/passthrough metallic or roughness map lands factor 1.0,
// an emissive map lands emissiveColor white + emissiveIntensity 1, a
// baked/synthesized baseColorMap suppresses the baseColor value (material
// default is white). Alpha packs into baseColorMap.A (section 1.3): a baked
// alpha chain forces baseColorMap synthesis and alphaMode=2 unless a cutoff
// is set.

#include <QJsonObject>
#include <QString>
#include <QVector>

#include "bakeprogram.h"
#include "pbrgraphevaluator.h"

class NodeGraph;
class NodeModel;
class SocketModel;

namespace materials {

// How one master input socket lands on the PbrMaterial (shared by the
// evaluator, the baker and the bakeInfo classifier).
struct MasterSlot
{
	enum Target { ColorSlot, FloatSlot, NormalSlot, NoTarget };
	QString socketName;
	Target target;
	QString valueKey; // constant lands here ("" = constants unsupported)
	QString mapKey;   // texture lands here ("" = textures unsupported)
	bool invertToRoughness = false; // legacy Shininess -> roughness
};

QVector<MasterSlot> masterSlotsFor(const QString& masterTypeName);
SocketModel* findMasterInSocket(NodeModel* master, const QString& name);

class GraphBaker
{
public:
	struct Options
	{
		int resolution = 1024;   // bake resolution (square), clamped 1..4096
		double time = 0.0;       // the bake parameter (spec 1.4)
		QString outputDir;       // absolute directory for PNGs; empty + bakeMaps => no files land
		QString relativePrefix;  // prepended to emitted map values (e.g. "BakedMaps/<guid>/")
		bool bakeMaps = true;    // false = evaluator mode: Baked chains report unsupported
		bool pruneStale = true;  // remove PNGs in outputDir not produced by this bake
	};

	struct Result
	{
		PbrGraphEvaluator::Result eval; // values, unsupported, approximated, animated, hasPbrMaster
		QJsonObject maps;        // mapKey -> emitted (prefixed) path, baked this run or cache-hit
		QJsonObject passthrough; // mapKey -> source path bound directly
		qint64 msElapsed = 0;
	};

	// The compile/execute split (spec section 1.1): compile() snapshots the
	// graph into pure value objects on the GUI thread (node models carry
	// live QWidgets); runCompiled() is safe on any thread - the preview path
	// runs it under QtConcurrent.
	struct CompiledSlot
	{
		MasterSlot slot;
		bool connected = false;
		BakeProgram program;
	};
	struct CompiledGraph
	{
		bool hasMaster = false;
		bool hasPbrMaster = false;
		QString name;
		QVector<CompiledSlot> sockets;
	};

	static CompiledGraph compile(NodeGraph* graph, BakeProgram::TextureResolver resolver = {});
	static Result runCompiled(const CompiledGraph& compiled, const Options& opts);

	static Result run(NodeGraph* graph, const Options& opts,
	                  BakeProgram::TextureResolver resolver = {});

	// graph.bakeInfo: master socket name -> classification string, after the
	// per-socket landing rules.
	static QJsonObject classify(NodeGraph* graph, BakeProgram::TextureResolver resolver = {});
};

} // namespace materials
