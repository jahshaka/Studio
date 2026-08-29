#include "propertywidgetbase.h"
#include <QLabel>
#include <QDebug>
#include <QFileDialog>
#include "basepropertywidget.h"

PropertyWidgetBase::PropertyWidgetBase() : QWidget()
{
	layout = new QHBoxLayout;
	setLayout(layout);
}


PropertyWidgetBase::~PropertyWidgetBase()
{
}


Widget2D::Widget2D() : PropertyWidgetBase()
{
	x = y = 0;
	xSpinBox = new WideRangeSpinBox;
	ySpinBox = new WideRangeSpinBox;
	auto xLabel = new QLabel("X");
	auto yLabel = new QLabel("Y");

	xSpinBox->setFont(font);
	ySpinBox->setFont(font);
	xLabel->setFont(font);
	yLabel->setFont(font);

	xLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
	yLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

	auto box1 = new QWidget;
	auto layout1 = new QHBoxLayout;
	box1->setLayout(layout1);
	auto box2 = new QWidget;
	auto layout2 = new QHBoxLayout;
	box2->setLayout(layout2);

	layout1->addWidget(xLabel);
	layout1->addWidget(xSpinBox);
	layout2->addWidget(yLabel);
	layout2->addWidget(ySpinBox);

	layout->addWidget(box1);
	layout->addWidget(box2);

	connect(xSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		x = val;
		emit valueChanged(QVector2D(x, y));
	});

	connect(ySpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		y = val;
		emit valueChanged(QVector2D(x, y));
	});
}

Widget2D::~Widget2D()
{
}

void Widget2D::setValues(float xValue, float yValue)
{
	x = xValue;
	y = yValue;
	xSpinBox->setValue(xValue);
	ySpinBox->setValue(yValue);
}



Widget3D::Widget3D() : PropertyWidgetBase()
{
	xSpinBox = new WideRangeSpinBox;
	ySpinBox = new WideRangeSpinBox;
	zSpinBox = new WideRangeSpinBox;
	auto xLabel = new QLabel("X");
	auto yLabel = new QLabel("Y");
	auto zLabel = new QLabel("Z");
	x = y = z = 0;

	xSpinBox->setFont(font);
	ySpinBox->setFont(font);
	zSpinBox->setFont(font);
	xLabel->setFont(font);
	yLabel->setFont(font);
	zLabel->setFont(font);

	xLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
	yLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
	zLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

	layout->addWidget(xLabel);
	layout->addWidget(xSpinBox);
	layout->addWidget(yLabel);
	layout->addWidget(ySpinBox);
	layout->addWidget(zLabel);
	layout->addWidget(zSpinBox);

	connect(xSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		x = val;
		emit valueChanged(QVector3D(x, y, z));
	});

	connect(ySpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		y = val;
		emit valueChanged(QVector3D(x, y, z));
	});

	connect(zSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		z = val;
		emit valueChanged(QVector3D(x, y, z));
	});

}

Widget3D::~Widget3D()
{
}

void Widget3D::setValues(float xValue, float yValue, float zValue)
{
	x = xValue;
	y = yValue;
	z = zValue;
	xSpinBox->setValue(xValue);
	ySpinBox->setValue(yValue);
	zSpinBox->setValue(zValue);
}


