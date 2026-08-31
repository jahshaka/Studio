/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#pragma once

// Materials Evaluator phase 1 (SPECS/MATERIALS_EVALUATOR_SPEC.md section 1):
// compile the sub-graph feeding one master input socket into a BakeProgram -
// a flat post-order op list that is a pure value object (no QObject, no
// QWidget), safe to evaluate on any thread. The CPU semantics of every op are
// the parity contract with the (post-defect-fix) GLSL emitter; type coercion
// mirrors core/sockethelper.cpp exactly:
//   float -> vecN   splat            vec4(f)
//   shrink          leading comps    v.xy
//   grow            repeat last      v.xyyy
//
// Documented judgment calls (spec section 1.2/1.5 anchors):
// - Components are doubles (uniform folds compare exact vs hand-computed
//   fixtures); baked pixels quantize to 8 bits anyway.
// - A vector2 LANDING DIRECTLY on a color slot folds to (x, y, 0) - the
//   audit D5 contract - while a vec2 flowing through a wider socket inside a
//   chain coerces GLSL-exact (v.xyy / v.xyyy).
// - fresnel honors its Normal input on the CPU (the audit D17 intent; the
//   legacy GLSL body still hardcodes v_normal and dies in phase 5).
// - composevector produces a vec4 (audit D11 - the out socket is vec4 now).

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

class NodeGraph;
class NodeModel;
class SocketModel;

namespace materials {

// A GLSL-style value: up to 4 components, doubles for exact uniform folds.
struct Value
{
	double x = 0, y = 0, z = 0, w = 0;
	int arity = 1;

	Value() {}
	Value(double s) : x(s), arity(1) {}
	Value(double x_, double y_) : x(x_), y(y_), arity(2) {}
	Value(double x_, double y_, double z_) : x(x_), y(y_), z(z_), arity(3) {}
	Value(double x_, double y_, double z_, double w_) : x(x_), y(y_), z(z_), w(w_), arity(4) {}

	double component(int i) const
	{
		switch (i) {
		case 0: return x;
		case 1: return y;
		case 2: return z;
		default: return w;
		}
	}
	void setComponent(int i, double v)
	{
		switch (i) {
		case 0: x = v; break;
		case 1: y = v; break;
		case 2: z = v; break;
		default: w = v; break;
		}
	}

	// sockethelper.cpp rules: splat / leading / repeat-last.
	Value coerced(int toArity) const;
};

// The fake fragment context (spec section 1.4). position = (u, v, 0).
struct EvalContext
{
	double u = 0, v = 0;   // bake UV in [0,1]^2
	double time = 0;       // bake parameter, t=0 in this program
	// tangent-space identity context
	double normalX = 0, normalY = 0, normalZ = 1;
	double viewX = 0, viewY = 0, viewZ = 1;
};

struct BakeInputRef
{
	enum FallbackKind { Literal, Uv, Time };
	int op = -1;              // index into BakeProgram::ops; -1 = unconnected
	int arity = 4;            // the input socket's arity (coercion target)
	FallbackKind fallbackKind = Literal;
	Value fallback;           // parsed socket default (Literal kind)
};

// One node output. Op identity is (source node, outIndex) - a node with
// several outputs compiles to one op per used output, memoized.
struct BakeOp
{
	QString typeName;
	QString nodeId;
	int outIndex = 0;
	QVector<BakeInputRef> inputs;

	Value literal;            // constants and folded properties
	bool hasLiteral = false;

	QImage image;             // the texture this op samples (RGBA8888)
	QString imagePath;        // resolved source path
	QString imageStamp;       // path|mtime|size - cache-key ingredient
	bool isTextureCarrier = false; // texture node / texture-property out 0

	// classification flags, propagated through inputs at compile time
	bool varying = false;
	bool animated = false;
	bool approximated = false;
	QString unsupportedReason; // non-empty: this chain cannot evaluate
};

class BakeProgram
{
public:
	enum class SocketClass { Unconnected, Uniform, Passthrough, Baked, Unsupported };

	QVector<BakeOp> ops;
	int rootOp = -1;

	SocketClass classification = SocketClass::Unconnected;
	QStringList approximatedNodes; // typeNames, deduped
	QStringList unsupportedNodes;  // typeNames/descriptions, deduped
	bool animated = false;

	// Passthrough: the source image bound directly as the map, no bake.
	QString passthroughPath;
	QString passthroughStamp;

	// Maps a texture reference (asset GUID or path) to an image path.
	using TextureResolver = std::function<QString(const QString&)>;

	// Compiles the chain feeding `masterInput`. Main thread only (node models
	// carry live QWidgets); the resulting program is thread-safe by value.
	static BakeProgram compile(SocketModel* masterInput, const TextureResolver& resolve);

	// Runs the op list at one fragment context. Meaningful for Uniform
	// programs at any context, and per-texel for Baked ones.
	Value evaluate(const EvalContext& ctx) const;

	// Deterministic content signature of the compiled chain (op list,
	// literals, image stamps) - the section 1.6 hash ingredient.
	QByteArray signature() const;

	static QString classToString(SocketClass c);

	// GLSL-parity bilinear sample with repeat wrap of an RGBA8888 image.
	static Value sampleImage(const QImage& image, double u, double v);
};

} // namespace materials
