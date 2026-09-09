/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "pieceemitter.h"

#include "services/filewriteatomic.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace materials {

namespace {

// The pass-buffer clock our Hlms library declares for piece-carrying materials
// (irisgl/engine/media/Hlms/Jahshaka/JahFog_piece_vs_piece_ps.any). Both shader
// stages see it; nothing else does.
const char *kClock = "passBuf.jahClock.x";

// ---------------------------------------------------------------- values
//
// EVERY intermediate is a float4 whose components at or above the value's
// logical ARITY are zero. That is not a convenience, it is what makes the two
// backends provably the same: the CPU's Value has exactly that invariant
// (`coerced` and the componentwise helpers build a fresh, zeroed Value and fill
// only [0, arity)), so an op that reads a component past its input's arity
// reads 0 on both sides. Emitting narrower GLSL types would have to reproduce
// that padding anyway, in more places.

QString num(double v)
{
	// 9 significant digits round-trips a float32 exactly; the GPU computes in
	// float32 whatever we write, and the parity oracle's tolerance covers the
	// difference from the CPU's doubles.
	QString s = QString::number(v, 'g', 9);
	if (!s.contains('.') && !s.contains('e') && !s.contains("inf") && !s.contains("nan"))
		s += QStringLiteral(".0");
	return s;
}

QString literal(const Value &v)
{
	return QStringLiteral("float4( %1, %2, %3, %4 )")
	    .arg(num(v.arity > 0 ? v.x : 0.0), num(v.arity > 1 ? v.y : 0.0),
	         num(v.arity > 2 ? v.z : 0.0), num(v.arity > 3 ? v.w : 0.0));
}

/// Zeroes every component at or above `arity` — the invariant above. Applied to
/// every op result, because plenty of GLSL builtins do NOT map 0 to 0
/// (`1.0 - x`, `pow(x, y)`, `x / y`) and would otherwise leave rubbish in the
/// padding lanes where the CPU has zeros.
QString mask(const QString &expr, int arity)
{
	switch (arity) {
	case 1: return QStringLiteral("float4( (%1).x, 0.0, 0.0, 0.0 )").arg(expr);
	case 2: return QStringLiteral("float4( (%1).xy, 0.0, 0.0 )").arg(expr);
	case 3: return QStringLiteral("float4( (%1).xyz, 0.0 )").arg(expr);
	default: return QStringLiteral("(%1)").arg(expr);
	}
}

/// Value::coerced, as an expression: splat a scalar, take leading components
/// when shrinking, repeat the last component when growing.
QString coerce(const QString &expr, int from, int to)
{
	if (to <= 0 || from == to) return expr;
	if (from == 1) {
		switch (to) {
		case 2: return QStringLiteral("float4( (%1).x, (%1).x, 0.0, 0.0 )").arg(expr);
		case 3: return QStringLiteral("float4( (%1).xxx, 0.0 )").arg(expr);
		default: return QStringLiteral("float4( (%1).xxxx )").arg(expr);
		}
	}
	if (from > to) return mask(expr, to);
	// grow: v.xyyy / v.xyzz
	if (from == 2) {
		return to == 3 ? QStringLiteral("float4( (%1).xyy, 0.0 )").arg(expr)
		               : QStringLiteral("float4( (%1).xyyy )").arg(expr);
	}
	return QStringLiteral("float4( (%1).xyzz )").arg(expr);   // from == 3, to == 4
}

const char *kComp[4] = { "x", "y", "z", "w" };

// ------------------------------------------------------------ op coverage
//
// The op table, straight against bakeprogram.cpp's registry. A key here is
// lowered; a key absent from it falls the graph back to the baker with the
// reason recorded. The absences are deliberate and each has a REASON, listed
// in rejectionFor(): they are not "not done yet" so much as "not the same
// value on both backends", which the parity oracle would catch anyway.
const QStringList &emittableOps()
{
	static const QStringList ops = {
		// literals
		"float", "color", "vector2", "vector3", "vector4", "property",
		// arithmetic
		"add", "subtract", "multiply", "vectorMultiply", "divide", "power", "sqrt",
		"min", "max", "abs", "sign", "ceil", "floor", "round", "trunc", "fraction",
		"oneminus", "negate", "sine",
		// interpolation / comparison
		"step", "smoothstep", "clamp", "lerp",
		// vector algebra
		"reflect", "dot", "length", "distance", "normalize",
		"splitvector", "composevector", "makeColor",
		// uv + normals
		"uv", "texCoords", "uvTransform", "panner", "flipbook",
		"normalintensity", "combinenormals",
		// the clock
		"time", "pulsate",
	};
	return ops;
}

QString rejectionFor(const QString &type)
{
	// Texture sampling: HlmsPbs' 16 texture units are SEMANTIC (albedo, normal,
	// metalness, roughness, emissive, reflection) and bound by the material
	// system, not by the graph. A piece could sample the slots that happen to
	// be bound, but a graph's Nth texture node has no slot to live in. The
	// baker already handles textures better than a piece would: a bare texture
	// chain is PASSTHROUGH (the source image binds directly, no resampling, no
	// quality loss at all), and a textured math chain bakes once instead of
	// being recomputed per pixel forever.
	if (type == "texture" || type == "textureSampler" || type == "texelsize" ||
	    type == "propertyNormalSample")
		return QStringLiteral("samples a texture: HlmsPbs' texture slots are semantic, so a "
		                      "graph texture has no unit to bind to — the baker keeps these "
		                      "(a bare texture chain passes through with no resampling)");
	// The fake fragment context. The CPU evaluates these against an IDENTITY
	// tangent frame at t=0 (normal (0,0,1), view (0,0,1), depth 0) and the
	// evaluator names them "approximated" for exactly that reason. A piece
	// would compute the REAL value, which is the right picture but a different
	// number — and the parity oracle exists to refuse two backends that
	// disagree. Lowering these needs the oracle to grow a per-op "the GPU is
	// allowed to differ here" rule first; that is a decision, not a patch.
	if (type == "worldNormal" || type == "localNormal" || type == "fresnel" || type == "depth")
		return QStringLiteral("evaluates against the fake fragment context on the CPU "
		                      "(identity normal/view, depth 0), so the two backends would "
		                      "legitimately disagree — kept on the baker until the parity "
		                      "oracle has a rule for per-op divergence");
	return QStringLiteral("no lowering for node type '%1'").arg(type);
}

// ------------------------------------------------------------ the lowering

struct Lowered
{
	bool ok = false;
	QString reason;
	QStringList lines;    ///< statements, in order
	QString rootVar;      ///< the variable holding the chain's result
	int rootArity = 4;
	bool animated = false;
};

/// Result arity of an op, given its inputs' SOCKET arities. Every rule here
/// mirrors one line of bakeprogram.cpp's evaluator — the CPU's arity is a pure
/// function of the op and its socket arities, which is what lets the emitter
/// know its types statically.
int arityOf(const BakeOp &op)
{
	const QString &t = op.typeName;
	if (op.hasLiteral) return op.literal.arity;
	auto in = [&](int i) { return i < op.inputs.size() ? op.inputs[i].arity : 4; };

	if (t == "dot" || t == "length" || t == "distance" || t == "splitvector" ||
	    t == "time" || t == "pulsate")
		return 1;
	if (t == "composevector" || t == "makeColor") return 4;
	if (t == "uv" || t == "texCoords" || t == "uvTransform" || t == "panner" ||
	    t == "flipbook")
		return 2;
	if (t == "normalintensity") return 3;
	if (t == "smoothstep" || t == "clamp") return in(2);
	// everything else is componentwise over its FIRST input (cw1/cw2/lerp/
	// reflect/normalize/combinenormals all take their arity from input 0)
	return in(0);
}

class Lowerer
{
public:
	explicit Lowerer(const BakeProgram &program, const QString &prefix)
	    : mProgram(program), mPrefix(prefix) {}