Widget4D::Widget4D() : PropertyWidgetBase()
{ 
	x = y = z = w = 0;
	xSpinBox = new WideRangeSpinBox;
	ySpinBox = new WideRangeSpinBox;
	zSpinBox = new WideRangeSpinBox;
	wSpinBox = new WideRangeSpinBox;
	auto xLabel = new QLabel("X");
	auto yLabel = new QLabel("Y");
	auto zLabel = new QLabel("Z");
	auto wLabel = new QLabel("W");

	xSpinBox->setFont(font);
	ySpinBox->setFont(font);
	zSpinBox->setFont(font);
	wSpinBox->setFont(font);
	xLabel->setFont(font);
	yLabel->setFont(font);
	zLabel->setFont(font);
	wLabel->setFont(font);

	xLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
	yLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
	zLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
	wLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

	layout->addWidget(xLabel);
	layout->addWidget(xSpinBox);
	layout->addWidget(yLabel);
	layout->addWidget(ySpinBox);
	layout->addWidget(zLabel);
	layout->addWidget(zSpinBox);
	layout->addWidget(wLabel);
	layout->addWidget(wSpinBox);

	connect(xSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		x = val;
		emit valueChanged(QVector4D(x, y, z, w));
	});

	connect(ySpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		y = val;
		emit valueChanged(QVector4D(x, y, z, w));
	});

	connect(ySpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		y = val;
		emit valueChanged(QVector4D(x, y, z, w));
	});

	connect(zSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		z = val;
		emit valueChanged(QVector4D(x, y, z, w));
	});
}

Widget4D::~Widget4D()
{
}

void Widget4D::setXSpinBoxConnection(std::function<void(double val)> func)
{
	connect(xSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		x = val;
		func(val);
		emit valueChanged(QVector4D(x, y, z, w));
	});
}

void Widget4D::setYSpinBoxConnection(std::function<void(double val)> func)
{
	connect(ySpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		y = val;
		func(val);
		emit valueChanged(QVector4D(x, y, z, w));
	});
}

void Widget4D::setZSpinBoxConnection(std::function<void(double val)> func)
{
	connect(zSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		z = val;
		func(val);
		emit valueChanged(QVector4D(x, y, z, w));
	});
}


void Widget4D::setWSpinBoxConnection(std::function<void(double val)> func)
{
	connect(zSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		w = val;
		func(val);
		emit valueChanged(QVector4D(x, y, z, w));
	});
}

void Widget4D::setValues(float xValue, float yValue, float zValue, float wValue)
{
	x = xValue;
	y = yValue;
	z = zValue;
	w = wValue;
	xSpinBox->setValue(xValue);
	ySpinBox->setValue(yValue);
	zSpinBox->setValue(zValue);
	wSpinBox->setValue(wValue);
}


WidgetInt::WidgetInt() : PropertyWidgetBase()
{
	value = 0;
	spinBox = new WideRangeIntBox(this); 
	maxSpinBox = new WideRangeIntBox(this);
	minSpinBox = new WideRangeIntBox(this);
	stepSpinBox = new QSpinBox(this);

	auto label = new QLabel("Value");
	auto label1 = new QLabel("Min");
	auto label2 = new QLabel("Max");
	auto label3 = new QLabel("Step");

	label->setFont(font);
	label1->setFont(font);
	label2->setFont(font);
	label3->setFont(font);

	spinBox->setFont(font);
	maxSpinBox->setFont(font);
	minSpinBox->setFont(font);
	stepSpinBox->setFont(font);

	auto vbox = new QHBoxLayout;
	auto minbox = new QHBoxLayout;
	auto maxbox = new QHBoxLayout;
	auto stepbox = new QHBoxLayout;

	vbox->addWidget(label);
	vbox->addWidget(spinBox);
	minbox->addWidget(label2);
	minbox->addWidget(maxSpinBox);
	maxbox->addWidget(label1);
	maxbox->addWidget(minSpinBox);
	stepbox->addWidget(label3);
	stepbox->addWidget(stepSpinBox);

	auto wid = new QWidget;
	auto widLayout = new QVBoxLayout;
	wid->setLayout(widLayout);

	widLayout->addLayout(vbox);
	widLayout->addLayout(minbox);
	widLayout->addLayout(maxbox);
	widLayout->addLayout(stepbox);

	layout->addWidget(wid);

	connect(spinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int val) {
		value = val;
		emit valueChanged(value);
	});

	connect(minSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int val) {
		spinBox->setMinimum(val);
		emit minChanged(val);
	});
	connect(maxSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int val) {
		spinBox->setMaximum(val);
		emit maxChanged(val);

	});
	connect(stepSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int val) {
		spinBox->setSingleStep(val);
		emit stepChanged(val);
	});
}


