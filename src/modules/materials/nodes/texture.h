#pragma once

#include "irisgl/core/math/vec.h"
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>

#include "../graph/nodegraph.h"
#include "../models/nodemodel.h"
#include "../graph/sockets.h"
#include "../propertywidgets/propertywidgetbase.h"

/* Blends two normals */
class CombineNormalsNode : public NodeModel
{
public:
	CombineNormalsNode();

};

/* Provides the texture width, height, 1/width and 1/height — plus the
   Aspect (W/H) and 1/Aspect ratio outs (IMAGE_PLANE_SPEC option C.2) */
class TexelSizeNode : public NodeModel
{
public:
	TexelSizeNode();

};

/* THE UV NODE (MATERIAL_UV_NODES_SPEC D-3, typeName "uv").
   Unreal's TextureCoordinate: one node carrying the coordinate source AND its
   transform — `R(rot) * ((uv * tiling + offset) - 0.5) + 0.5`, rotation in
   DEGREES about the texture centre (Unreal's CustomRotator convention).

   It REPLACES two nodes that were the same idea split in half: `texCoords`
   (bare UV + an unserialized TexCoord0-3 combo) and `uvTransform` (tiling +
   offset, IMAGE_PLANE_SPEC option C.1). Both typeNames survive as hidden load
   aliases (NodeLibrary::addAlias) and both keep their socket indices here, so
   every saved graph re-attaches 1:1 with no migration:
       in 0 UV · in 1 Tiling · in 2 Offset · in 3 Rotation (APPENDED) · out 0 UV
   `texCoords` saved no input connections at all (it had no inputs) and its
   only output was index 0 — so the alias is index-safe in both directions.

   The inline editors write the socket DEFAULTS, so a connected Tiling/Offset/
   Rotation socket overrides them (the node-editor convention). The UV Set combo
   is a persisted WIDGET field, not a socket: the bake evaluates every set as
   UV0 today (MATERIALS_EVALUATOR_SPEC §1.2) and reports uvSet > 0 as
   approximated; the field is stored so a graph authored for UV1 survives until
   BAKE_LIGHTING phase 0 gives it a second UV stream. */
class UVNode : public NodeModel
{
public:
	UVNode();

	QJsonValue serializeWidgetValue(int widgetIndex = 0) override;
	void deserializeWidgetValue(QJsonValue val, int widgetIndex = 0) override;

	/// The persisted UV-set index (0..3). Read by the evaluator's `approximated`
	/// report; nothing samples a second stream yet.
	int getUvSet() const { return uvSet; }

private:
	void pushSocketDefaults();

	QDoubleSpinBox *tileXBox, *tileYBox, *offsetXBox, *offsetYBox, *rotationBox;
	QComboBox *uvSetCombo;
	iris::Vec2 tiling = iris::Vec2(1.0f, 1.0f);
	iris::Vec2 offset = iris::Vec2(0.0f, 0.0f);
	float rotation = 0.0f;   // degrees
	int uvSet = 0;
};

/// Old name, kept so an out-of-tree include still compiles; the class merged.
using UVTransformNode = UVNode;

/*
// Samples texture
class SampleTextureNode : public NodeModel
{
public:
	SampleTextureNode();

};
*/
/*
// Samples texture as latlong, requires vec3 UVs
class SampleEquirectangularTextureNode : public NodeModel
{
public:
	SampleEquirectangularTextureNode();

};
*/
/* Generates UV based on flipbook animation
	Inputs: UV, rows, columns, startframe
	Output: UV
*/

class FlipbookUVAnimationNode : public NodeModel
{
public:
	FlipbookUVAnimationNode();

};