	Lowered run()
	{
		Lowered out;
		if (mProgram.rootOp < 0 || mProgram.rootOp >= mProgram.ops.size()) {
			out.reason = QStringLiteral("nothing connected");
			return out;
		}
		mArity.resize(mProgram.ops.size());
		for (int i = 0; i < mProgram.ops.size(); ++i) {
			const BakeOp &op = mProgram.ops[i];
			if (!op.unsupportedReason.isEmpty()) {
				out.reason = QStringLiteral("the chain is unsupported on both backends: %1")
				                 .arg(op.unsupportedReason);
				return out;
			}
			if (!op.hasLiteral && !emittableOps().contains(op.typeName)) {
				out.reason = rejectionFor(op.typeName);
				return out;
			}
			if (!emitOp(i, out)) return out;
		}
		out.ok = true;
		out.rootVar = var(mProgram.rootOp);
		out.rootArity = mArity[mProgram.rootOp];
		out.lines = mLines;
		out.animated = mAnimated;
		return out;
	}

private:
	QString var(int i) const { return QStringLiteral("%1%2").arg(mPrefix).arg(i); }

	/// One input, as a float4 expression already coerced to its socket arity.
	QString input(const BakeOp &op, int index)
	{
		if (index >= op.inputs.size()) return QStringLiteral("float4( 0.0, 0.0, 0.0, 0.0 )");
		const BakeInputRef &ref = op.inputs[index];
		QString expr;
		int arity = 4;
		if (ref.op >= 0) {
			expr = var(ref.op);
			arity = mArity[ref.op];
		} else if (ref.fallbackKind == BakeInputRef::Uv) {
			expr = QStringLiteral("float4( jahUv, 0.0, 0.0 )");
			arity = 2;
		} else if (ref.fallbackKind == BakeInputRef::Time) {
			expr = QStringLiteral("float4( %1, 0.0, 0.0, 0.0 )").arg(kClock);
			arity = 1;
			mAnimated = true;
		} else {
			expr = literal(ref.fallback);
			arity = ref.fallback.arity;
		}
		return coerce(expr, arity, ref.arity);
	}

