/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "graphbaker.h"

#include "services/filewriteatomic.h"

#include <QColor>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QMutex>
#include <QSet>
#include <QtConcurrent>
#include <cmath>
#include <numeric>

#include "../graph/nodegraph.h"
#include "../models/nodemodel.h"
#include "../models/socketmodel.h"

namespace materials {

// ------------------------------------------------------------ master slots

QVector<MasterSlot> masterSlotsFor(const QString& masterType)
{
	if (masterType == "PbrMaterial") {
		return {
			{ "Base Color", MasterSlot::ColorSlot, "baseColor", "baseColorMap" },
			{ "Metallic", MasterSlot::FloatSlot, "metallic", "metallicMap" },
			{ "Roughness", MasterSlot::FloatSlot, "roughness", "roughnessMap" },
			{ "Normal", MasterSlot::NormalSlot, "", "normalMap" },
			// NO "Occlusion" slot: HLMS_ADOPTION P2 removed the socket, the
			// bake output and the document rows together. It used to bake a
			// full-resolution occlusionMap PNG into the user's project that
			// nothing on any render path ever read.
			{ "Emissive", MasterSlot::ColorSlot, "emissiveColor", "emissiveMap" },
			{ "Alpha", MasterSlot::FloatSlot, "alpha", "" },
			{ "Alpha Cutoff", MasterSlot::FloatSlot, "alphaCutoff", "" },
			{ "Vertex Offset", MasterSlot::NoTarget, "", "" },
			{ "Vertex Extrusion", MasterSlot::NoTarget, "", "" },
		};
	}

	// Legacy SurfaceMasterNode (typeName "Material"): approximate the
	// Blinn-Phong sockets onto PBR keys. Specular/Ambient have no
	// HlmsPbs-compatible target; they fall through as unsupported when fed.
	return {
		{ "Diffuse", MasterSlot::ColorSlot, "baseColor", "baseColorMap" },
		{ "Specular", MasterSlot::NoTarget, "", "" },
		// Shininess is a gloss value; roughness is its inverse. Values above
		// 1 are treated as the classic 0-100 Blinn exponent range.
		{ "Shininess", MasterSlot::FloatSlot, "roughness", "", true },
		{ "Normal", MasterSlot::NormalSlot, "", "normalMap" },
		{ "Ambient", MasterSlot::NoTarget, "", "" },
		{ "Emission", MasterSlot::ColorSlot, "emissiveColor", "emissiveMap" },
		{ "Alpha", MasterSlot::FloatSlot, "alpha", "" },
		{ "Alpha Cutoff", MasterSlot::FloatSlot, "alphaCutoff", "" },
		{ "Vertex Offset", MasterSlot::NoTarget, "", "" },
		{ "Vertex Extrusion", MasterSlot::NoTarget, "", "" },
	};
}

SocketModel* findMasterInSocket(NodeModel* master, const QString& name)
{
	for (auto sock : master->inSockets)
		if (sock->name == name) return sock;
	return nullptr;
}

// ----------------------------------------------------------------- helpers

namespace {

// A master input the PBR target has nowhere to put, said ONCE per process per
// (socket, kind).
//
// The nine legacy presets all feed a SPECULAR MAP into a socket with no
// HlmsPbs-compatible target (`{ "Specular", MasterSlot::NoTarget }` — a
// specular colour map is not a PBR input, and converting one needs the
// spec-gloss -> metal-rough fit the GLB importer does, which is a follow-up,
// not a bake rule). It was dropped in complete silence: the texture is listed
// in Result::eval.unsupportedNodes, which only the materials panel reads, so
// from a script, a thumbnail or a preview dock the map simply never existed.
// Once per key, because a bake runs on every graph edit and this is a property
// of the GRAPH, not of the edit.
void logUnsupportedOnce(const QString& socketName, const QString& what)
{
	static QMutex mutex;
	static QSet<QString> said;
	const QString key = socketName + QLatin1Char('|') + what;
	QMutexLocker lock(&mutex);
	if (said.contains(key)) return;
	said.insert(key);
	qWarning().noquote() << "materials: the" << socketName
	                     << "input (" + what + ") has no PBR target and is dropped by the bake";
}

QJsonObject colorToJson(const QColor& c)
{
	QJsonObject obj;
	obj["r"] = c.redF();
	obj["g"] = c.greenF();
	obj["b"] = c.blueF();
	obj["a"] = c.alphaF();
	return obj;
}

// The QColor a folded chain value lands as on a color slot. arity 1 splats
// to grayscale (GLSL-exact float->vec3); arity 2 lands (x, y, 0) - the
// audit D5 contract for vector2, generalized to every vec2-valued root;
// wider values land their leading components with w as alpha when present.
QColor colorFromValue(const Value& v)
{
	auto c = [](double d) { return qBound(0.0, d, 1.0); };
	if (v.arity == 1) return QColor::fromRgbF(c(v.x), c(v.x), c(v.x));
	if (v.arity == 2) return QColor::fromRgbF(c(v.x), c(v.y), 0.0);
	return QColor::fromRgbF(c(v.x), c(v.y), c(v.z), v.arity == 4 ? c(v.w) : 1.0);
}

QString rootTypeName(const BakeProgram& program)
{
	if (program.rootOp < 0 || program.rootOp >= program.ops.size()) return QStringLiteral("?");
	return program.ops[program.rootOp].typeName;
}

uchar toByte(double v)
{
	const int b = int(std::lround(qBound(0.0, v, 1.0) * 255.0));
	return uchar(qBound(0, b, 255));
}

// Writes a chain value's RGB bytes with the colorFromValue arity rules,
// without a QColor round-trip (which would quantize twice).
void valueToRgb(const Value& v, uchar* p)
{
	if (v.arity == 1) { p[0] = p[1] = p[2] = toByte(v.x); return; }
	p[0] = toByte(v.x);
	p[1] = toByte(v.y);
	p[2] = toByte(v.arity == 2 ? 0.0 : v.z);
}

QString hash16(const QByteArray& recipe)
{
	return QString::fromLatin1(
	    QCryptographicHash::hash(recipe, QCryptographicHash::Sha1).toHex().left(16));
}

// One master socket during a run: the compiled slot plus per-run state.
struct SlotState
{
	const GraphBaker::CompiledSlot* cs = nullptr;
	bool needsBake = false;
};

} // namespace

// ---------------------------------------------------------------- classify

QJsonObject GraphBaker::classify(NodeGraph* graph, BakeProgram::TextureResolver resolver)
{
	QJsonObject out;
	QJsonObject perSocket;
	if (!graph || !graph->getMasterNode()) {
		out["perSocket"] = perSocket;
		return out;
	}
	if (!resolver) resolver = [](const QString& value) { return value; };

	// THROUGH compile(), not per socket: the UV fold is a property of the whole
	// material, and a bakeInfo that classified each socket in isolation would
	// report "baked" for a chain the bake is about to pass through.
	const CompiledGraph compiled = compile(graph, resolver);

	auto master = graph->getMasterNode();
	for (const auto& cs : compiled.sockets) {
		const MasterSlot& slot = cs.slot;
		if (!cs.connected) {
			auto sock = findMasterInSocket(master, slot.socketName);
			if (sock) perSocket[slot.socketName] = "unconnected";
			continue;
		}

		const BakeProgram& program = cs.program;
		using SocketClass = BakeProgram::SocketClass;
		QString cls = BakeProgram::classToString(program.classification);

		// per-socket landing rules (spec section 1.3)
		if (slot.target == MasterSlot::NoTarget) {
			cls = "unsupported"; // Vertex Offset / Extrusion: Option C future
		}
		else if (program.classification == SocketClass::Baked && slot.socketName == "Alpha Cutoff") {
			cls = "unsupported"; // a varying cutoff cannot land
		}
		else if (program.classification == SocketClass::Passthrough && slot.mapKey.isEmpty()) {
			cls = "unsupported"; // texture into a value-only slot
		}
		else if (program.classification == SocketClass::Uniform && slot.target == MasterSlot::NormalSlot) {
			cls = "unsupported"; // Normal is a map-only slot
		}
		perSocket[slot.socketName] = cls;
	}
	out["perSocket"] = perSocket;

	// WHICH ROUTE THE TEXTURES TOOK. A script (and the panel) has to be able to
	// tell "tiled at render time, full resolution" from "resampled into a map",
	// because that difference is the entire reason the UV input exists.
	if (compiled.uvFold.valid) {
		QJsonObject fold;
		QJsonArray scale; scale.append(compiled.uvFold.scaleX); scale.append(compiled.uvFold.scaleY);
		fold["scale"] = scale;
		QJsonArray offset; offset.append(compiled.uvFold.offsetX); offset.append(compiled.uvFold.offsetY);
		fold["offset"] = offset;
		fold["rotation"] = compiled.uvFold.rotationDeg;
		fold["samplers"] = compiled.uvFold.samplers;
		out["fold"] = fold;
	}
	else {
		out["fold"] = QJsonValue::Null;
		if (!compiled.uvFold.reason.isEmpty()) out["foldReason"] = compiled.uvFold.reason;
	}
	// What LOADING this graph had to change (NodeGraph::migrationNotes). A
	// migration that drops a connection has to be visible somewhere a caller
	// actually looks, and bakeInfo is the "what will this graph produce, and
	// what will it not" report — so it is the honest place for it.
	if (!graph->migrationNotes.isEmpty())
		out["migrations"] = QJsonArray::fromStringList(graph->migrationNotes);
	return out;
}

// --------------------------------------------------------------------- run

GraphBaker::CompiledGraph GraphBaker::compile(NodeGraph* graph, BakeProgram::TextureResolver resolver)
{
	CompiledGraph out;
	if (!graph) return out;
	auto master = graph->getMasterNode();
	if (!master) return out;

	out.hasMaster = true;
	out.hasPbrMaster = (master->typeName == "PbrMaterial");
	out.name = graph->settings.name;
	out.blendMode = graph->settings.blendMode;
	if (!resolver) resolver = [](const QString& value) { return value; };

	for (const auto& slot : masterSlotsFor(master->typeName)) {
		CompiledSlot cs;
		cs.slot = slot;
		auto sock = findMasterInSocket(master, slot.socketName);
		cs.connected = sock && sock->hasConnection();
		if (cs.connected)
			cs.program = BakeProgram::compile(sock, resolver);
		out.sockets.append(cs);
	}

	resolveUvFold(out);
	return out;
}

// THE FOLD (MATERIAL_UV_NODES_SPEC 3.2, decision D-1a).
//
// Every socket's program answers "what one UV transform do MY samplers share",
// and this intersects those answers into the material's single transform. The
// moment two sockets disagree — or any socket cannot express its UVs as one
// constant transform of the mesh's — the whole material bakes, because a
// material carries ONE transform and a half-folded graph would render two
// different pictures from one authored surface.
//
// It refuses in one more case that no per-socket analysis can see: a rotation
// with a NORMAL map bound. Rotating the lookup does not counter-rotate the
// tangent-space vector the map encodes, on the baked route or the render-time
// one, so the surface is lit wrong either way — but at least the BAKED route
// keeps the two routes agreeing with each other, and the reason is reported
// rather than silently shipped (spec 3.3).
void GraphBaker::resolveUvFold(CompiledGraph& compiled)
{
	using UvFold = BakeProgram::UvFold;
	UvFold merged;
	bool haveAny = false;
	bool normalHasSampler = false;

	for (const auto& cs : compiled.sockets) {
		if (!cs.connected) continue;
		const UvFold f = cs.program.uvFold();
		if (!f.valid) {
			if (f.reason.isEmpty()) continue;   // no samplers here at all
			compiled.uvFold = UvFold();
			compiled.uvFold.reason = cs.slot.socketName + ": " + f.reason;
			return;
		}
		if (cs.slot.target == MasterSlot::NormalSlot && f.samplers > 0)
			normalHasSampler = true;
		if (!haveAny) {
			merged = f;
			haveAny = true;
			continue;
		}
		if (merged.scaleX != f.scaleX || merged.scaleY != f.scaleY
		    || merged.offsetX != f.offsetX || merged.offsetY != f.offsetY
		    || merged.rotationDeg != f.rotationDeg) {
			compiled.uvFold = UvFold();
			compiled.uvFold.reason =
			    QStringLiteral("two master inputs tile differently; the material carries "
			                   "one transform, so these bake");
			return;
		}
		merged.samplers += f.samplers;
	}

	if (!haveAny || merged.samplers == 0) return;   // nothing to fold

	if (normalHasSampler && merged.rotationDeg != 0.0) {
		compiled.uvFold = UvFold();
		compiled.uvFold.reason =
		    QStringLiteral("a rotated UV on a normal map would rotate the lookup but not the "
		                   "tangent-space normal it samples; baking keeps both routes agreeing");
		return;
	}

	compiled.uvFold = merged;

	// The identity is a fold that changes nothing: the classification is
	// already right (a `uv` node at its defaults reads as the bake UV) and
	// landing scale 1 / offset 0 / rotation 0 would add keys to every graph
	// material's stored values for no reason.
	const bool identity = merged.scaleX == 1.0 && merged.scaleY == 1.0
	                      && merged.offsetX == 0.0 && merged.offsetY == 0.0
	                      && merged.rotationDeg == 0.0;
	if (identity) return;

	for (auto& cs : compiled.sockets)
		if (cs.connected) cs.program.applyUvFold();
}

GraphBaker::Result GraphBaker::run(NodeGraph* graph, const Options& opts,
                                   BakeProgram::TextureResolver resolver)
{
	return runCompiled(compile(graph, resolver), opts);
}

GraphBaker::Result GraphBaker::runCompiled(const CompiledGraph& compiled, const Options& opts)
{
	Result out;
	QElapsedTimer timer;
	timer.start();

	if (!compiled.hasMaster) return out;
	out.eval.hasPbrMaster = compiled.hasPbrMaster;

	const int resolution = qBound(1, opts.resolution, 4096);
	const bool canBake = opts.bakeMaps && !opts.outputDir.isEmpty();

	EvalContext uniformCtx;
	uniformCtx.time = opts.time;

	auto unsupported = [&](const QString& socketName, const QString& what) {
		out.eval.unsupportedNodes.append(socketName + " <- " + what);
		logUnsupportedOnce(socketName, what);
	};

	QVector<SlotState> states(compiled.sockets.size());
	for (int i = 0; i < compiled.sockets.size(); ++i)
		states[i].cs = &compiled.sockets[i];

	using SocketClass = BakeProgram::SocketClass;

	// the base-color/alpha interplay (spec 1.3): a baked alpha chain packs
	// into baseColorMap.A and forces its synthesis
	SlotState* baseState = nullptr;
	SlotState* alphaState = nullptr;
	for (auto& state : states) {
		if (state.cs->slot.mapKey == "baseColorMap") baseState = &state;
		if (state.cs->slot.valueKey == "alpha" && state.cs->slot.mapKey.isEmpty()) alphaState = &state;
	}
	const bool alphaBaked = canBake && alphaState && alphaState->cs->connected
	                        && alphaState->cs->program.classification == SocketClass::Baked;

	// ---- land every socket ---------------------------------------------
	for (auto& state : states) {
		if (!state.cs->connected) continue;
		// A socket the shader-piece emitter took (HLMS_ADOPTION P5) is not
		// this baker's business any more: whatever it landed would be
		// overwritten in the pixel shader before a light was accumulated.
		if (opts.emittedSockets.contains(state.cs->slot.socketName)) continue;
		const MasterSlot& slot = state.cs->slot;
		const BakeProgram& program = state.cs->program;

		const bool isBaseWithBakedAlpha = (&state == baseState) && alphaBaked;

		switch (program.classification) {
		case SocketClass::Unconnected:
			break;
		case SocketClass::Unsupported:
			for (const auto& what : program.unsupportedNodes) unsupported(slot.socketName, what);
			break;
		case SocketClass::Passthrough:
			if (slot.mapKey.isEmpty()) {
				unsupported(slot.socketName, "texture");
			}
			else if (isBaseWithBakedAlpha) {
				state.needsBake = true; // RGB sampled from the source, A from the alpha chain
			}
			else {
				out.eval.values[slot.mapKey] = program.passthroughPath;
				out.passthrough[slot.mapKey] = program.passthroughPath;
			}
			break;
		case SocketClass::Baked:
			if (slot.target == MasterSlot::NoTarget || slot.socketName == "Alpha Cutoff") {
				unsupported(slot.socketName, rootTypeName(program));
			}
			else if (!canBake) {
				unsupported(slot.socketName, rootTypeName(program));
			}
			else {
				state.needsBake = true; // the Alpha slot's bake rides on baseColorMap
			}
			break;
		case SocketClass::Uniform: {
			if (isBaseWithBakedAlpha) {
				state.needsBake = true; // synthesized map, RGB = the folded color
				break;
			}
			const Value v = program.evaluate(uniformCtx);
			if (slot.target == MasterSlot::FloatSlot) {
				double s = v.x; // vecN -> float: leading component
				if (slot.invertToRoughness) {
					double gloss = s > 1.0 ? s / 100.0 : s;
					// FLOORED: see kLegacyGlossRoughnessFloor. A legacy gloss
					// of 1 used to land roughness 0, which is a specular
					// singularity under any normal map.
					s = qMax(1.0 - qBound(0.0, gloss, 1.0), kLegacyGlossRoughnessFloor);
				}
				// FloatSlots are 0-1 quantities on PbrMaterial (audit D6)
				out.eval.values[slot.valueKey] = qBound(0.0, s, 1.0);
			}
			else if (slot.target == MasterSlot::ColorSlot) {
				out.eval.values[slot.valueKey] = colorToJson(colorFromValue(v));
			}
			else {
				unsupported(slot.socketName, rootTypeName(program)); // Normal: map-only
			}
			break;
		}
		}

		if (program.classification == SocketClass::Uniform
		    || program.classification == SocketClass::Baked) {
			for (const auto& name : program.approximatedNodes)
				out.eval.approximatedNodes.append(slot.socketName + " <- " + name);
			out.eval.animated |= program.animated;
		}
	}

	// ---- factor interplay for maps (engine multiplies map x factor) ----
	auto applyMapFactorRules = [&](const QString& mapKey) {
		if (mapKey == "metallicMap") out.eval.values["metallic"] = 1.0;
		else if (mapKey == "roughnessMap") out.eval.values["roughness"] = 1.0;
		else if (mapKey == "emissiveMap") {
			out.eval.values["emissiveColor"] = colorToJson(QColor(Qt::white));
			out.eval.values["emissiveIntensity"] = 1.0;
		}
	};
	for (auto it = out.passthrough.begin(); it != out.passthrough.end(); ++it)
		applyMapFactorRules(it.key());

	// ---- bake ----------------------------------------------------------
	QSet<QString> keepFiles;
	bool bakedAnything = false;

	if (canBake) {
		QDir().mkpath(opts.outputDir);

		auto emitMap = [&](const QString& mapKey, const QString& fileName) {
			out.eval.values[mapKey] = opts.relativePrefix + fileName;
			out.maps[mapKey] = opts.relativePrefix + fileName;
			keepFiles.insert(fileName);
			bakedAnything = true;
		};

		// -- baseColorMap: base color chain RGB + alpha chain A ----------
		const bool baseNeeds = baseState && (baseState->needsBake
		                       || (alphaBaked && !baseState->cs->connected));
		if (baseNeeds || alphaBaked) {
			QByteArray recipe = "baseColorMap|";
			Value uniformBase(1.0, 1.0, 1.0); // white: material default RGB
			QImage srcImage;                  // passthrough source, if sampling
			enum { RgbUniform, RgbSampled, RgbEvaluated } rgbMode = RgbUniform;
			if (baseState && baseState->cs->connected) {
				switch (baseState->cs->program.classification) {
				case SocketClass::Baked:
					rgbMode = RgbEvaluated;
					recipe += baseState->cs->program.signature();
					break;
				case SocketClass::Passthrough: {
					rgbMode = RgbSampled;
					recipe += "src:" + baseState->cs->program.passthroughStamp.toUtf8();
					// the compiled carrier op usually holds the loaded image
					const auto& rootOp = baseState->cs->program.ops[baseState->cs->program.rootOp];
					srcImage = rootOp.image.isNull()
					               ? QImage(baseState->cs->program.passthroughPath)
					                     .convertToFormat(QImage::Format_RGBA8888)
					               : rootOp.image;
					break;
				}
				case SocketClass::Uniform: {
					uniformBase = baseState->cs->program.evaluate(uniformCtx);
					recipe += QStringLiteral("rgb:%1,%2,%3,%4|%5")
					              .arg(uniformBase.x).arg(uniformBase.y)
					              .arg(uniformBase.z).arg(uniformBase.w)
					              .arg(uniformBase.arity).toUtf8();
					break;
				}
				default:
					break;
				}
			}
			if (alphaBaked) recipe += "|alpha:" + alphaState->cs->program.signature();
			recipe += "|" + QByteArray::number(resolution) + "|" + QByteArray::number(opts.time);

			const QString fileName = QStringLiteral("baseColorMap-%1.png").arg(hash16(recipe));
			const QString filePath = opts.outputDir + "/" + fileName;
			if (!QFileInfo::exists(filePath)) {
				QImage image(resolution, resolution, QImage::Format_RGBA8888);
				// rows are independent (spec section 2: scanline-parallel);
				// each worker keeps its own scratch buffer
				QVector<int> rows(resolution);
				std::iota(rows.begin(), rows.end(), 0);
				// bits()/bytesPerLine() ONCE, outside the parallel map:
				// QImage::scanLine() is the non-const overload and calls
				// detach() — every worker thread poking the same QImageData
				// concurrently. The rows themselves are independent, so a base
				// pointer plus a stride is both correct and race-free.
				uchar* const base = image.bits();
				const qsizetype stride = image.bytesPerLine();
				QtConcurrent::blockingMap(rows, [&](int y) {
					QVarLengthArray<materials::Value, 64> scratch;
					uchar* line = base + y * stride;
					EvalContext ctx = uniformCtx;
					ctx.v = (y + 0.5) / resolution;
					for (int x = 0; x < resolution; ++x) {
						ctx.u = (x + 0.5) / resolution;
						uchar* p = line + 4 * x;
						if (rgbMode == RgbEvaluated)
							valueToRgb(baseState->cs->program.evaluate(ctx, scratch), p);
						else if (rgbMode == RgbSampled && !srcImage.isNull())
							valueToRgb(BakeProgram::sampleImage(srcImage, ctx.u, ctx.v), p);
						else
							valueToRgb(uniformBase, p);
						p[3] = alphaBaked ? toByte(alphaState->cs->program.evaluate(ctx, scratch).x) : 255;
					}
				});
				FileWrite::writeFileAtomic(filePath, [&](QFile &f) { return image.save(&f, "PNG"); });
			}
			emitMap("baseColorMap", fileName);
			// the map holds the color; leaving baseColor set would multiply
			// it in twice (material default is white)
			out.eval.values.remove("baseColor");
			out.passthrough.remove("baseColorMap");
		}

		// -- the other baked slots ---------------------------------------
		for (auto& state : states) {
			if (!state.needsBake || &state == baseState || &state == alphaState) continue;
			const MasterSlot& slot = state.cs->slot;

			QByteArray recipe = slot.mapKey.toUtf8() + "|" + state.cs->program.signature()
			                    + "|" + QByteArray::number(resolution)
			                    + "|" + QByteArray::number(opts.time);
			const QString fileName = QStringLiteral("%1-%2.png").arg(slot.mapKey, hash16(recipe));
			const QString filePath = opts.outputDir + "/" + fileName;
			if (!QFileInfo::exists(filePath)) {
				QImage image(resolution, resolution, QImage::Format_RGBA8888);
				QVector<int> rows(resolution);
				std::iota(rows.begin(), rows.end(), 0);
				// See the base-colour bake above: scanLine() detaches, so the
				// base pointer and stride are read once, on this thread.
				uchar* const base = image.bits();
				const qsizetype stride = image.bytesPerLine();
				QtConcurrent::blockingMap(rows, [&](int y) {
					QVarLengthArray<materials::Value, 64> scratch;
					uchar* line = base + y * stride;
					EvalContext ctx = uniformCtx;
					ctx.v = (y + 0.5) / resolution;
					for (int x = 0; x < resolution; ++x) {
						ctx.u = (x + 0.5) / resolution;
						const Value v = state.cs->program.evaluate(ctx, scratch);
						uchar* p = line + 4 * x;
						if (slot.target == MasterSlot::FloatSlot) {
							// grayscale written to RGB (spec 1.3)
							const uchar g = toByte(v.x);
							p[0] = g; p[1] = g; p[2] = g; p[3] = 255;
						}
						else {
							// ColorSlot (emissive) and NormalSlot: the raw
							// flowing RGB written verbatim, clamped (spec 1.5)
							valueToRgb(v, p);
							p[3] = 255;
						}
					}
				});
				FileWrite::writeFileAtomic(filePath, [&](QFile &f) { return image.save(&f, "PNG"); });
			}
			emitMap(slot.mapKey, fileName);
			applyMapFactorRules(slot.mapKey);
		}

		// -- prune stale files for this material -------------------------
		if (opts.pruneStale && bakedAnything) {
			const auto entries = QDir(opts.outputDir).entryList({ "*.png" }, QDir::Files);
			for (const auto& entry : entries)
				if (!keepFiles.contains(entry)) QFile::remove(opts.outputDir + "/" + entry);
		}
	}

	// ---- the folded UV transform ---------------------------------------
	// It lands as ordinary material values, so it reaches the renderer through
	// the same generic key loop every other folded value uses
	// (PbrGraphEvaluator::materialFromValues -> PbrMaterial::setValue) and the
	// glTF exporter through the same fields. `textureScale` is written as a
	// two-element array; a plain number is still read everywhere, which is what
	// keeps every material written before this loadable.
	if (compiled.uvFold.valid) {
		const auto& f = compiled.uvFold;
		const bool identity = f.scaleX == 1.0 && f.scaleY == 1.0 && f.offsetX == 0.0
		                      && f.offsetY == 0.0 && f.rotationDeg == 0.0;
		if (!identity) {
			QJsonArray scale; scale.append(f.scaleX); scale.append(f.scaleY);
			out.eval.values["textureScale"] = scale;
			if (f.offsetX != 0.0 || f.offsetY != 0.0) {
				QJsonArray offset; offset.append(f.offsetX); offset.append(f.offsetY);
				out.eval.values["textureOffset"] = offset;
			}
			if (f.rotationDeg != 0.0)
				out.eval.values["textureRotation"] = f.rotationDeg;
		}
	}

	// ---- alpha-mode rules ----------------------------------------------
	// A connected cutoff means cutout transparency; a varying alpha with no
	// cutoff needs blend mode so the baked A channel actually renders.
	if (out.eval.values.contains("alphaCutoff") && out.eval.values["alphaCutoff"].toDouble() > 0.0)
		out.eval.values["alphaMode"] = 1;
	else if (alphaBaked)
		out.eval.values["alphaMode"] = 2;

	// ---- material-settings blend mode ----------------------------------
	// The master's Blend Mode setting is material STATE, not texel math: the
	// bakes above are untouched, only the landed alphaMode changes. Opaque
	// (the default) keeps the auto rules so existing graphs behave as before;
	// an explicit choice overrides them (PbrMaterial alphaMode values:
	// 1 masked, 2 translucent, 4 additive, 5 modulate — 3 is Glass, not a
	// graph blend mode).
	switch (compiled.blendMode) {
	case BlendMode::Opaque:                                       break;
	case BlendMode::Masked:      out.eval.values["alphaMode"] = 1; break;
	case BlendMode::Translucent: out.eval.values["alphaMode"] = 2; break;
	case BlendMode::Additive:    out.eval.values["alphaMode"] = 4; break;
	case BlendMode::Modulate:    out.eval.values["alphaMode"] = 5; break;
	}

	if (!out.eval.unsupportedNodes.isEmpty()) {
		qWarning() << "GraphBaker: unsupported inputs on" << compiled.name
		           << "- material defaults used for:" << out.eval.unsupportedNodes.join(", ");
	}

	out.msElapsed = timer.elapsed();
	return out;
}

} // namespace materials
