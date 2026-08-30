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
	QLineEdit* lineEdit;

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
	TexturePropertyWidget *textureWidget;
public:
	TextureNode();
	virtual void process(ModelContext* context) override;

	// image path of the chosen texture, empty if none (used by PbrGraphEvaluator)
	QString getTexturePath() const;
};

class PropertyNode : public NodeModel
{
	GraphTexture* graphTexture;
	Property* prop;
public:
	PropertyNode();

	// doesnt own property
	void setProperty(Property* property);
	Property* getProperty() { return prop; }

	virtual QJsonValue serializeWidgetValue(int widgetIndex = 0) override;

	virtual void process(ModelContext* context) override;
};

class TexturePropertyNode : public NodeModel
{
	TextureProperty* prop;
public:
	TexturePropertyNode();

	// doesnt own property
	void setProperty(Property* property);

	virtual QJsonValue serializeWidgetValue(int widgetIndex = 0) override;

	virtual void process(ModelContext* context) override;
};

class Vector2Node : public NodeModel
{
public:
	Vector2Node();
	int x, y;
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
	int x, y, z;
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
	int x, y, z, w;
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
