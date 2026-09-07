/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#pragma once

// HLMS_ADOPTION P5: the SHADER-PIECE EMITTER — a second backend for the
// evaluator's compiler.
//
// The Materials Evaluator program (MATERIALS_EVALUATOR_SPEC) already lowers a
// node graph into a BakeProgram: a flat post-order op list with resolved input
// references, whose CPU semantics are pinned by shadergraph.baker_parity. This
// file walks that SAME op list and writes GLSL instead of evaluating it — so
// the graph gets two backends with one front end, and the differential oracle
// (shadergraph.emitter_parity) is what keeps them honest.
//
// WHAT A PIECE IS. Ogre-Next lets a single material splice generated source
// into named hook points of its own shader templates. We target two:
//   custom_ps_preLights    — the surface, fully assembled, before any light is
//                            accumulated: exactly the master surface sockets.
//   custom_vs_preTransform — world-space vertex position before the
//                            view-projection: the Vertex Offset / Vertex
//                            Extrusion sockets, which NO CPU bake can express
//                            and which have read "unsupported" since the
//                            evaluator shipped.
//
// WHAT IT BUYS over the baker: no bake resolution (the surface is evaluated per
// pixel, not per texel of a 1024² PNG), no frozen clock (t is a pass uniform,
// so a graph animates without one recompile or one re-bake), and vertex
// motion. What it COSTS is stated plainly in the spec's risk table: we own a
// shader generator. It emits OUR pieces into OGRE'S documented hooks — it is
// not a fork of the templates and not a custom Hlms.
//
// THE BAKER IS NOT REPLACED. It stays as (1) the fallback for every graph this
// emitter refuses, with a NAMED reason per socket, (2) the web-export path
// (glTF has no shader pieces, so exportWeb keeps consuming baked maps), and
// (3) the headless/no-render-system path. A graph that emits a piece for Base
// Color still bakes everything the piece does not own.
//
// THE PIECE-COLLISION RULE (HLMS_ADOPTION_SPEC §7.4). Custom pieces are parsed
// after the Hlms library folders, and a DUPLICATE piece name fails the whole
// shader — with one line in the log and a black frame. Our own library
// (irisgl/engine/media/Hlms/Jahshaka) already defines custom_passBuffer,
// custom_VStoPS, custom_ps_posExecution, custom_ps_uv_modifier_macros and an
// override of DoAtmosphereNprSky. The emitter therefore defines EXACTLY ONE
// piece per stage, from a hard-coded allow-list, and never an interpolant:
// everything the pixel stage needs comes from `inPs` and the pass buffer.

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include "bakeprogram.h"
#include "graphbaker.h"

namespace materials {

class PieceEmitter
{
public:
	/// What the emitter made of one graph.
	struct Result
	{
		/// True when at least one piece was emitted. False means "the baker
		/// handles this graph exactly as it did before P5" — never an error.
		bool accepted = false;

		QString pixelSource;    ///< custom_ps_preLights piece, empty if none
		QString vertexSource;   ///< custom_vs_preTransform piece, empty if none

		/// Master socket names the PIECE owns. The baker must skip these:
		/// the piece overwrites what a baked map would have contributed, so
		/// baking them would cost a PNG that nothing reads.
		QStringList emittedSockets;

		/// Why every socket the emitter did NOT take was left to the baker,
		/// by socket name. A socket that simply is not connected is absent.
		/// This is what graph.emitInfo() reports, and the reason a user is
		/// never left guessing why their graph did not animate.
		QMap<QString, QString> fallbackReasons;

		/// True when any emitted chain reads the clock (time / pulsate, or a
		/// panner/flipbook fed by one). The host pushes Scene::setShaderTime
		/// every frame for these and can skip it for the rest.
		bool animated = false;
	};

	/// Lowers a compiled graph. Pure: no files are written, nothing is
	/// resolved against a project, and it is safe off the GUI thread (a
	/// CompiledGraph is a value object by construction).
	static Result lower(const GraphBaker::CompiledGraph &compiled);

	/// Convenience: compile + emit.
	static Result lower(NodeGraph *graph, BakeProgram::TextureResolver resolver = {});

	/// The CONTENT-ADDRESSED file name for a piece source: the first 16 hex of
	/// a SHA-256 over the source, plus the stage's suffix.
	///
	/// Content addressing is not a nicety here, it is what makes three
	/// separate hazards unreachable (HLMS_ADOPTION_SPEC §7.3):
	///   * Ogre's piece registry is keyed by FILE NAME and throws when one
	///     name is registered twice with different content. Same name now
	///     implies same content, by construction.
	///   * Two identical graphs share one file and therefore one compiled
	///     shader permutation — deduplication for free.
	///   * An EDITED graph is a NEW name, so only its own cache entries are
	///     invalidated; the upstream revalidation bug at OgreHlms.cpp:4112
	///     (`&&` where `||` was meant) is unreachable for us, because we never
	///     re-use a name with changed content.
	static QString fileNameFor(const QString &source, bool vertexStage);

	/// The per-user cache directory pieces live in — NOT the project. Pieces
	/// are a regenerable cache, exactly like BakedMaps: the graph is the truth
	/// and the piece is derived, so a project archive ships the graph.
	///
	/// Per-USER rather than per-project for a hard reason: a piece file that
	/// disappears aborts the ENTIRE Hlms disk cache load (OgreHlmsDiskCache.cpp
	/// applyTo() clears the whole shader cache and returns), so project-local
	/// pieces would mean opening project B threw away every shader project A
	/// had compiled. One stable directory keeps every cached shader valid.
	static QString cacheDir();

	/// Writes `source` into `dir` under its content-addressed name and returns
	/// the absolute path. An existing file with that name is a CACHE HIT and is
	/// not rewritten (same name implies same content). Empty on failure.
	static QString write(const QString &dir, const QString &source, bool vertexStage);

	/// emit() + write() for both stages, into cacheDir(). The two paths land on
	/// the returned object as `customPiecePixel` / `customPieceVertex` — the
	/// keys iris::PbrMaterial::setValue takes, so an emitted graph flows into a
	/// document material through exactly the same path baked maps do.
	static QJsonObject emitAndStore(NodeGraph *graph, BakeProgram::TextureResolver resolver = {},
	                                Result *resultOut = nullptr);

	/// Every op key the emitter can lower, for the coverage report and for
	/// tests that assert the table has not silently shrunk.
	static const QStringList &supportedOps();
	/// The master socket names the emitter can land, in master-slot order.
	static const QStringList &supportedSockets();
};

} // namespace materials
