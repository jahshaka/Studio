/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "bakeprogram.h"

#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QJsonValue>
#include <QSet>
#include <cmath>

#include "../graph/nodegraph.h"
#include "../models/connectionmodel.h"
#include "../models/nodemodel.h"
#include "../models/properties.h"
#include "../models/socketmodel.h"
#include "../nodes/test.h" // TextureNode

namespace materials {

// ---------------------------------------------------------------- coercion

Value Value::coerced(int toArity) const
{
	if (toArity <= 0 || toArity == arity) return *this;

	Value out;
	out.arity = toArity;
	if (arity == 1) {
		// float -> vecN: splat (GLSL vecN(f))
		for (int i = 0; i < toArity; ++i) out.setComponent(i, x);
		return out;
	}
	if (arity > toArity) {
		// shrink: leading components (v.xy)
		for (int i = 0; i < toArity; ++i) out.setComponent(i, component(i));
		return out;
	}
	// grow: repeat the last component (v.xyyy)
	for (int i = 0; i < toArity; ++i)
		out.setComponent(i, component(qMin(i, arity - 1)));
	return out;
}

// ---------------------------------------------------------------- helpers

namespace {

int arityOfSocket(const QString& typeName)
{
	if (typeName == "float") return 1;
	if (typeName == "vec2") return 2;
	if (typeName == "vec3") return 3;
	if (typeName == "vec4") return 4;
	return 0; // texture
}

// Parses a GLSL default-value literal ("0.0f", "1.0", "vec3(0.0, 0.0, 1.0)").
Value parseGlslLiteral(QString text)
{
	text = text.trimmed();
	auto parseScalar = [](QString s) -> double {
		s = s.trimmed();
		if (s.endsWith('f', Qt::CaseInsensitive)) s.chop(1);
		bool ok = false;
		double v = s.toDouble(&ok);
		return ok ? v : 0.0;
	};

	if (text.startsWith("vec")) {
		const int open = text.indexOf('(');
		const int close = text.lastIndexOf(')');
		const int n = text.mid(3, open - 3).toInt();
		Value out;
		out.arity = qBound(1, n, 4);
		if (open >= 0 && close > open) {
			const auto parts = text.mid(open + 1, close - open - 1).split(',');
			for (int i = 0; i < out.arity; ++i)
				out.setComponent(i, parseScalar(parts.value(qMin(i, parts.size() - 1))));
		}
		return out;
	}
	return Value(parseScalar(text));
}

// GLSL builtins, componentwise over the (already coerced) inputs.
Value cw1(const Value& a, double (*f)(double))
{
	Value out;
	out.arity = a.arity;
	for (int i = 0; i < a.arity; ++i) out.setComponent(i, f(a.component(i)));
	return out;
}

Value cw2(const Value& a, const Value& b, double (*f)(double, double))
{
	Value out;
	out.arity = a.arity;
	for (int i = 0; i < a.arity; ++i) out.setComponent(i, f(a.component(i), b.component(i)));
	return out;
}

double glslDot(const Value& a, const Value& b)
{
	double sum = 0;
	for (int i = 0; i < a.arity; ++i) sum += a.component(i) * b.component(i);
	return sum;
}

Value glslNormalize(const Value& v)
{
	const double len = std::sqrt(glslDot(v, v));
	Value out;
	out.arity = v.arity;
	for (int i = 0; i < v.arity; ++i) out.setComponent(i, v.component(i) / len);
	return out;
}

QString imageStampFor(const QString& path)
{
	QFileInfo info(path);
	return QStringLiteral("%1|%2|%3")
	    .arg(path)
	    .arg(info.exists() ? info.lastModified().toSecsSinceEpoch() : 0)
	    .arg(info.exists() ? info.size() : 0);
}

// ------------------------------------------------------------ op registry

using EvalFn = BakeOp::EvalFn;

Value evalLiteral(const BakeOp& op, const Value*, const EvalContext&)
{
	return op.literal;
}

const QHash<QString, EvalFn>& evalRegistry()
{
	static const QHash<QString, EvalFn> registry = [] {
		QHash<QString, EvalFn> r;

		// constants / folded properties carry their literal
		for (const auto& t : { "float", "color", "vector2", "vector3", "vector4", "property" })
			r[t] = evalLiteral;

		r["add"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw2(in[0], in[1], [](double a, double b) { return a + b; });
		};
		r["subtract"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw2(in[0], in[1], [](double a, double b) { return a - b; });
		};
		r["multiply"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw2(in[0], in[1], [](double a, double b) { return a * b; });
		};
		r["vectorMultiply"] = r["multiply"];
		r["divide"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			// GLSL semantics: b == 0 -> inf, no silent epsilon (spec 1.2, D22 stays cosmetic)
			return cw2(in[0], in[1], [](double a, double b) { return a / b; });
		};
		r["power"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw2(in[0], in[1], [](double a, double b) { return std::pow(a, b); });
		};
		r["sqrt"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw1(in[0], [](double a) { return std::sqrt(a); });
		};
		r["min"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw2(in[0], in[1], [](double a, double b) { return a < b ? a : b; });
		};
		r["max"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw2(in[0], in[1], [](double a, double b) { return a > b ? a : b; });
		};
		r["abs"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw1(in[0], [](double a) { return std::fabs(a); });
		};
		r["sign"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw1(in[0], [](double a) { return double((a > 0) - (a < 0)); });
		};
		r["ceil"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw1(in[0], [](double a) { return std::ceil(a); });
		};
		r["floor"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw1(in[0], [](double a) { return std::floor(a); });
		};
		r["round"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw1(in[0], [](double a) { return std::round(a); });
		};
		r["trunc"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw1(in[0], [](double a) { return std::trunc(a); });
		};
		r["fraction"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw1(in[0], [](double a) { return a - std::floor(a); }); // GLSL fract
		};
		r["oneminus"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw1(in[0], [](double a) { return 1.0 - a; });
		};
		r["negate"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw1(in[0], [](double a) { return -a; });
		};
		r["sine"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return cw1(in[0], [](double a) { return std::sin(a); });
		};
		r["step"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			// inputs: Edge, Value; step(edge, x) = x < edge ? 0 : 1
			return cw2(in[0], in[1], [](double edge, double x) { return x < edge ? 0.0 : 1.0; });
		};
		r["smoothstep"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			// inputs: Edge1, Edge2, Value - GLSL Hermite
			Value out;
			out.arity = in[2].arity;
			for (int i = 0; i < out.arity; ++i) {
				const double e0 = in[0].component(i), e1 = in[1].component(i), x = in[2].component(i);
				double t = (x - e0) / (e1 - e0);
				t = t < 0 ? 0 : (t > 1 ? 1 : t);
				out.setComponent(i, t * t * (3.0 - 2.0 * t));
			}
			return out;
		};
		r["clamp"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			// inputs in socket order Min, Max, Value; post-D7: clamp(Value, Min, Max)
			Value out;
			out.arity = in[2].arity;
			for (int i = 0; i < out.arity; ++i) {
				const double lo = in[0].component(i), hi = in[1].component(i), x = in[2].component(i);
				out.setComponent(i, x < lo ? lo : (x > hi ? hi : x));
			}
			return out;
		};
		r["lerp"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			// mix(a, b, t): t is the float T socket
			const double t = in[2].x;
			Value out;
			out.arity = in[0].arity;
			for (int i = 0; i < out.arity; ++i)
				out.setComponent(i, in[0].component(i) * (1.0 - t) + in[1].component(i) * t);
			return out;
		};
		r["reflect"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			// inputs: Normal (0), Incident (1); GLSL reflect(I, N) = I - 2*dot(N,I)*N
			const Value& n = in[0];
			const Value& i = in[1];
			const double d = glslDot(n, i);
			Value out;
			out.arity = n.arity;
			for (int c = 0; c < out.arity; ++c)
				out.setComponent(c, i.component(c) - 2.0 * d * n.component(c));
			return out;
		};
		r["dot"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return Value(glslDot(in[0], in[1]));
		};
		r["length"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return Value(std::sqrt(glslDot(in[0], in[0])));
		};
		r["distance"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			const Value d = cw2(in[0], in[1], [](double a, double b) { return a - b; });
			return Value(std::sqrt(glslDot(d, d)));
		};
		r["normalize"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return glslNormalize(in[0]);
		};
		r["splitvector"] = [](const BakeOp& op, const Value* in, const EvalContext&) {
			return Value(in[0].component(qBound(0, op.outIndex, 3)));
		};
		r["composevector"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			// vec4(x, y, z, w) - post-D11 the out socket is vec4 too
			return Value(in[0].x, in[1].x, in[2].x, in[3].x);
		};
		r["makeColor"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return Value(in[0].x, in[1].x, in[2].x, 1.0);
		};
		r["texCoords"] = [](const BakeOp&, const Value*, const EvalContext& ctx) {
			// TexCoord0-3 all evaluate to the bake UV (single-UV bake, spec 1.2)
			return Value(ctx.u, ctx.v);
		};
		r["textureSampler"] = [](const BakeOp& op, const Value* in, const EvalContext&) {
			if (op.image.isNull()) return Value(0.0, 0.0, 0.0, 0.0); // GLSL-documented: unconnected -> vec4(0)
			return BakeProgram::sampleImage(op.image, in[0].x, in[0].y);
		};
		r["texture"] = [](const BakeOp& op, const Value* in, const EvalContext&) {
			// Out 0 with nothing on its UV input is a REFERENCE: consumers read
			// op.image and the master binds it directly (Passthrough).
			if (op.isTextureCarrier) return Value(0.0, 0.0, 0.0, 0.0);
			// Otherwise the node samples itself (D-2 option B1): out 1 RGBA, or
			// out 0 with a UV connected. Same GLSL-documented empty-slot value
			// as textureSampler.
			if (op.image.isNull()) return Value(0.0, 0.0, 0.0, 0.0);
			return BakeProgram::sampleImage(op.image, in[0].x, in[0].y);
		};
		r["texelsize"] = [](const BakeOp& op, const Value*, const EvalContext&) {
			const double w = op.image.isNull() ? 0.0 : op.image.width();
			const double h = op.image.isNull() ? 0.0 : op.image.height();
			switch (op.outIndex) {
			case 0: return Value(w, h);
			case 1: return Value(w);
			case 2: return Value(h);
			case 3: return Value(w > 0 ? 1.0 / w : 0.0); // unconnected -> 0, not inf
			case 4: return Value(h > 0 ? 1.0 / h : 0.0);
			// The appended aspect outs (option C.2); no texture -> 0 like the
			// reciprocals above.
			case 5: return Value(h > 0 ? w / h : 0.0);   // Aspect (W/H)
			default: return Value(w > 0 ? h / w : 0.0);  // 1/Aspect
			}
		};
		// THE UV NODE (MATERIAL_UV_NODES_SPEC D-3). Inputs: UV, Tiling, Offset,
		// Rotation(degrees). The formula is
		//     uv' = R(theta) * ((uv * s + o) - 0.5) + 0.5
		// rotating about the texture centre (Unreal's CustomRotator default),
		// positive = counter-clockwise in UV space. THE RENDER-TIME FOLD
		// COMPUTES THE SAME EXPRESSION (OgreMaterials userValue + the
		// custom_ps_uv_modifier_macros piece), which is what lets the baker
		// choose either route without the picture changing.
		r["uv"] = [](const BakeOp& op, const Value* in, const EvalContext&) {
			const double x = in[0].x * in[1].x + in[2].x;
			const double y = in[0].y * in[1].y + in[2].y;
			const double deg = in[3].x;
			// A LITERAL zero rotation is the overwhelmingly common case AND the
			// one that must stay bit-identical to the old uv*tiling+offset
			// node: skip the centre round-trip rather than trust (a-0.5)+0.5.
			// The flag, not `deg == 0`, because the GLSL emitter can only
			// decide at compile time and the two backends must decide alike.
			if (op.uvRotationIsZero) return Value(x, y);
			const double kPi = 3.14159265358979323846;
			const double th = deg * kPi / 180.0;
			const double c = std::cos(th), sn = std::sin(th);
			const double tx = x - 0.5, ty = y - 0.5;
			return Value(c * tx - sn * ty + 0.5, sn * tx + c * ty + 0.5);
		};
		// The two typeNames "uv" absorbed. Nothing constructs them any more
		// (the library aliases them and NodeGraph::deserialize renames on
		// load); kept so a program compiled from a hand-built node with the
		// old typeName still evaluates instead of silently reading zero.
		r["uvTransform"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			return Value(in[0].x * in[1].x + in[2].x,
			             in[0].y * in[1].y + in[2].y);
		};
		r["panner"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			// uv + speed * time
			const double t = in[2].x;
			return Value(in[0].x + in[1].x * t, in[0].y + in[1].y * t);
		};
		r["flipbook"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			// post-D10 corrected row/col math (nodes/texture.cpp flipbook())
			const double rows = in[1].x, columns = in[2].x, animlength = in[3].x, time = in[4].x;
			const double totalFrames = rows * columns;
			const double frameWidth = 1.0 / columns;
			const double frameHeight = 1.0 / rows;
			const double timePerFrame = animlength / totalFrames;
			const double currentFrame = std::floor(std::fmod(time / timePerFrame, totalFrames));
			const double animCol = std::fmod(currentFrame, columns);
			const double animRow = rows - std::floor(currentFrame / columns) - 1.0;
			return Value(animCol * frameWidth + in[0].x * frameWidth,
			             animRow * frameHeight + in[0].y * frameHeight);
		};
		r["normalintensity"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			// normalize(mix(vec3(0,0,1), N, intensity))
			const double i = in[1].x;
			Value mixed(0.0 * (1.0 - i) + in[0].x * i,
			            0.0 * (1.0 - i) + in[0].y * i,
			            1.0 * (1.0 - i) + in[0].z * i);
			return glslNormalize(mixed);
		};
		r["combinenormals"] = [](const BakeOp&, const Value* in, const EvalContext&) {
			// normalize(a + b) on the raw flowing values (spec 1.5 - no hidden decode)
			return glslNormalize(cw2(in[0], in[1], [](double a, double b) { return a + b; }));
		};
		r["worldNormal"] = [](const BakeOp&, const Value*, const EvalContext& ctx) {
			return Value(ctx.normalX, ctx.normalY, ctx.normalZ); // tangent-space identity
		};
		r["localNormal"] = r["worldNormal"];
		r["fresnel"] = [](const BakeOp& op, const Value* in, const EvalContext& ctx) {
			// pow(1 - max(0, dot(N, view)), power); Normal input honored (D17 intent)
			const double d = in[0].x * ctx.viewX + in[0].y * ctx.viewY + in[0].z * ctx.viewZ;
			const double f = std::pow(1.0 - (d > 0.0 ? d : 0.0), in[1].x);
			return Value(f, f, f, f);
		};
		r["depth"] = [](const BakeOp&, const Value*, const EvalContext&) {
			// near-plane convention of the legacy (1 - z/w) at z=0
			return Value(0.0);
		};
		r["time"] = [](const BakeOp&, const Value*, const EvalContext& ctx) {
			return Value(ctx.time);
		};
		r["pulsate"] = [](const BakeOp&, const Value* in, const EvalContext& ctx) {
			return Value(std::sin(ctx.time * in[0].x) * 0.5 + 0.5);
		};
		// texture property out 2: rgba.xyz * 2 - 1 (the node's GLSL decode)
		r["propertyNormalSample"] = [](const BakeOp& op, const Value* in, const EvalContext&) {
			const Value rgba = op.image.isNull()
			                       ? Value(0.0, 0.0, 0.0, 0.0)
			                       : BakeProgram::sampleImage(op.image, in[0].x, in[0].y);
			return Value(rgba.x * 2.0 - 1.0, rgba.y * 2.0 - 1.0, rgba.z * 2.0 - 1.0);
		};
		return r;
	}();
	return registry;
}

// ------------------------------------------------------------- the compiler

// Node types whose op reads a texture through op.image; value is per the
// registry. property(texture) is handled separately.
bool nodeIsApproximated(const QString& t)
{
	return t == "worldNormal" || t == "localNormal" || t == "fresnel" || t == "depth";
}
bool nodeIsAnimated(const QString& t) { return t == "time" || t == "pulsate"; }
bool nodeIsVarying(const QString& t) { return t == "texCoords"; }

struct Compiler
{
	BakeProgram program;
	const BakeProgram::TextureResolver& resolve;
	QHash<QString, int> memo; // "nodeId:outIndex" -> op index
	QSet<QString> approximated;
	QSet<QString> unsupported;

