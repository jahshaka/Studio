#pragma once

#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QVector2D>

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

/* UV Transform (IMAGE_PLANE_SPEC option C.1) — Unreal's TexCoord →
   Multiply → Constant2Vector ergonomics as ONE node: uv * tiling + offset.
   The inline vec2 editors write the Tiling/Offset socket DEFAULTS, so a
   connected socket overrides them (the node-editor convention). */
class UVTransformNode : public NodeModel
{
public:
	UVTransformNode();

	QJsonValue serializeWidgetValue(int widgetIndex = 0) override;
	void deserializeWidgetValue(QJsonValue val, int widgetIndex = 0) override;

private:
	void pushSocketDefaults();

	QDoubleSpinBox *tileXBox, *tileYBox, *offsetXBox, *offsetYBox;
	QVector2D tiling = QVector2D(1.0f, 1.0f);
	QVector2D offset = QVector2D(0.0f, 0.0f);
};

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

