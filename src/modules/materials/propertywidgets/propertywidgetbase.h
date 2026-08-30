#pragma once
#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>


class WideRangeSpinBox;
class WideRangeIntBox;
class PropertyWidgetBase : public QWidget
{
public:
	PropertyWidgetBase();
	~PropertyWidgetBase();
	QFont font;

	QLayout *layout;
};

class Widget2D : public PropertyWidgetBase
{
	Q_OBJECT
public:
	Widget2D();
	~Widget2D();

	WideRangeSpinBox* xSpinBox;
	WideRangeSpinBox* ySpinBox;
	double x;
	double y;
	void setValues(float xValue, float yValue);
signals:
	void valueChanged(QVector2D val);

};


class Widget3D : public PropertyWidgetBase
{
	Q_OBJECT
public:
	Widget3D();
	~Widget3D();

	WideRangeSpinBox* xSpinBox;
	WideRangeSpinBox* ySpinBox;
	WideRangeSpinBox* zSpinBox;
	double x;
	double y;
	double z;
	void setValues(float xValue, float yValue, float zValue);

signals:
	void valueChanged(QVector3D val);
};


class Widget4D : public PropertyWidgetBase
{
	Q_OBJECT
public:
	Widget4D();
	~Widget4D();

	void setXSpinBoxConnection(std::function<void(double val)> func = nullptr);
	void setYSpinBoxConnection(std::function<void(double val)> func = nullptr);
	void setZSpinBoxConnection(std::function<void(double val)> func = nullptr);
	void setWSpinBoxConnection(std::function<void(double val)> func = nullptr);

	WideRangeSpinBox* xSpinBox;
	WideRangeSpinBox* ySpinBox;
	WideRangeSpinBox* zSpinBox;
	WideRangeSpinBox* wSpinBox;
	double x;
	double y;
	double z;
	double w;
	void setValues(float xValue, float yValue, float zValue, float wValue);

signals:
	void valueChanged(QVector4D val);

};


enum class LabelState {
	Visible,
	Hidden
};

class WidgetInt : public PropertyWidgetBase
{
	Q_OBJECT
public:
	WidgetInt();
	WidgetInt(LabelState statue);
	~WidgetInt();

	void setIntSpinBoxConnection(std::function<void(int val)> func = nullptr);

	WideRangeIntBox* spinBox;
	WideRangeIntBox* maxSpinBox;
	WideRangeIntBox* minSpinBox;
	QSpinBox* stepSpinBox;
	int value;
	int min = 0;
	int max = 0;
	int step = 0;
	LabelState state = LabelState::Visible;
signals:
	void valueChanged(int val);
	void minChanged(int val);
	void maxChanged(int val);
	void stepChanged(int val);
};


class WidgetFloat : public PropertyWidgetBase
{
	Q_OBJECT
public:
	WidgetFloat();
	WidgetFloat(LabelState statue);
	~WidgetFloat();

	void setFloatSpinBoxConnection(std::function<void(double val)> func = nullptr);

	WideRangeSpinBox* floatSpinBox;
	WideRangeSpinBox* maxSpinBox;
	WideRangeSpinBox* minSpinBox;
	QDoubleSpinBox* stepSpinBox;
	double value;
	double min;
	double max;
	double step;

	LabelState state = LabelState::Visible;
signals:
	void valueChanged(double val);
	void minChanged(double val);
	void maxChanged(double val);
	void stepChanged(double val);

};

class WidgetTexture : public PropertyWidgetBase
{
	Q_OBJECT
public:
	WidgetTexture();
	~WidgetTexture();

	QPushButton *texture;
	QString fileName;
};