WidgetInt::WidgetInt(LabelState state) : WidgetInt()
{
	this->state = state;
}

WidgetInt::~WidgetInt()
{
}

void WidgetInt::setIntSpinBoxConnection(std::function<void(int val)> func)
{
	connect(spinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int val) {
		value = val;
		func(val);
		emit valueChanged(value);
	});

	connect(minSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int val) {
		spinBox->setMinimum(val);
	});
	connect(maxSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int val) {
		spinBox->setMaximum(val);

	});
	connect(stepSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int val) {
		spinBox->setSingleStep(val);
	});

}



WidgetFloat::WidgetFloat() : PropertyWidgetBase()
{
	value = 0;
	floatSpinBox = new WideRangeSpinBox;
	maxSpinBox = new WideRangeSpinBox(this);
	minSpinBox = new WideRangeSpinBox(this);
	stepSpinBox = new QDoubleSpinBox(this);

	auto label = new QLabel("Value");
	auto label1 = new QLabel("Min");
	auto label2 = new QLabel("Max");
	auto label3 = new QLabel("Step");

	label->setFont(font);
	label1->setFont(font);
	label2->setFont(font);
	label3->setFont(font);

	floatSpinBox->setFont(font);
	maxSpinBox->setFont(font);
	minSpinBox->setFont(font);
	stepSpinBox->setFont(font);

	auto vbox = new QHBoxLayout;
	auto minbox = new QHBoxLayout;
	auto maxbox = new QHBoxLayout;
	auto stepbox = new QHBoxLayout;

	vbox->addWidget(label);
	vbox->addWidget(floatSpinBox);
	minbox->addWidget(label2);
	minbox->addWidget(maxSpinBox);
	maxbox->addWidget(label1);
	maxbox->addWidget(minSpinBox);
	stepbox->addWidget(label3);
	stepbox->addWidget(stepSpinBox);

	auto wid = new QWidget;
	auto widLayout = new QVBoxLayout;
	wid->setLayout(widLayout);

	widLayout->addLayout(vbox);
	widLayout->addLayout(minbox);
	widLayout->addLayout(maxbox);
	widLayout->addLayout(stepbox);

	layout->addWidget(wid);

	connect(floatSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		value = val;
		emit valueChanged(value);
	});

	connect(minSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		floatSpinBox->setMinimum(val);
		emit minChanged(val);
	});
	connect(maxSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		floatSpinBox->setMaximum(val);
		emit maxChanged(val);

	});
	connect(stepSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		floatSpinBox->setSingleStep(val);
		emit stepChanged(val);
	});
}


WidgetFloat::WidgetFloat(LabelState state) : WidgetFloat()
{
	this->state = state;
}

WidgetFloat::~WidgetFloat()
{
}

void WidgetFloat::setFloatSpinBoxConnection(std::function<void(double val)> func)
{
	connect(floatSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		value = val;
		func(val);
		emit valueChanged(value);
	});
}

WidgetTexture::WidgetTexture()
{
	auto wid = new QWidget;
	auto winHolder = new QVBoxLayout;
	winHolder->setContentsMargins(0, 0, 0, 0);
	wid->setLayout(winHolder);
	
	texture = new QPushButton();
	texture->setIconSize({ 360,145});
	texture->setMinimumSize(160, 146);	
	winHolder->addWidget(texture);
	
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(texture);
	setStyleSheet("background:rgba(0,0,0,0); color: rgba(250,250,250,.9); padding: 0px;");
	texture->setStyleSheet("background:rgba(0,0,0,0); border : 2px solid rgba(50,50,50,.3);");
}

WidgetTexture::~WidgetTexture()
{
}