	/// The scalar .x of an input — several ops read exactly one component
	/// (lerp's T, panner's time, makeColor's channels).
	QString scalar(const BakeOp &op, int index) { return QStringLiteral("(%1).x").arg(input(op, index)); }

	bool emitOp(int i, Lowered &out)
	{
		const BakeOp &op = mProgram.ops[i];
		const QString &t = op.typeName;
		const int arity = arityOf(op);
		mArity[i] = arity;

		if (op.hasLiteral) {
			mLines << QStringLiteral("const float4 %1 = %2;").arg(var(i), literal(op.literal));
			return true;
		}

		QString expr;
		if (t == "add")           expr = QStringLiteral("%1 + %2").arg(input(op, 0), input(op, 1));
		else if (t == "subtract") expr = QStringLiteral("%1 - %2").arg(input(op, 0), input(op, 1));
		else if (t == "multiply" || t == "vectorMultiply")
			expr = QStringLiteral("%1 * %2").arg(input(op, 0), input(op, 1));
		else if (t == "divide")   expr = QStringLiteral("%1 / %2").arg(input(op, 0), input(op, 1));
		else if (t == "power")    expr = QStringLiteral("pow( %1, %2 )").arg(input(op, 0), input(op, 1));
		else if (t == "sqrt")     expr = QStringLiteral("sqrt( %1 )").arg(input(op, 0));
		else if (t == "min")      expr = QStringLiteral("min( %1, %2 )").arg(input(op, 0), input(op, 1));
		else if (t == "max")      expr = QStringLiteral("max( %1, %2 )").arg(input(op, 0), input(op, 1));
		else if (t == "abs")      expr = QStringLiteral("abs( %1 )").arg(input(op, 0));
		else if (t == "sign")     expr = QStringLiteral("sign( %1 )").arg(input(op, 0));
		else if (t == "ceil")     expr = QStringLiteral("ceil( %1 )").arg(input(op, 0));
		else if (t == "floor")    expr = QStringLiteral("floor( %1 )").arg(input(op, 0));
		// GLSL has no round-half-away-from-zero builtin that matches std::round
		// (roundEven ties to even, round() is implementation-defined for .5);
		// this is std::round exactly.
		else if (t == "round")    expr = QStringLiteral("sign( %1 ) * floor( abs( %1 ) + 0.5 )")
		                                     .arg(input(op, 0));
		else if (t == "trunc")    expr = QStringLiteral("trunc( %1 )").arg(input(op, 0));
		else if (t == "fraction") expr = QStringLiteral("fract( %1 )").arg(input(op, 0));
		else if (t == "oneminus") expr = QStringLiteral("float4( 1.0, 1.0, 1.0, 1.0 ) - %1").arg(input(op, 0));
		else if (t == "negate")   expr = QStringLiteral("-%1").arg(input(op, 0));
		else if (t == "sine")     expr = QStringLiteral("sin( %1 )").arg(input(op, 0));
		// step( edge, x ): sockets are Edge, Value — the CPU's cw2 order.
		else if (t == "step")     expr = QStringLiteral("step( %1, %2 )").arg(input(op, 0), input(op, 1));
		else if (t == "smoothstep")
			expr = QStringLiteral("smoothstep( %1, %2, %3 )")
			           .arg(input(op, 0), input(op, 1), input(op, 2));
		// clamp( value, min, max ) — the socket order is Min, Max, Value.
		else if (t == "clamp")
			expr = QStringLiteral("clamp( %1, %2, %3 )").arg(input(op, 2), input(op, 0), input(op, 1));
		// mix's T is the SCALAR .x of the T socket, exactly as the CPU reads it.
		else if (t == "lerp")
			expr = QStringLiteral("mix( %1, %2, %3 )").arg(input(op, 0), input(op, 1), scalar(op, 2));
		// GLSL reflect( I, N ); the sockets are Normal, Incident.
		else if (t == "reflect")
			expr = QStringLiteral("reflect( %1, %2 )").arg(input(op, 1), input(op, 0));
		else if (t == "dot")
			expr = QStringLiteral("float4( dot( %1, %2 ), 0.0, 0.0, 0.0 )").arg(input(op, 0), input(op, 1));
		else if (t == "length")
			expr = QStringLiteral("float4( length( %1 ), 0.0, 0.0, 0.0 )").arg(input(op, 0));
		else if (t == "distance")
			expr = QStringLiteral("float4( distance( %1, %2 ), 0.0, 0.0, 0.0 )")
			           .arg(input(op, 0), input(op, 1));
		else if (t == "normalize") expr = QStringLiteral("normalize( %1 )").arg(input(op, 0));
		else if (t == "splitvector")
			expr = QStringLiteral("float4( (%1).%2, 0.0, 0.0, 0.0 )")
			           .arg(input(op, 0), kComp[qBound(0, op.outIndex, 3)]);
		else if (t == "composevector")
			expr = QStringLiteral("float4( %1, %2, %3, %4 )")
			           .arg(scalar(op, 0), scalar(op, 1), scalar(op, 2), scalar(op, 3));
		else if (t == "makeColor")
			expr = QStringLiteral("float4( %1, %2, %3, 1.0 )")
			           .arg(scalar(op, 0), scalar(op, 1), scalar(op, 2));
		else if (t == "texCoords")
			expr = QStringLiteral("float4( jahUv, 0.0, 0.0 )");
		// THE UV NODE: R(rot) * ((uv * tiling + offset) - 0.5) + 0.5, rotation
		// in degrees about the texture centre. The zero-rotation form is the
		// plain uv*s+o — chosen from BakeOp::uvRotationIsZero, the SAME flag the
		// CPU evaluator reads, so the two backends never disagree by the ulp
		// that a (x - 0.5) + 0.5 round-trip costs (shadergraph.emitter_parity).
		else if (t == "uv") {
			const QString tiled = QStringLiteral("((%1).xy * (%2).xy + (%3).xy)")
			                          .arg(input(op, 0), input(op, 1), input(op, 2));
			if (op.uvRotationIsZero) {
				expr = QStringLiteral("float4( %1, 0.0, 0.0 )").arg(tiled);
			}
			else {
				const QString rad = QStringLiteral("(%1 * 0.01745329252)").arg(scalar(op, 3));
				expr = QStringLiteral(
				           "float4( float2( cos( %1 ) * ((%2).x - 0.5) - sin( %1 ) * ((%2).y - 0.5) + 0.5,"
				           " sin( %1 ) * ((%2).x - 0.5) + cos( %1 ) * ((%2).y - 0.5) + 0.5 ), 0.0, 0.0 )")
				           .arg(rad, tiled);
			}
		}
		// uv * tiling + offset (the retired `uvTransform` node's op)
		else if (t == "uvTransform")
			expr = QStringLiteral("float4( (%1).xy * (%2).xy + (%3).xy, 0.0, 0.0 )")
			           .arg(input(op, 0), input(op, 1), input(op, 2));
		// uv + speed * time; the Time socket is read as a scalar
		else if (t == "panner")
			expr = QStringLiteral("float4( (%1).xy + (%2).xy * %3, 0.0, 0.0 )")
			           .arg(input(op, 0), input(op, 1), scalar(op, 2));
		else if (t == "flipbook") expr = flipbook(op);
		// normalize( mix( (0,0,1), N, intensity ) )
		else if (t == "normalintensity")
			expr = QStringLiteral("float4( normalize( mix( float3( 0.0, 0.0, 1.0 ), (%1).xyz, %2 ) ), 0.0 )")
			           .arg(input(op, 0), scalar(op, 1));
		else if (t == "combinenormals")
			expr = QStringLiteral("normalize( %1 + %2 )").arg(input(op, 0), input(op, 1));
		else if (t == "time") {
			expr = QStringLiteral("float4( %1, 0.0, 0.0, 0.0 )").arg(kClock);
			mAnimated = true;
		}
		else if (t == "pulsate") {
			expr = QStringLiteral("float4( sin( %1 * %2 ) * 0.5 + 0.5, 0.0, 0.0, 0.0 )")
			           .arg(kClock, scalar(op, 0));
			mAnimated = true;
		}
		else {
			out.reason = rejectionFor(t);
			return false;
		}

		mLines << QStringLiteral("const float4 %1 = %2;").arg(var(i), mask(expr, arity));
		return true;
	}