	explicit Compiler(const BakeProgram::TextureResolver& r) : resolve(r) {}

	void markUnsupported(BakeOp& op, const QString& what)
	{
		op.unsupportedReason = what;
		unsupported.insert(what);
	}

	// Resolves the texture feeding `sock` (a texture-typed input) into an
	// image + stamp on the consuming op. Returns false when nothing is
	// connected or the source has no file.
	bool resolveTextureInput(SocketModel* sock, BakeOp& op)
	{
		if (!sock || !sock->hasConnection()) return false;
		auto source = sock->getConnection()->leftSocket->getNode();
		if (!source) return false;

		QString stored;
		if (source->typeName == "texture") {
			stored = static_cast<TextureNode*>(source)->getTexturePath();
		}
		if (stored.isEmpty()) return false;

		const QString path = resolve ? resolve(stored) : stored;
		if (path.isEmpty()) return false;

		QImage image(path);
		if (image.isNull()) return false;
		op.image = image.convertToFormat(QImage::Format_RGBA8888);
		op.imagePath = path;
		op.imageStamp = imageStampFor(path);
		return true;
	}

	BakeInputRef makeRef(BakeOp& op, NodeModel* node, int inIndex)
	{
		BakeInputRef ref;
		auto sock = node->inSockets[inIndex];
		ref.arity = arityOfSocket(sock->typeName);

		if (sock->hasConnection()) {
			ref.op = compileOutput(sock->getConnection()->leftSocket);
			if (ref.op >= 0) {
				const auto& src = program.ops[ref.op];
				op.varying |= src.varying;
				op.animated |= src.animated;
				op.approximated |= src.approximated;
				if (op.unsupportedReason.isEmpty() && !src.unsupportedReason.isEmpty())
					op.unsupportedReason = src.unsupportedReason;
			}
			return ref;
		}

		// unconnected: the socket's GLSL default is the value
		const QString def = sock->getValue().trimmed();
		if (def.startsWith("v_texCoord")) {
			ref.fallbackKind = BakeInputRef::Uv;
			op.varying = true;
		}
		else if (def == "u_time") {
			ref.fallbackKind = BakeInputRef::Time;
			op.animated = true;
		}
		else {
			ref.fallback = parseGlslLiteral(def).coerced(ref.arity == 0 ? 1 : ref.arity);
		}
		return ref;
	}

