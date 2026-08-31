/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "graphbaker.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
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
			{ "Occlusion", MasterSlot::FloatSlot, "occlusionFactor", "occlusionMap" },
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

// One compiled master socket.
struct SlotState
{
	const MasterSlot* slot = nullptr;
	bool connected = false;
	BakeProgram program;
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

	auto master = graph->getMasterNode();
	for (const auto& slot : masterSlotsFor(master->typeName)) {
		auto sock = findMasterInSocket(master, slot.socketName);
		if (!sock) continue;
		if (!sock->hasConnection()) {
			perSocket[slot.socketName] = "unconnected";
			continue;
		}

		auto program = BakeProgram::compile(sock, resolver);
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
	return out;
}

// --------------------------------------------------------------------- run

GraphBaker::Result GraphBaker::run(NodeGraph* graph, const Options& opts,
                                   BakeProgram::TextureResolver resolver)
{
	Result out;
	QElapsedTimer timer;
	timer.start();

	if (!graph) return out;
	auto master = graph->getMasterNode();
	if (!master) return out;
	out.eval.hasPbrMaster = (master->typeName == "PbrMaterial");

	if (!resolver) resolver = [](const QString& value) { return value; };

	const int resolution = qBound(1, opts.resolution, 4096);
	const bool canBake = opts.bakeMaps && !opts.outputDir.isEmpty();

	EvalContext uniformCtx;
	uniformCtx.time = opts.time;

	auto unsupported = [&](const QString& socketName, const QString& what) {
		out.eval.unsupportedNodes.append(socketName + " <- " + what);
	};

	// ---- compile every connected master socket -------------------------
	const auto slotTable = masterSlotsFor(master->typeName);
	QVector<SlotState> states(slotTable.size());
	for (int i = 0; i < slotTable.size(); ++i) {
		states[i].slot = &slotTable[i];
		auto sock = findMasterInSocket(master, slotTable[i].socketName);
		states[i].connected = sock && sock->hasConnection();
		if (states[i].connected)
			states[i].program = BakeProgram::compile(sock, resolver);
	}

	using SocketClass = BakeProgram::SocketClass;

	// the base-color/alpha interplay (spec 1.3): a baked alpha chain packs
	// into baseColorMap.A and forces its synthesis
	SlotState* baseState = nullptr;
	SlotState* alphaState = nullptr;
	for (auto& state : states) {
		if (state.slot->mapKey == "baseColorMap") baseState = &state;
		if (state.slot->valueKey == "alpha" && state.slot->mapKey.isEmpty()) alphaState = &state;
	}
	const bool alphaBaked = canBake && alphaState && alphaState->connected
	                        && alphaState->program.classification == SocketClass::Baked;

	// ---- land every socket ---------------------------------------------
	for (auto& state : states) {
		if (!state.connected) continue;
		const MasterSlot& slot = *state.slot;
		BakeProgram& program = state.program;

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
					s = 1.0 - qBound(0.0, gloss, 1.0);
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
		else if (mapKey == "occlusionMap") out.eval.values["occlusionFactor"] = 1.0;
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
		                       || (alphaBaked && !baseState->connected));
		if (baseNeeds || alphaBaked) {
			QByteArray recipe = "baseColorMap|";
			Value uniformBase(1.0, 1.0, 1.0); // white: material default RGB
			QImage srcImage;                  // passthrough source, if sampling
			enum { RgbUniform, RgbSampled, RgbEvaluated } rgbMode = RgbUniform;
			if (baseState && baseState->connected) {
				switch (baseState->program.classification) {
				case SocketClass::Baked:
					rgbMode = RgbEvaluated;
					recipe += baseState->program.signature();
					break;
				case SocketClass::Passthrough: {
					rgbMode = RgbSampled;
					recipe += "src:" + baseState->program.passthroughStamp.toUtf8();
					// the compiled carrier op usually holds the loaded image
					const auto& rootOp = baseState->program.ops[baseState->program.rootOp];
					srcImage = rootOp.image.isNull()
					               ? QImage(baseState->program.passthroughPath)
					                     .convertToFormat(QImage::Format_RGBA8888)
					               : rootOp.image;
					break;
				}
				case SocketClass::Uniform: {
					uniformBase = baseState->program.evaluate(uniformCtx);
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
			if (alphaBaked) recipe += "|alpha:" + alphaState->program.signature();
			recipe += "|" + QByteArray::number(resolution) + "|" + QByteArray::number(opts.time);

			const QString fileName = QStringLiteral("baseColorMap-%1.png").arg(hash16(recipe));
			const QString filePath = opts.outputDir + "/" + fileName;
			if (!QFileInfo::exists(filePath)) {
				QImage image(resolution, resolution, QImage::Format_RGBA8888);
				// rows are independent (spec section 2: scanline-parallel);
				// each worker keeps its own scratch buffer
				QVector<int> rows(resolution);
				std::iota(rows.begin(), rows.end(), 0);
				QtConcurrent::blockingMap(rows, [&](int y) {
					QVarLengthArray<materials::Value, 64> scratch;
					uchar* line = image.scanLine(y);
					EvalContext ctx = uniformCtx;
					ctx.v = (y + 0.5) / resolution;
					for (int x = 0; x < resolution; ++x) {
						ctx.u = (x + 0.5) / resolution;
						uchar* p = line + 4 * x;
						if (rgbMode == RgbEvaluated)
							valueToRgb(baseState->program.evaluate(ctx, scratch), p);
						else if (rgbMode == RgbSampled && !srcImage.isNull())
							valueToRgb(BakeProgram::sampleImage(srcImage, ctx.u, ctx.v), p);
						else
							valueToRgb(uniformBase, p);
						p[3] = alphaBaked ? toByte(alphaState->program.evaluate(ctx, scratch).x) : 255;
					}
				});
				image.save(filePath, "PNG");
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
			const MasterSlot& slot = *state.slot;

			QByteArray recipe = slot.mapKey.toUtf8() + "|" + state.program.signature()
			                    + "|" + QByteArray::number(resolution)
			                    + "|" + QByteArray::number(opts.time);
			const QString fileName = QStringLiteral("%1-%2.png").arg(slot.mapKey, hash16(recipe));
			const QString filePath = opts.outputDir + "/" + fileName;
			if (!QFileInfo::exists(filePath)) {
				QImage image(resolution, resolution, QImage::Format_RGBA8888);
				QVector<int> rows(resolution);
				std::iota(rows.begin(), rows.end(), 0);
				QtConcurrent::blockingMap(rows, [&](int y) {
					QVarLengthArray<materials::Value, 64> scratch;
					uchar* line = image.scanLine(y);
					EvalContext ctx = uniformCtx;
					ctx.v = (y + 0.5) / resolution;
					for (int x = 0; x < resolution; ++x) {
						ctx.u = (x + 0.5) / resolution;
						const Value v = state.program.evaluate(ctx, scratch);
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
				image.save(filePath, "PNG");
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

	// ---- alpha-mode rules ----------------------------------------------
	// A connected cutoff means cutout transparency; a varying alpha with no
	// cutoff needs blend mode so the baked A channel actually renders.
	if (out.eval.values.contains("alphaCutoff") && out.eval.values["alphaCutoff"].toDouble() > 0.0)
		out.eval.values["alphaMode"] = 1;
	else if (alphaBaked)
		out.eval.values["alphaMode"] = 2;

	if (!out.eval.unsupportedNodes.isEmpty()) {
		qWarning() << "GraphBaker: unsupported inputs on" << graph->settings.name
		           << "- material defaults used for:" << out.eval.unsupportedNodes.join(", ");
	}

	out.msElapsed = timer.elapsed();
	return out;
}

} // namespace materials