	/// The flipbook UV, transcribed from the CPU op (which is itself the
	/// post-D10 corrected row/column math).
	QString flipbook(const BakeOp &op)
	{
		const QString uv = input(op, 0);
		const QString rows = scalar(op, 1), cols = scalar(op, 2);
		const QString len = scalar(op, 3), time = scalar(op, 4);
		return QStringLiteral(
		           "float4( ( mod( floor( mod( (%5) / ( (%3) / ( (%1) * (%2) ) ), (%1) * (%2) ) ), (%2) ) "
		           "+ (%4).x ) * ( 1.0 / (%2) ), "
		           "( (%1) - floor( floor( mod( (%5) / ( (%3) / ( (%1) * (%2) ) ), (%1) * (%2) ) ) / (%2) ) - 1.0 "
		           "+ (%4).y ) * ( 1.0 / (%1) ), 0.0, 0.0 )")
		    .arg(rows, cols, len, uv, time);
	}

	const BakeProgram &mProgram;
	QString mPrefix;
	QVector<int> mArity;
	QStringList mLines;
	bool mAnimated = false;
};

// The master sockets each stage can land, and how.
struct SocketPlan
{
	const char *name;
	bool vertex;
};
const SocketPlan kPlans[] = {
	{ "Base Color", false },
	{ "Metallic", false },
	{ "Roughness", false },
	{ "Vertex Offset", true },
	{ "Vertex Extrusion", true },
};

QString reasonForUnsupportedSocket(const QString &name)
{
	if (name == "Normal")
		return QStringLiteral("the surface normal arrives in TANGENT space and is transformed by "
		                      "a TBN matrix that only exists when a normal map is bound, so a "
		                      "piece cannot write it safely — and a baked normal map is exactly "
		                      "what this socket already produces, at no loss");
	if (name == "Emissive")
		return QStringLiteral("emissive is read from the material constant buffer, which is "
		                      "READ-ONLY in the shader (`material` is a #define onto a const "
		                      "buffer array) — a piece cannot override it");
	if (name == "Alpha" || name == "Alpha Cutoff")
		return QStringLiteral("alpha is consumed before this hook (it scales F0 and drives the "
		                      "alpha test) and is material STATE rather than surface maths");
	return QStringLiteral("this master socket has no piece landing in v1");
}

} // namespace