	// Resolves the literal value behind one input ref, if there is one.
	bool literalInput(const BakeOp& op, int index, Value& out) const
	{
		if (index >= op.inputs.size()) return false;
		const auto& ref = op.inputs[index];
		if (ref.op < 0) {
			if (ref.fallbackKind != BakeInputRef::Literal) return false;
			out = ref.fallback;
			return true;
		}
		const BakeOp& src = program.ops[ref.op];
		if (!src.hasLiteral) return false;
		out = src.literal.coerced(ref.arity);
		return true;
	}

	// Records the `uv` op's transform when it is KNOWABLE without evaluating:
	// the UV input is the bake UV and Tiling/Offset/Rotation are literal (the
	// inline editors' socket defaults, or a constant node). That is the exact
	// shape the render-time fold can carry (MATERIAL_UV_NODES_SPEC §3.2), and
	// the identity case is what keeps `texture -> uv(defaults) -> sampler`
	// Passthrough — a bare coordinate node, which is what `texCoords` was.
	//
	// A CHAIN of two uv nodes is deliberately NOT composed: two rotations
	// compose into a transform whose offset is not either node's, and getting
	// that subtly wrong is worse than baking. Chains bake.
	void recordUvTransform(BakeOp& op, NodeModel* node)
	{
		// Read through the SERIALIZED widget value, not a downcast: the
		// evaluator slice is compiled into test targets that do not include
		// nodes/texture.cpp, and a dynamic_cast to UVNode there is an
		// undefined typeinfo at link time. The widget JSON is the node's own
		// contract and is what the file carries anyway.
		op.uvSet = qBound(0, node->serializeWidgetValue().toObject()["uvSet"].toInt(0), 3);
		if (op.uvSet > 0) {
			// I-7: every UV set evaluates as UV0 until BAKE_LIGHTING phase 0
			// gives the document a second UV stream. Say so rather than render
			// a lie quietly.
			op.approximated = true;
			approximated.insert(QStringLiteral("uv (UV set %1 evaluates as UV 0)").arg(op.uvSet));
		}
		if (op.inputs.isEmpty()) return;
		Value rotationOnly;
		if (literalInput(op, 3, rotationOnly) && rotationOnly.x == 0.0)
			op.uvRotationIsZero = true;
		const auto& uvRef = op.inputs[0];
		if (uvRef.op >= 0 || uvRef.fallbackKind != BakeInputRef::Uv) return;
		Value tiling, offset, rotation;
		if (!literalInput(op, 1, tiling) || !literalInput(op, 2, offset)
		    || !literalInput(op, 3, rotation))
			return;
		op.uvOpKnown = true;
		op.uvScaleX = tiling.x;
		op.uvScaleY = tiling.y;
		op.uvOffsetX = offset.x;
		op.uvOffsetY = offset.y;
		op.uvRotationDeg = rotation.x;
	}

