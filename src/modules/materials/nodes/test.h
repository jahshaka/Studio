#pragma once

#include "irisgl/core/math/vec.h"
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleValidator>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>

#include "../graph/nodegraph.h"
#include "../models/nodemodel.h"
#include "../graph/sockets.h"
#include "../propertywidgets/propertywidgetbase.h"
#include "../propertywidgets/texturepropertywidget.h"


class QDoubleSpinBox;

#if(EFFECT_BUILD_AS_LIB)
#include "ui/controls/colorpickerwidget.h"
#endif

class SurfaceMasterNode : public NodeModel
{
public:
	SurfaceMasterNode();
};


class FloatNodeModel : public NodeModel
{
	QDoubleSpinBox* valueBox;

	FloatSocketModel* valueSock;
public:
	FloatNodeModel();

	void editTextChanged(const QString& text);

	virtual QJsonValue serializeWidgetValue(int widgetIndex = 0) override;

	virtual void deserializeWidgetValue(QJsonValue val, int widgetIndex = 0) override;
};

class VectorMultiplyNode : public NodeModel
{
public:
	VectorMultiplyNode();

};

class WorldNormalNode : public NodeModel
{
public:
	WorldNormalNode();

};

class LocalNormalNode : public NodeModel
{
public:
	LocalNormalNode();

};

class TimeNode : public NodeModel
{
public:
	TimeNode();

};

class SineNode : public NodeModel
{
public:
	SineNode();

};

class PulsateNode : public NodeModel
{
public:
	PulsateNode();

};

class PannerNode : public NodeModel
{
public:
	PannerNode();

};

class NormalIntensityNode : public NodeModel
{
public:
	NormalIntensityNode();

};


class MakeColorNode : public NodeModel
{
public:
	MakeColorNode();

};

class TextureCoordinateNode : public NodeModel
{
	QComboBox* combo;
	QString uv;
public:
	TextureCoordinateNode();


	void comboTextChanged(const QString& text);
};

class TextureSamplerNode : public NodeModel
{
	QComboBox* combo;
	QString uv;
public:
	TextureSamplerNode();


	void comboTextChanged(const QString& text);
};

class GraphTexture;
class TextureNode : public NodeModel
{
	Q_OBJECT
	QPushButton *texture;
	GraphTexture* graphTexture;
public:
	TextureNode();

	// image path of the chosen texture, empty if none (used by PbrGraphEvaluator)
	QString getTexturePath() const;

	void setTexturePath(const QString& path);

	// DB-backed route (§3b property migration + the panel's picker): the
	// stored value is an asset guid; the path resolves through TextureManager
	// when a database is behind it, and stays empty otherwise.
	QString getTextureGuid() const;
	void setTextureGuid(const QString& guid);

	virtual QJsonValue serializeWidgetValue(int widgetIndex = 0) override;
	virtual void deserializeWidgetValue(QJsonValue val, int widgetIndex = 0) override;
};

// (PropertyNode / TexturePropertyNode retired 2026-08-31, §3b: graph-global
// properties migrate to real constant/texture nodes at load time —
// NodeGraph::deserialize)

class Vector2Node : public NodeModel
{
public:
	Vector2Node();
	double x, y; // was int — truncated every fractional component (audit D4)
	iris::Vec2 value;
	QDoubleSpinBox *xSpinBox, *ySpinBox;

	QJsonValue serializeWidgetValue(int widgetIndex);
	void deserializeWidgetValue(QJsonValue val, int widgetIndex);
};

class Vector3Node : public NodeModel
{
public:
	Vector3Node();
	double x, y, z; // was int — truncated every fractional component (audit D4)
	iris::Vec3 value;
	QDoubleSpinBox *xSpinBox, *ySpinBox, *zSpinBox;

	QJsonValue serializeWidgetValue(int widgetIndex);
	void deserializeWidgetValue(QJsonValue val, int widgetIndex);
};

class Vector4Node : public NodeModel
{
public:
	Vector4Node();
	double x, y, z, w; // was int — truncated every fractional component (audit D4)
	iris::Vec4 value;
	QDoubleSpinBox *xSpinBox, *ySpinBox, *zSpinBox, *wSpinBox;

	QJsonValue serializeWidgetValue(int widgetIndex);
	void deserializeWidgetValue(QJsonValue val, int widgetIndex);
};

#if(EFFECT_BUILD_AS_LIB)
class ColorPickerNode : public NodeModel
{
public:
	ColorPickerNode();

private:
	ColorPickerWidget *colorWidget;

	QJsonValue serializeWidgetValue(int widgetIndex);
	void deserializeWidgetValue(QJsonValue val, int widgetIndex);
};
#endif