// ------------------------------------------------------------------- emit

PieceEmitter::Result PieceEmitter::lower(const GraphBaker::CompiledGraph &compiled)
{
	Result result;
	if (!compiled.hasMaster) return result;
	if (!compiled.hasPbrMaster) {
		// The legacy Blinn-Phong master's sockets are APPROXIMATED onto PBR
		// keys by the baker (Shininess inverted into roughness, and so on).
		// Lowering an approximation would put the approximation in two places.
		result.fallbackReasons.insert(QString(),
		                              QStringLiteral("the legacy Blinn-Phong master approximates "
		                                             "its sockets onto PBR keys; only the PBR "
		                                             "master emits pieces"));
		return result;
	}

	QMap<QString, Lowered> lowered;
	bool anyVarying = false;

	for (const SocketPlan &plan : kPlans) {
		const GraphBaker::CompiledSlot *slot = nullptr;
		for (const auto &cs : compiled.sockets)
			if (cs.slot.socketName == QLatin1String(plan.name)) { slot = &cs; break; }
		if (!slot || !slot->connected) continue;

		Lowerer lowerer(slot->program, QStringLiteral("jah%1_")
		                                   .arg(QString(plan.name).remove(' ')));
		Lowered low = lowerer.run();
		if (!low.ok) {
			result.fallbackReasons.insert(plan.name, low.reason);
			continue;
		}
		// THE v1 SCOPE RULE, and it is a scope rule rather than a capability
		// one: a socket goes to the piece only when the BAKER CANNOT DO IT —
		// it reads the clock, or it is a vertex socket. A merely UV-varying
		// chain (a checker, a gradient, a UV-driven mask) bakes exactly as it
		// always did, and that is deliberate:
		//   * the bake is exact and already tested;
		//   * it is what glTF/web export consumes — a piece has no baked map
		//     to export, so taking these sockets would silently drop the
		//     pattern out of every exported build (§7.5 keeps export on the
		//     baker);
		//   * and it keeps every existing pixel and pipeline suite behaving
		//     the way it did.
		// Lifting this needs an export-time re-bake first; the emitter is
		// ready for it (nothing else in the lowering cares).
		if (!low.animated && !plan.vertex) {
			const bool constantFold =
			    slot->program.classification == BakeProgram::SocketClass::Uniform;
			result.fallbackReasons.insert(
			    plan.name,
			    constantFold
			        ? QStringLiteral("the chain folds to a constant, which the baker already "
			                         "lands exactly — a piece would add a shader permutation "
			                         "and change nothing")
			        : QStringLiteral("the chain varies with UV but not with time, and the baker "
			                         "lands that exactly — and its baked map is what glTF/web "
			                         "export consumes. v1 emits pieces for what the baker CANNOT "
			                         "do: the clock, and vertex motion"));
			continue;
		}
		lowered.insert(plan.name, low);
		if (low.animated) anyVarying = true;
	}

	// Sockets the emitter has no landing for at all, reported so a user can see
	// WHY their graph did not animate rather than guessing.
	for (const auto &cs : compiled.sockets) {
		if (!cs.connected) continue;
		bool planned = false;
		for (const SocketPlan &plan : kPlans)
			if (cs.slot.socketName == QLatin1String(plan.name)) { planned = true; break; }
		if (!planned) result.fallbackReasons.insert(cs.slot.socketName,
		                                            reasonForUnsupportedSocket(cs.slot.socketName));
	}

	if (lowered.isEmpty()) return result;

	// THE COUPLING RULE. In the metallic workflow the shader has already turned
	// base colour and metalness into pixelData.diffuse and pixelData.F0 by the
	// time our hook runs, and neither is recoverable from the other. So a piece
	// that owns EITHER must own BOTH — and if the one it does not have from the
	// graph would come from a baked MAP, the piece cannot honour it and the
	// whole graph goes back to the baker rather than silently dropping a map.
	const bool wantsSurface = lowered.contains("Base Color") || lowered.contains("Metallic");
	if (wantsSurface) {
		for (const char *name : { "Base Color", "Metallic" }) {
			if (lowered.contains(name)) continue;
			const GraphBaker::CompiledSlot *slot = nullptr;
			for (const auto &cs : compiled.sockets)
				if (cs.slot.socketName == QLatin1String(name)) { slot = &cs; break; }
			const bool needsMap =
			    slot && slot->connected &&
			    (slot->program.classification == BakeProgram::SocketClass::Baked ||
			     slot->program.classification == BakeProgram::SocketClass::Passthrough);
			if (!needsMap) continue;
			Result refused;
			refused.fallbackReasons.insert(
			    QString(), QStringLiteral("'%1' needs a baked map while another surface socket "
			                              "emits a piece; base colour and metalness are fused "
			                              "into the shader's diffuse/F0 before the hook, so the "
			                              "piece would have to drop that map — the whole graph "
			                              "stays on the baker instead").arg(name));
			return refused;
		}
	}

	// NOTHING TO GAIN, NOTHING TO PAY. A graph whose emittable sockets are all
	// constant folds is already exact on the baker, for free, with no extra
	// shader permutation — so it does not get a piece. The vertex sockets are
	// the exception: the baker cannot express them at all.
	const bool vertexWork = lowered.contains("Vertex Offset") || lowered.contains("Vertex Extrusion");
	if (!anyVarying && !vertexWork) {
		result.fallbackReasons.insert(
		    QString(), QStringLiteral("every connected socket folds to a constant, which the "
		                              "baker already lands exactly — a piece would add a shader "
		                              "permutation and change nothing"));
		return result;
	}

	// ---- the pixel piece
	QStringList ps;
	const bool hasBase = lowered.contains("Base Color");
	const bool hasMetal = lowered.contains("Metallic");
	const bool hasRough = lowered.contains("Roughness");
	if (hasBase || hasMetal || hasRough) {
		ps << QStringLiteral("@piece( custom_ps_preLights )")
		   << QStringLiteral("{")
		   << QStringLiteral("\t@property( hlms_uv_count )")
		   << QStringLiteral("\t\tconst float2 jahUv = inPs.uv0.xy;")
		   << QStringLiteral("\t@else")
		   << QStringLiteral("\t\tconst float2 jahUv = float2( 0.0, 0.0 );")
		   << QStringLiteral("\t@end");
		for (const char *name : { "Base Color", "Metallic", "Roughness" }) {
			if (!lowered.contains(name)) continue;
			for (const QString &line : lowered[name].lines) ps << QStringLiteral("\t") + line;
			result.emittedSockets << QString::fromLatin1(name);
			result.animated |= lowered[name].animated;
		}
		if (hasBase || hasMetal) {
			// Both terms, always, and in the shader's own order (upstream's
			// SampleSpecularMap, metallic branch): F0 is a function of BOTH
			// base colour and metalness, and pixelData.diffuse has already had
			// the metalness taken out of it.
			//
			// THE 1/PI IS NOT DECORATION. HlmsPbsDatablock::setDiffuse stores
			// kD = albedo * 0.318309886 (OgreHlmsPbsDatablock.cpp:492), so by
			// this hook `pixelData.diffuse` holds albedo/PI — which is exactly
			// why the template multiplies it BACK by 3.14159 when it builds F0.
			// A piece that writes a raw albedo here renders PI times too
			// bright, and the parity oracle catches it as a ~3x error (it did).
			// The `material.kD` fallback below is already divided.
			const QString base =
			    hasBase ? QStringLiteral("(%1).xyz * 0.318309886")
			                  .arg(coerce(lowered["Base Color"].rootVar,
			                              lowered["Base Color"].rootArity, 3))
			            : QStringLiteral("float3( midf3_c( material.kD.xyz ) )");
			const QString metal =
			    hasMetal ? QStringLiteral("(%1).x").arg(lowered["Metallic"].rootVar)
			             : QStringLiteral("float( material.F0.x )");
			ps << QStringLiteral("\tconst float3 jahBase = %1;").arg(base)
			   << QStringLiteral("\tconst float jahMetal = %1;").arg(metal)
			   << QStringLiteral("\tpixelData.F0 = lerp( make_float_fresnel( 0.04f ), "
			                     "midf3_c( jahBase ) * _h( 3.14159f ), midf_c( jahMetal ) );")
			   << QStringLiteral("\tpixelData.diffuse.xyz = midf3_c( jahBase - jahBase * jahMetal );");
		}
		if (hasRough) {
			// perceptualRoughness is the authored value; `roughness` is its
			// square, clamped exactly as the template clamps it.
			ps << QStringLiteral("\tpixelData.perceptualRoughness = midf_c( (%1).x );")
			          .arg(lowered["Roughness"].rootVar)
			   << QStringLiteral("\tpixelData.roughness = max( pixelData.perceptualRoughness * "
			                     "pixelData.perceptualRoughness, _h( 0.001f ) );");
		}
		ps << QStringLiteral("}") << QStringLiteral("@end");
	}

	// ---- the vertex piece
	QStringList vs;
	if (vertexWork) {
		vs << QStringLiteral("@piece( custom_vs_preTransform )")
		   << QStringLiteral("{")
		   << QStringLiteral("\t@property( hlms_uv_count )")
		   << QStringLiteral("\t\tconst float2 jahUv = inVs_uv0.xy;")
		   << QStringLiteral("\t@else")
		   << QStringLiteral("\t\tconst float2 jahUv = float2( 0.0, 0.0 );")
		   << QStringLiteral("\t@end");
		for (const char *name : { "Vertex Offset", "Vertex Extrusion" }) {
			if (!lowered.contains(name)) continue;
			for (const QString &line : lowered[name].lines) vs << QStringLiteral("\t") + line;
			result.emittedSockets << QString::fromLatin1(name);
			result.animated |= lowered[name].animated;
		}
		if (lowered.contains("Vertex Offset")) {
			// worldPos is already world-space here and is what the view
			// projection consumes two lines later — a world-space offset is
			// exactly what the socket means.
			const Lowered &low = lowered["Vertex Offset"];
			vs << QStringLiteral("\tworldPos.xyz += (%1).xyz;")
			          .arg(coerce(low.rootVar, low.rootArity, 3));
		}
		if (lowered.contains("Vertex Extrusion")) {
			// Along the WORLD normal, which has two different spellings: the
			// skinned path has already blended `worldNorm` across the bones,
			// while the rigid path must rotate the object-space normal by the
			// object's own matrix. A mesh with no normals gets neither — it
			// has no surface to extrude along and no lighting either.
			const Lowered &low = lowered["Vertex Extrusion"];
			const QString k = QStringLiteral("(%1).x").arg(low.rootVar);
			vs << QStringLiteral("\t@property( hlms_normal || hlms_qtangent )")
			   << QStringLiteral("\t\t@property( hlms_skeleton )")
			   << QStringLiteral("\t\t\tworldPos.xyz += %1 * normalize( float3( worldNorm ) );").arg(k)
			   << QStringLiteral("\t\t@else")
			   << QStringLiteral("\t\t\tworldPos.xyz += %1 * normalize( mul( float4( float3( inputNormal ), "
			                     "0.0 ), worldMat ) );").arg(k)
			   << QStringLiteral("\t\t@end")
			   << QStringLiteral("\t@end");
		}
		vs << QStringLiteral("}") << QStringLiteral("@end");
	}

	if (ps.isEmpty() && vs.isEmpty()) return result;

	const QString header = QStringLiteral(
	    "// GENERATED by Jahshaka's shader-graph emitter (HLMS_ADOPTION P5).\n"
	    "// Do not edit: this file is CONTENT-ADDRESSED — its name is a hash of\n"
	    "// these bytes, and the graph it came from is the source of truth.\n"
	    "// An edit here would make the name a lie and the shader cache stale.\n"
	    "// The Hlms parser executes at-directives inside comments, so this\n"
	    "// header never spells one.\n");
	if (!ps.isEmpty()) result.pixelSource = header + ps.join(QLatin1Char('\n')) + "\n";
	if (!vs.isEmpty()) result.vertexSource = header + vs.join(QLatin1Char('\n')) + "\n";
	result.accepted = true;
	return result;
}