	int addOp(BakeOp& op)
	{
		program.ops.append(op);
		return program.ops.size() - 1;
	}

	// Compiles the op producing `leftSock` (an OUTPUT socket). Returns the op index.
	int compileOutput(SocketModel* leftSock)
	{
		auto node = leftSock->getNode();
		if (!node) return -1;
		const int outIndex = node->outSockets.indexOf(leftSock);
		const QString key = node->id + ":" + QString::number(outIndex);
		if (memo.contains(key)) return memo[key];

		BakeOp op;
		op.typeName = node->typeName;
		op.nodeId = node->id;
		op.outIndex = qMax(0, outIndex);
		const QString& type = node->typeName;

		if (type == "float") {
			op.literal = Value(node->serializeWidgetValue().toDouble());
			op.hasLiteral = true;
		}
		else if (type == "color") {
			const auto obj = node->serializeWidgetValue().toObject();
			const Value rgba(obj["r"].toDouble(), obj["g"].toDouble(),
			                 obj["b"].toDouble(), obj["a"].toDouble(1.0));
			// out 0 is RGBA; 1-4 are the R,G,B,A channels
			op.literal = op.outIndex == 0 ? rgba : Value(rgba.component(op.outIndex - 1));
			op.hasLiteral = true;
		}
		else if (type == "vector2" || type == "vector3" || type == "vector4") {
			const auto obj = node->serializeWidgetValue().toObject();
			if (type == "vector2")
				op.literal = Value(obj["x"].toDouble(), obj["y"].toDouble());
			else if (type == "vector3")
				op.literal = Value(obj["x"].toDouble(), obj["y"].toDouble(), obj["z"].toDouble());
			else
				op.literal = Value(obj["x"].toDouble(), obj["y"].toDouble(),
				                   obj["z"].toDouble(), obj["w"].toDouble());
			op.hasLiteral = true;
		}
		else if (type == "texture") {
			auto texNode = static_cast<TextureNode*>(node);
			const QString stored = texNode->getTexturePath();
			const QString path = (resolve && !stored.isEmpty()) ? resolve(stored) : stored;
			if (!path.isEmpty()) {
				op.imagePath = path;
				op.imageStamp = imageStampFor(path);
				QImage image(path);
				if (!image.isNull())
					op.image = image.convertToFormat(QImage::Format_RGBA8888);
			}
			// THE TEXTURE NODE IS A SAMPLER TOO (D-2 option B1). Two ops share
			// one node, told apart by what is asked of it:
			//   out 1 (RGBA)            -> always a sample at the UV input
			//   out 0 with UV connected -> a sample as well: a texture
			//                              REFERENCE cannot carry UV math into
			//                              a map binding, so the chain has to
			//                              resample (or fold, §3.2)
			//   out 0, UV unconnected   -> the carrier it has always been, so
			//                              every shipped .effect and
			//                              materials.createFromImage keep their
			//                              Passthrough classification exactly
			const bool uvConnected = !node->inSockets.isEmpty()
			                         && node->inSockets[0]->hasConnection();
			if (op.outIndex == 1 || uvConnected) {
				op.inputs.append(makeRef(op, node, 0)); // UV (defaults to the bake UV)
				// no image -> vec4(0) everywhere; a constant is not varying no
				// matter what feeds the UV (same rule as textureSampler)
				if (op.image.isNull() && op.unsupportedReason.isEmpty()) op.varying = false;
			}
			else {
				op.isTextureCarrier = true;
			}
		}
		else if (type == "textureSampler") {
			const bool hasImage = resolveTextureInput(node->inSockets[0], op);
			op.inputs.append(makeRef(op, node, 1)); // UV (defaults to the bake UV)
			// no texture -> vec4(0) everywhere (GLSL-documented); a constant
			// is not varying no matter what feeds the UV
			if (!hasImage && op.unsupportedReason.isEmpty()) op.varying = false;
		}
		else if (type == "texelsize") {
			if (!resolveTextureInput(node->inSockets[0], op))
				qWarning() << "BakeProgram: texelsize with no texture evaluates to 0";
		}
		else if (type == "fresnel") {
			// unconnected Normal means the context normal, not the socket's
			// vec4(0) default - that is what makes fresnel fold to 0 under
			// the fake context (spec 1.4)
			auto normalSock = node->inSockets[0];
			if (normalSock->hasConnection()) {
				op.inputs.append(makeRef(op, node, 0));
			}
			else {
				BakeInputRef ref;
				ref.arity = 3;
				ref.fallback = Value(0.0, 0.0, 1.0); // == ctx.normal (identity)
				op.inputs.append(ref);
			}
			op.inputs.append(makeRef(op, node, 1)); // Power
		}
		else if (type == "uv") {
			for (int i = 0; i < node->inSockets.size(); ++i)
				op.inputs.append(makeRef(op, node, i));
			recordUvTransform(op, node);
		}
		else if (evalRegistry().contains(type)) {
			// generic op: every input socket in order becomes a ref
			for (int i = 0; i < node->inSockets.size(); ++i)
				op.inputs.append(makeRef(op, node, i));
		}
		else {
			markUnsupported(op, type);
		}

		op.varying |= nodeIsVarying(type);
		op.animated |= nodeIsAnimated(type);
		if (nodeIsApproximated(type) || nodeIsAnimated(type)) {
			op.approximated = true;
			approximated.insert(type);
		}

		// resolve the evaluator once - per-pixel hash lookups are too slow
		op.fn = evalRegistry().value(op.typeName, nullptr);

		const int index = addOp(op);
		memo[key] = index;
		return index;
	}
};

} // namespace

// ------------------------------------------------------------------ compile

BakeProgram BakeProgram::compile(SocketModel* masterInput, const TextureResolver& resolve)
{
	Compiler compiler(resolve);
	BakeProgram& program = compiler.program;

	if (!masterInput || !masterInput->hasConnection()) {
		program.classification = SocketClass::Unconnected;
		return program;
	}

	auto leftSock = masterInput->getConnection()->leftSocket;
	program.rootOp = compiler.compileOutput(leftSock);
	program.approximatedNodes = QStringList(compiler.approximated.begin(), compiler.approximated.end());
	program.approximatedNodes.sort();
	program.unsupportedNodes = QStringList(compiler.unsupported.begin(), compiler.unsupported.end());
	program.unsupportedNodes.sort();

	program.reclassify();
	return program;
}

// Recomputes the classification from the op list. Split out of compile()
// because applyUvFold() rewrites sampler UVs and the answer has to be asked
// again — that rewrite is the whole point of the fold.
void BakeProgram::reclassify()
{
	if (rootOp < 0) {
		classification = SocketClass::Unconnected;
		return;
	}
	passthroughPath.clear();
	passthroughStamp.clear();

	const BakeOp& root = ops[rootOp];
	animated = root.animated;

	if (!root.unsupportedReason.isEmpty()) {
		classification = SocketClass::Unsupported;
		return;
	}

	// Passthrough: a bare texture reference (texture node / texture property
	// out 0) feeding the master, or textureSampler(texture, bake UV) with no
	// math - bind the source image directly, no bake, no quality loss.
	if (root.isTextureCarrier) {
		if (root.imagePath.isEmpty()) {
			// an empty texture slot is a silent no-op, exactly as before
			classification = SocketClass::Unconnected;
			return;
		}
		classification = SocketClass::Passthrough;
		passthroughPath = root.imagePath;
		passthroughStamp = root.imageStamp;
		return;
	}
	// A SAMPLER ROOT over the bake UV is the same picture as binding the source
	// image, so it passes through too. Two node shapes reach here now: the
	// dedicated sampler, and the Texture node sampling itself (out 1, or out 0
	// with a UV connected — D-2 option B1).
	const bool samplerRoot = root.typeName == "textureSampler"
	                         || (root.typeName == "texture" && !root.isTextureCarrier);
	if (samplerRoot && !root.image.isNull() && root.inputs.size() == 1) {
		const auto& uvRef = root.inputs[0];
		const bool uvIsBakeUv =
		    (uvRef.op < 0 && uvRef.fallbackKind == BakeInputRef::Uv) ||
		    (uvRef.op >= 0 && (ops[uvRef.op].typeName == "texCoords"
		                       || isIdentityUvOp(ops[uvRef.op])));
		if (uvIsBakeUv) {
			classification = SocketClass::Passthrough;
			passthroughPath = root.imagePath;
			passthroughStamp = root.imageStamp;
			return;
		}
	}

	classification = root.varying ? SocketClass::Baked : SocketClass::Uniform;
}

void BakeProgram::applyUvFold()
{
	for (auto& op : ops) {
		const bool isSampler = op.typeName == "textureSampler"
		                       || (op.typeName == "texture" && !op.isTextureCarrier);
		if (!isSampler) continue;
		const int uvIndex = op.typeName == "textureSampler" ? 1 : 0;
		if (uvIndex >= op.inputs.size()) continue;
		BakeInputRef ref;
		ref.arity = 2;
		ref.fallbackKind = BakeInputRef::Uv;
		op.inputs[uvIndex] = ref;
	}
	// The `uv` ops are left in the list on purpose: they cost one evaluation of
	// nothing, and signature() still names them, which is exactly the cache-key
	// property I-6 asks for — a fold flip must not hit a stale baked map. The
	// rewritten refs make the signature differ from the unfolded program's.
	reclassify();
}

// ----------------------------------------------------------------- evaluate

Value BakeProgram::evaluate(const EvalContext& ctx) const
{
	QVarLengthArray<Value, 64> scratch;
	return evaluate(ctx, scratch);
}

Value BakeProgram::evaluate(const EvalContext& ctx, QVarLengthArray<Value, 64>& results) const
{
	results.resize(ops.size());
	Value inputs[8];
	for (int i = 0; i < ops.size(); ++i) {
		const BakeOp& op = ops[i];
		if (!op.unsupportedReason.isEmpty()) { results[i] = Value(0.0); continue; }
		if (op.hasLiteral) { results[i] = op.literal; continue; }

		const int inCount = qMin(op.inputs.size(), 8);
		for (int j = 0; j < inCount; ++j) {
			const auto& ref = op.inputs[j];
			Value v;
			if (ref.op >= 0) v = results[ref.op];
			else if (ref.fallbackKind == BakeInputRef::Uv) v = Value(ctx.u, ctx.v);
			else if (ref.fallbackKind == BakeInputRef::Time) v = Value(ctx.time);
			else v = ref.fallback;
			inputs[j] = ref.arity > 0 ? v.coerced(ref.arity) : v;
		}

		results[i] = op.fn ? op.fn(op, inputs, ctx) : Value(0.0);
	}
	return rootOp >= 0 ? results[rootOp] : Value(0.0);
}

// --------------------------------------------------------------- utilities

Value BakeProgram::sampleImage(const QImage& image, double u, double v)
{
	// bilinear, repeat wrap - GLSL texture() parity for the default sampler.
	// QImage row 0 is v=0 (the baker writes with the same convention, so
	// passthrough and baked outputs stay layout-identical).
	const int w = image.width(), h = image.height();
	if (w <= 0 || h <= 0) return Value(0.0, 0.0, 0.0, 0.0);

	const double px = u * w - 0.5;
	const double py = v * h - 0.5;
	const int x0 = int(std::floor(px));
	const int y0 = int(std::floor(py));
	const double fx = px - x0;
	const double fy = py - y0;

	auto wrap = [](int a, int n) { const int m = a % n; return m < 0 ? m + n : m; };
	const int xs[2] = { wrap(x0, w), wrap(x0 + 1, w) };
	const int ys[2] = { wrap(y0, h), wrap(y0 + 1, h) };

	double acc[4] = { 0, 0, 0, 0 };
	const double weights[2][2] = { { (1 - fx) * (1 - fy), fx * (1 - fy) },
	                               { (1 - fx) * fy, fx * fy } };
	for (int j = 0; j < 2; ++j) {
		const uchar* line = image.constScanLine(ys[j]);
		for (int i = 0; i < 2; ++i) {
			const uchar* p = line + 4 * xs[i];
			const double wgt = weights[j][i];
			acc[0] += wgt * p[0];
			acc[1] += wgt * p[1];
			acc[2] += wgt * p[2];
			acc[3] += wgt * p[3];
		}
	}
	return Value(acc[0] / 255.0, acc[1] / 255.0, acc[2] / 255.0, acc[3] / 255.0);
}

QByteArray BakeProgram::signature() const
{
	QString sig;
	sig += QStringLiteral("root:%1;class:%2;").arg(rootOp).arg(classToString(classification));
	if (!passthroughStamp.isEmpty()) sig += "pass:" + passthroughStamp + ";";
	for (const auto& op : ops) {
		sig += op.typeName + "#" + QString::number(op.outIndex);
		if (op.hasLiteral)
			sig += QStringLiteral("(%1,%2,%3,%4|%5)")
			           .arg(op.literal.x, 0, 'g', 17).arg(op.literal.y, 0, 'g', 17)
			           .arg(op.literal.z, 0, 'g', 17).arg(op.literal.w, 0, 'g', 17)
			           .arg(op.literal.arity);
		if (!op.imageStamp.isEmpty()) sig += "[" + op.imageStamp + "]";
		for (const auto& ref : op.inputs) {
			if (ref.op >= 0)
				sig += QStringLiteral("<%1").arg(ref.op);
			else if (ref.fallbackKind == BakeInputRef::Uv)
				sig += "<uv";
			else if (ref.fallbackKind == BakeInputRef::Time)
				sig += "<t";
			else
				sig += QStringLiteral("<(%1,%2,%3,%4|%5)")
				           .arg(ref.fallback.x, 0, 'g', 17).arg(ref.fallback.y, 0, 'g', 17)
				           .arg(ref.fallback.z, 0, 'g', 17).arg(ref.fallback.w, 0, 'g', 17)
				           .arg(ref.fallback.arity);
			sig += QStringLiteral(":%1").arg(ref.arity);
		}
		sig += ";";
	}
	return sig.toUtf8();
}

// ------------------------------------------------------------------- the fold

bool BakeProgram::isIdentityUvOp(const BakeOp& op)
{
	return op.uvOpKnown && op.uvScaleX == 1.0 && op.uvScaleY == 1.0
	       && op.uvOffsetX == 0.0 && op.uvOffsetY == 0.0 && op.uvRotationDeg == 0.0;
}

// THE FOLD DETECTOR (MATERIAL_UV_NODES_SPEC §3.2, decision D-1a).
//
// WHY IT EXISTS AT ALL. Baking a tiled texture RESAMPLES it: a 2048-square
// source tiled 4x into the default 1024-square bake keeps 256 px per tile — an
// 8x downsample of what the user gave us — while the render-time path samples
// the full source, mip-mapped, every tile. The whole point of a UV input is
// tiling, so the common case must not be the lossy one.
//
// FOLDABLE iff every sampler in this program reads its UV either from the bake
// UV through ONE `uv` node with a literal transform, or (the degenerate case)
// straight from the bake UV; all of them share the SAME transform; and no other
// op consumes a `uv` output (a `uv -> split -> Roughness` chain would see
// transformed UVs in the bake and untransformed ones at render time — two
// different pictures from one graph, which is the one outcome worth refusing).
BakeProgram::UvFold BakeProgram::uvFold() const
{
	UvFold fold;
	bool first = true;

	// Every op that reads a `uv` op's output, and how.
	QVector<bool> uvOpConsumedBySampler(ops.size(), false);
	QVector<bool> uvOpConsumedByOther(ops.size(), false);

	for (const auto& op : ops) {
		const bool isSampler = op.typeName == "textureSampler"
		                       || (op.typeName == "texture" && !op.isTextureCarrier);
		for (int i = 0; i < op.inputs.size(); ++i) {
			const int src = op.inputs[i].op;
			if (src < 0 || src >= ops.size()) continue;
			if (ops[src].typeName != "uv") continue;
			// a sampler's UV input is index 1 on textureSampler, 0 on texture
			const bool uvSlot = isSampler
			                    && i == (op.typeName == "textureSampler" ? 1 : 0);
			if (uvSlot) uvOpConsumedBySampler[src] = true;
			else uvOpConsumedByOther[src] = true;
		}
	}

	for (int i = 0; i < ops.size(); ++i) {
		if (ops[i].typeName != "uv") continue;
		if (!uvOpConsumedByOther[i]) continue;
		fold.reason = QStringLiteral("a UV node feeds something other than a texture, "
		                             "so the transform has to be evaluated per texel");
		return fold;
	}

	for (int i = 0; i < ops.size(); ++i) {
		const BakeOp& op = ops[i];
		const bool isSampler = op.typeName == "textureSampler"
		                       || (op.typeName == "texture" && !op.isTextureCarrier);
		if (!isSampler) continue;
		if (op.image.isNull()) continue; // an empty slot samples vec4(0); it tiles nothing
		const int uvIndex = op.typeName == "textureSampler" ? 1 : 0;
		if (uvIndex >= op.inputs.size()) continue;
		const auto& ref = op.inputs[uvIndex];

		double sx = 1, sy = 1, ox = 0, oy = 0, rot = 0;
		if (ref.op < 0) {
			if (ref.fallbackKind != BakeInputRef::Uv) {
				fold.reason = QStringLiteral("a texture samples a computed UV, not the mesh's");
				return fold;
			}
		}
		else {
			const BakeOp& src = ops[ref.op];
			if (src.typeName != "uv" || !src.uvOpKnown) {
				fold.reason = QStringLiteral("a texture's UV comes from math the material "
				                             "cannot carry (only one UV node with constant "
				                             "tiling/offset/rotation folds)");
				return fold;
			}
			sx = src.uvScaleX; sy = src.uvScaleY;
			ox = src.uvOffsetX; oy = src.uvOffsetY;
			rot = src.uvRotationDeg;
		}

		if (first) {
			fold.scaleX = sx; fold.scaleY = sy;
			fold.offsetX = ox; fold.offsetY = oy;
			fold.rotationDeg = rot;
			first = false;
		}
		else if (fold.scaleX != sx || fold.scaleY != sy || fold.offsetX != ox
		         || fold.offsetY != oy || fold.rotationDeg != rot) {
			fold.reason = QStringLiteral("two textures use different UV transforms; the "
			                             "material carries one, so these bake");
			return fold;
		}
		++fold.samplers;
	}

	fold.valid = !first;
	return fold;
}

QString BakeProgram::classToString(SocketClass c)
{
	switch (c) {
	case SocketClass::Unconnected: return "unconnected";
	case SocketClass::Uniform: return "uniform";
	case SocketClass::Passthrough: return "passthrough";
	case SocketClass::Baked: return "baked";
	case SocketClass::Unsupported: return "unsupported";
	}
	return "unconnected";
}

} // namespace materials
