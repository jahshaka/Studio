#pragma once

#include <QLineEdit>
#include <QComboBox>
#include <QDoubleValidator>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>

#include "../graph/nodegraph.h"
#include "../models/nodemodel.h"
#include "../graph/sockets.h"
#include "../generator/shadercontext.h"
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
	virtual void process(ModelContext* ctx) override;
};


class FloatNodeModel : public NodeModel
{
	QDoubleSpinBox* valueBox;

	FloatSocketModel* valueSock;
public:
	FloatNodeModel();

	void editTextChanged(const QString& text);
	virtual void process(ModelContext* context) override;

	virtual QJsonValue serializeWidgetValue(int widgetIndex = 0) override;

	virtual void deserializeWidgetValue(QJsonValue val, int widgetIndex = 0) override;
};

class VectorMultiplyNode : public NodeModel
{
public:
	VectorMultiplyNode();

	virtual void process(ModelContext* context) override;
};

class WorldNormalNode : public NodeModel
{
public:
	WorldNormalNode();

	virtual void process(ModelContext* context) override;
};

class LocalNormalNode : public NodeModel
{
public:
	LocalNormalNode();

	virtual void process(ModelContext* context) override;
};

class TimeNode : public NodeModel
{
public:
	TimeNode();

	virtual void process(ModelContext* context) override;
};

class SineNode : public NodeModel
{
public:
	SineNode();

	virtual void process(ModelContext* context) override;
};

class PulsateNode : public NodeModel
{
public:
	PulsateNode();

	virtual void process(ModelContext* context) override;
};

class PannerNode : public NodeModel
{
public:
	PannerNode();

	virtual void process(ModelContext* context) override;
};

class NormalIntensityNode : public NodeModel
{
public:
	NormalIntensityNode();

	virtual void process(ModelContext* context) override;
};


class MakeColorNode : public NodeModel
{
public:
	MakeColorNode();

	virtual void process(ModelContext *context) override;
};

class TextureCoordinateNode : public NodeModel
{
	QComboBox* combo;
	QString uv;
public:
	TextureCoordinateNode();

	virtual void process(ModelContext* context) override;

	void comboTextChanged(const QString& text);
};

class TextureSamplerNode : public NodeModel
{
	QComboBox* combo;
	QString uv;
public:
	TextureSamplerNode();

	virtual void process(ModelContext* context) override;

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
	virtual void process(ModelContext* context) override;

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
	QVector2D value;
	QDoubleSpinBox *xSpinBox, *ySpinBox;
	virtual void process(ModelContext* context) override;

	QJsonValue serializeWidgetValue(int widgetIndex);
	void deserializeWidgetValue(QJsonValue val, int widgetIndex);
};

class Vector3Node : public NodeModel
{
public:
	Vector3Node();
	double x, y, z; // was int — truncated every fractional component (audit D4)
	QVector3D value;
	QDoubleSpinBox *xSpinBox, *ySpinBox, *zSpinBox;
	virtual void process(ModelContext* context) override;

	QJsonValue serializeWidgetValue(int widgetIndex);
	void deserializeWidgetValue(QJsonValue val, int widgetIndex);
};

class Vector4Node : public NodeModel
{
public:
	Vector4Node();
	double x, y, z, w; // was int — truncated every fractional component (audit D4)
	QVector4D value;
	QDoubleSpinBox *xSpinBox, *ySpinBox, *zSpinBox, *wSpinBox;
	virtual void process(ModelContext* context) override;

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
	virtual void process(ModelContext* context) override;

	QJsonValue serializeWidgetValue(int widgetIndex);
	void deserializeWidgetValue(QJsonValue val, int widgetIndex);
};
#endif