PieceEmitter::Result PieceEmitter::lower(NodeGraph *graph, BakeProgram::TextureResolver resolver)
{
	return lower(GraphBaker::compile(graph, resolver));
}

QString PieceEmitter::fileNameFor(const QString &source, bool vertexStage)
{
	const QString hash = QString::fromLatin1(
	    QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
	return hash + (vertexStage ? QStringLiteral(".piece_vs.glsl") : QStringLiteral(".piece_ps.glsl"));
}

QString PieceEmitter::cacheDir()
{
	const QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
	return (root.isEmpty() ? QDir::homePath() + QStringLiteral("/.cache/jahshaka") : root) +
	       QStringLiteral("/ShaderPieces");
}

QString PieceEmitter::write(const QString &dir, const QString &source, bool vertexStage)
{
	if (source.isEmpty()) return {};
	QDir().mkpath(dir);
	const QString path = dir + QLatin1Char('/') + fileNameFor(source, vertexStage);
	// A CACHE HIT is the common case and must not rewrite the file: Ogre keys
	// its piece registry by name and would throw if the bytes ever differed,
	// and a rewrite would also churn the mtime the shader cache watches.
	if (QFileInfo::exists(path)) return path;
	if (!FileWrite::writeFileAtomic(path, source.toUtf8())) return {};
	return path;
}

QJsonObject PieceEmitter::emitAndStore(NodeGraph *graph, BakeProgram::TextureResolver resolver,
                                       Result *resultOut)
{
	const Result result = lower(graph, resolver);
	if (resultOut) *resultOut = result;
	QJsonObject out;
	if (!result.accepted) return out;
	const QString dir = cacheDir();
	const QString ps = write(dir, result.pixelSource, false);
	const QString vs = write(dir, result.vertexSource, true);
	if (!ps.isEmpty()) out["customPiecePixel"] = ps;
	if (!vs.isEmpty()) out["customPieceVertex"] = vs;
	return out;
}

const QStringList &PieceEmitter::supportedOps() { return emittableOps(); }

const QStringList &PieceEmitter::supportedSockets()
{
	static QStringList names = [] {
		QStringList out;
		for (const SocketPlan &p : kPlans) out << QString::fromLatin1(p.name);
		return out;
	}();
	return names;
}

} // namespace materials
