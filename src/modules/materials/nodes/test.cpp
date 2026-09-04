/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "irisgl/core/math/vec.h"
#include "test.h"
#include "../models/library.h"
#include "../core/texturemanager.h"
#include "../propertywidgets/vectorpropertywidget.h"
#include "ui/style/stylesheet.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QDebug>

SurfaceMasterNode::SurfaceMasterNode()
{
	title = "Surface Material";
	typeName = "Material";
	setNodeType(NodeCategory::Input);
	addInputSocket(new Vector3SocketModel("Diffuse","vec3(1.0,1.0,1.0)"));
	addInputSocket(new Vector3SocketModel("Specular"));
	addInputSocket(new FloatSocketModel("Shininess"));
	addInputSocket(new Vector3SocketModel("Normal", "vec3(0.0, 0.0, 1.0)"));
	addInputSocket(new Vector3SocketModel("Ambient"));
	addInputSocket(new Vector3SocketModel("Emission"));
	addInputSocket(new FloatSocketModel("Alpha", "1.0f"));
	addInputSocket(new FloatSocketModel("Alpha Cutoff"));
	addInputSocket(new Vector3SocketModel("Vertex Offset"));
	addInputSocket(new FloatSocketModel("Vertex Extrusion"));
}


FloatNodeModel::FloatNodeModel() :
	NodeModel()
{
	setNodeType(NodeCategory::Constants);

	typeName = "float";
	title = "Float";

	// add output socket
	valueSock = new FloatSocketModel("value");
	addOutputSocket(valueSock);

	// compact in-node editor: one number box in the node BODY, on the
	// "value" socket's row (owner request - values live next to the socket
	// text inside the box, not in the header)
	valueBox = new QDoubleSpinBox;
	valueBox->setRange(-99999.0, 99999.0);
	valueBox->setDecimals(2);
	valueBox->setSingleStep(0.1);
	valueBox->setValue(0.0);
	valueBox->setAlignment(Qt::AlignCenter);
	valueBox->setFixedSize(58, 20);
	valueBox->setKeyboardTracking(false);
	valueBox->setStyleSheet(
		"QDoubleSpinBox{border: 1px solid rgba(200, 200, 200, .4); border-radius: 2px;"
		" padding: 0 2px; background: rgba(0, 0, 0, 0.35); color: rgba(250,250,250,1); font-size: 11px;}"
		"QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}");
	this->widget = valueBox;
	this->widgetBesideSockets = true;

	connect(valueBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		editTextChanged(QString::number(val));
	});

	// keep the socket's stored value in step with the editor's default
	valueSock->setValue("0");
}

void FloatNodeModel::editTextChanged(const QString& text)
{
	valueSock->setValue(text);
	emit valueChanged(this, 0);
}


QJsonValue FloatNodeModel::serializeWidgetValue(int widgetIndex)
{
	return valueSock->getValue().toDouble();
}

void FloatNodeModel::deserializeWidgetValue(QJsonValue val, int widgetIndex)
{
	auto value = val.toDouble();
	valueSock->setValue(QString("%1").arg(value));

	valueBox->blockSignals(true);
	valueBox->setValue(value);
	valueBox->blockSignals(false);
}


VectorMultiplyNode::VectorMultiplyNode()
{
	setNodeType(NodeCategory::Input);
	title = "Vector Multiply";
	typeName = "vectorMultiply";

	addInputSocket(new Vector4SocketModel("A"));
	addInputSocket(new Vector4SocketModel("B"));
	addOutputSocket(new Vector4SocketModel("Result"));
}


WorldNormalNode::WorldNormalNode()
{
	setNodeType(NodeCategory::Input);

	title = "World Normal";
	typeName = "worldNormal";

	addOutputSocket(new Vector3SocketModel("World Normal", "v_normal"));
}


LocalNormalNode::LocalNormalNode()
{
	setNodeType(NodeCategory::Input);

	title = "Local Normal";
	typeName = "localNormal";

	addOutputSocket(new Vector3SocketModel("Local Normal", "v_locaNormal"));
}


TimeNode::TimeNode()
{
	setNodeType(NodeCategory::Input);

	title = "Time";
	typeName = "time";

	addOutputSocket(new FloatSocketModel("Seconds", "u_time"));
}


SineNode::SineNode()
{
	setNodeType(NodeCategory::Math);

	title = "Sine";
	typeName = "sine";

	addInputSocket(new Vector3SocketModel("Value"));
	addOutputSocket(new Vector3SocketModel("Result"));
}


MakeColorNode::MakeColorNode() {
	setNodeType(NodeCategory::Utility);


	title = "Make Color";
	typeName = "makeColor";

	addInputSocket(new FloatSocketModel("R"));
	addInputSocket(new FloatSocketModel("G"));
	addInputSocket(new FloatSocketModel("B"));

	addOutputSocket(new Vector4SocketModel("Color"));
}


TextureCoordinateNode::TextureCoordinateNode()
{
	setNodeType(NodeCategory::Input);

	title = "Texture Coordinate";
	typeName = "texCoords";

	combo = new QComboBox();
	combo->addItem("TexCoord0");
	combo->addItem("TexCoord1");
	combo->addItem("TexCoord2");
	combo->addItem("TexCoord3");

	connect(combo, &QComboBox::currentTextChanged,
		this, &TextureCoordinateNode::comboTextChanged);

	combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	combo->setStyleSheet(StyleSheet::QComboBox());

	auto containerWidget = new QWidget();
	auto layout = new QVBoxLayout;
	containerWidget->setMaximumSize(170, 55);
	containerWidget->setLayout(layout);
	containerWidget->setStyleSheet("background:rgba(0,0,0,0);");
	layout->addWidget(combo);
	layout->setSpacing(0);

	this->widget = containerWidget;

	addOutputSocket(new Vector2SocketModel("UV"));
	uv = "v_texCoord";
}


void TextureCoordinateNode::comboTextChanged(const QString& text)
{
	if (text == "TexCoord0") {
		uv = "v_texCoord";
	}
	else if (text == "TexCoord1") {
		uv = "v_texCoord1";
	}
	else if (text == "TexCoord2") {
		uv = "v_texCoord2";
	}
	else if (text == "TexCoord3") {
		uv = "v_texCoord3";
	}

	emit valueChanged(this, 0);
}


TextureSamplerNode::TextureSamplerNode()
{
	setNodeType(NodeCategory::Input);


	title = "Sample Texture";
	typeName = "textureSampler";

	addInputSocket(new TextureSocketModel("Texture"));
	addInputSocket(new Vector2SocketModel("UV","v_texCoord"));
	addOutputSocket(new Vector4SocketModel("RGBA"));
}


// (PropertyNode / TexturePropertyNode retired 2026-08-31, §3b — load-time
// migration in NodeGraph::deserialize turns their instances into real
// constant/texture nodes)

TextureNode::TextureNode()
{
	setNodeType(NodeCategory::Input);
	title = "Texture";
	typeName = "texture";

	auto widget = new QWidget;
	auto layout = new QVBoxLayout;
	widget->setLayout(layout);
	widget->setMinimumSize(170, 155);
	texture = new QPushButton();
	texture->setIconSize(QSize(145, 145));
	texture->setMinimumSize(160, 146);
	

	auto label = new QLabel;

	auto pushLayout = new QVBoxLayout;
	texture->setLayout(pushLayout);
	//pushLayout->addWidget(label);

	layout->setContentsMargins(3, 0, 3, 2);
	layout->addWidget(texture);
	this->widget = widget;

	graphTexture = nullptr;
	connect(texture, &QPushButton::clicked, [=]() {
		auto filename = QFileDialog::getOpenFileName();
		if (filename.isEmpty()) return; // dialog cancelled

		setTexturePath(filename);

		// the picker is the node's inline editor: choosing an image must
		// reach the evaluator/preview like any other value edit
		emit valueChanged(this, 0);
	});
	
	widget->setStyleSheet("background:rgba(0,0,0,0); color: rgba(250,250,250,.9);");
	texture->setStyleSheet("background:rgba(0,0,0,0); border : 2px solid rgba(50,50,50,.3);");


	addOutputSocket(new TextureSocketModel("texture"));

}

QString TextureNode::getTexturePath() const
{
	if (graphTexture == nullptr) return QString();
	return graphTexture->path;
}

void TextureNode::setTexturePath(const QString& path)
{
	texture->setIcon(QIcon(path));

	if (graphTexture != nullptr) {
		TextureManager::getSingleton()->removeTexture(graphTexture);
		delete graphTexture;
	}

	graphTexture = TextureManager::getSingleton()->createTexture();
	graphTexture->path = path;
}

QString TextureNode::getTextureGuid() const
{
	if (graphTexture == nullptr) return QString();
	return graphTexture->guid;
}

void TextureNode::setTextureGuid(const QString& guid)
{
	if (graphTexture != nullptr) {
		TextureManager::getSingleton()->removeTexture(graphTexture);
		delete graphTexture;
		graphTexture = nullptr;
	}

	// resolves the path through the project database when one is set;
	// otherwise keeps the guid with an unresolved path
	graphTexture = TextureManager::getSingleton()->loadTextureFromGuid(guid);
	graphTexture->guid = guid;

	if (!graphTexture->path.isEmpty() && QFileInfo::exists(graphTexture->path))
		texture->setIcon(QIcon(graphTexture->path));
}

QJsonValue TextureNode::serializeWidgetValue(int widgetIndex)
{
	// guid-backed textures serialize their guid (durable across machines and
	// what DB dependency tracking wants); plain file picks keep the path
	if (graphTexture != nullptr && !graphTexture->guid.isEmpty())
		return graphTexture->guid;
	return getTexturePath();
}

void TextureNode::deserializeWidgetValue(QJsonValue val, int widgetIndex)
{
	// old graphs carry "" here (the path was never saved before) — leave
	// the node empty in that case, exactly as those files loaded before
	auto value = val.toString();
	if (value.isEmpty()) return;
	if (QFileInfo::exists(value))
		setTexturePath(value);
	else
		setTextureGuid(value); // an asset guid (or an app-relative preset image)
}


PulsateNode::PulsateNode()
{
	setNodeType(NodeCategory::Utility);

	title = "Pulsate";
	typeName = "pulsate";

	addInputSocket(new FloatSocketModel("Speed", "1.0"));
	addOutputSocket(new FloatSocketModel("Result"));
}


PannerNode::PannerNode()
{
	setNodeType(NodeCategory::Input);

	title = "Panner";
	typeName = "panner";

	addInputSocket(new Vector2SocketModel("UV", "v_texCoord"));
	addInputSocket(new Vector2SocketModel("Speed", "vec2(1.0,1.0)"));
	addInputSocket(new FloatSocketModel("Time", "u_time"));
	addOutputSocket(new Vector2SocketModel("Result"));
}


NormalIntensityNode::NormalIntensityNode()
{
	setNodeType(NodeCategory::Input);

	title = "Normal Intensity";
	typeName = "normalintensity";

	addInputSocket(new Vector3SocketModel("Normal", "vec3(0.0, 0.0, 1.0)"));
	addInputSocket(new FloatSocketModel("Intensity", "1.0"));
	addOutputSocket(new Vector3SocketModel("Result"));
}


Vector2Node::Vector2Node()
{
	setNodeType(NodeCategory::Constants);
	title = "Vector2";
	typeName = "vector2";

	x = y = 0;

	auto wid = new QWidget;
	auto layout = new QHBoxLayout;
	wid->setLayout(layout);
	wid->setFixedWidth(158); // fixed: the expanding boxes otherwise overrun the 170px card
	layout->setContentsMargins(4, 2, 4, 2);
	layout->setSpacing(3);

	xSpinBox = new QDoubleSpinBox;
	ySpinBox = new QDoubleSpinBox;
	for (auto box : { xSpinBox, ySpinBox }) {
		box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		box->setAlignment(Qt::AlignCenter);
		box->setRange(-99999.0, 99999.0);
		box->setDecimals(2);
		box->setSingleStep(0.1);
		box->setFixedHeight(20);
		box->setKeyboardTracking(false);
	}

	layout->addWidget(xSpinBox);
	layout->addWidget(ySpinBox);

	widget = wid;

	connect(xSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		x = val;
		value = iris::Vec2(x, y);
		emit valueChanged(this, 0);

	});

	connect(ySpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		y = val;
		value = iris::Vec2(x, y);
		emit valueChanged(this, 0);
	});

	widget->setStyleSheet(
		"QDoubleSpinBox{border: 2px solid rgba(200, 200, 200, .4); padding: 2px; background: rgba(0, 0, 0, 0.2);}"
		"QWidget{ background: rgba(0,0,0,0); color: rgba(250,250,250,1); }"
		"QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}"
	);


	addOutputSocket(new Vector2SocketModel("Result"));

}


QJsonValue Vector2Node::serializeWidgetValue(int widgetIndex)
{
	QJsonObject obj;
	obj["x"] = value.x();
	obj["y"] = value.y();

	return obj;
}

void Vector2Node::deserializeWidgetValue(QJsonValue val, int widgetIndex)
{
	auto obj = val.toObject();

	value.setX(x = obj["x"].toDouble());
	value.setY(y = obj["y"].toDouble());

	xSpinBox->setValue(value.x());
	ySpinBox->setValue(value.y());
}


Vector3Node::Vector3Node()
{
	setNodeType(NodeCategory::Constants);
	title = "Vector3";
	typeName = "vector3";

	x = y = z = 0;

	auto wid = new QWidget;
	auto layout = new QHBoxLayout;
	wid->setLayout(layout);
	wid->setFixedWidth(158);
	layout->setContentsMargins(4, 2, 4, 2);
	layout->setSpacing(3);

	xSpinBox = new QDoubleSpinBox;
	ySpinBox = new QDoubleSpinBox;
	zSpinBox = new QDoubleSpinBox;
	for (auto box : { xSpinBox, ySpinBox, zSpinBox }) {
		box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		box->setAlignment(Qt::AlignCenter);
		box->setRange(-99999.0, 99999.0);
		box->setDecimals(2);
		box->setSingleStep(0.1);
		box->setFixedHeight(20);
		box->setKeyboardTracking(false);
	}

	layout->addWidget(xSpinBox);
	layout->addWidget(ySpinBox);
	layout->addWidget(zSpinBox);

	widget = wid;

	connect(xSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		x = val;
		value = iris::Vec3(x, y, z);
		emit valueChanged(this, 0);

	});

	connect(ySpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		y = val;
		value = iris::Vec3(x, y, z);
		emit valueChanged(this, 0);
	});

	connect(zSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		z = val;
		value = iris::Vec3(x, y, z);
		emit valueChanged(this, 0);
	});

	widget->setStyleSheet(
		"QDoubleSpinBox{border: 2px solid rgba(200, 200, 200, .4); padding: 2px; background: rgba(0, 0, 0, 0.2);}"
		"QWidget{ background: rgba(0,0,0,0); color: rgba(250,250,250,1); }"
		"QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}"
	);


	addOutputSocket(new Vector3SocketModel("Result"));
}


QJsonValue Vector3Node::serializeWidgetValue(int widgetIndex)
{
	QJsonObject obj;
	obj["x"] = value.x();
	obj["y"] = value.y();
	obj["z"] = value.z();

	return obj;
}

void Vector3Node::deserializeWidgetValue(QJsonValue val, int widgetIndex)
{
	auto obj = val.toObject();

	value.setX(x = obj["x"].toDouble());
	value.setY(y = obj["y"].toDouble());
	value.setZ(z = obj["z"].toDouble());

	xSpinBox->setValue(value.x());
	ySpinBox->setValue(value.y());
	zSpinBox->setValue(value.z());
}

Vector4Node::Vector4Node()
{
	setNodeType(NodeCategory::Constants);
	title = "Vector4";
	typeName = "vector4";

	x = y = z = w = 0;

	auto wid = new QWidget;
	auto layout = new QHBoxLayout;
	wid->setLayout(layout);
	wid->setFixedWidth(158);
	layout->setContentsMargins(4, 2, 4, 2);
	layout->setSpacing(3);

	xSpinBox = new QDoubleSpinBox;
	ySpinBox = new QDoubleSpinBox;
	zSpinBox = new QDoubleSpinBox;
	wSpinBox = new QDoubleSpinBox;
	for (auto box : { xSpinBox, ySpinBox, zSpinBox, wSpinBox }) {
		box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		box->setAlignment(Qt::AlignCenter);
		box->setRange(-99999.0, 99999.0);
		box->setDecimals(2);
		box->setSingleStep(0.1);
		box->setFixedHeight(20);
		box->setKeyboardTracking(false);
	}

	layout->addWidget(xSpinBox);
	layout->addWidget(ySpinBox);
	layout->addWidget(zSpinBox);
	layout->addWidget(wSpinBox);

	widget = wid;

	connect(xSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		x = val;
		value = iris::Vec4(x, y, z, w);
		emit valueChanged(this, 0);

	});

	connect(ySpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		y = val;
		value = iris::Vec4(x, y, z, w);
		emit valueChanged(this, 0);
	});

	connect(zSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		z = val;
		value = iris::Vec4(x, y, z, w);
		emit valueChanged(this, 0);
	});

	// was wired to zSpinBox twice; w never updated from its own box
	connect(wSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		w = val;
		value = iris::Vec4(x, y, z, w);
		emit valueChanged(this, 0);
	});

	widget->setStyleSheet(
		"QDoubleSpinBox{border: 2px solid rgba(200, 200, 200, .4); padding: 2px; background: rgba(0, 0, 0, 0.2);}"
		"QWidget{ background: rgba(0,0,0,0); color: rgba(250,250,250,1); }"
		"QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}");


	addOutputSocket(new Vector4SocketModel("Result"));
}


QJsonValue Vector4Node::serializeWidgetValue(int widgetIndex)
{
	QJsonObject obj;
	obj["x"] = value.x();
	obj["y"] = value.y();
	obj["z"] = value.z();
	obj["w"] = value.w();

	return obj;
}

void Vector4Node::deserializeWidgetValue(QJsonValue val, int widgetIndex)
{
	auto obj = val.toObject();

	value.setX(x = obj["x"].toDouble());
	value.setY(y = obj["y"].toDouble());
	value.setZ(z = obj["z"].toDouble());
	value.setW(w = obj["w"].toDouble());

	xSpinBox->setValue(value.x());
	ySpinBox->setValue(value.y());
	zSpinBox->setValue(value.z());
	wSpinBox->setValue(value.w());
}


#if(EFFECT_BUILD_AS_LIB)
ColorPickerNode::ColorPickerNode()
{
	setNodeType(NodeCategory::Constants);
	title = "Color";
	typeName = "color";

	// in-node editor: the swatch lives INSIDE the node body, like the
	// texture node's preview (owner request); clicking it pops the color
	// dialog (ColorPickerWidget paints itself)
	colorWidget = new ColorPickerWidget();
	colorWidget->setFixedSize(120, 40);
	this->widget = colorWidget;
	connect(colorWidget, &ColorPickerWidget::onColorChanged, [=](QColor color) {
		emit valueChanged(this, 0);
	});

	addOutputSocket(new Vector4SocketModel("R G B A"));
	addOutputSocket(new FloatSocketModel("R"));
	addOutputSocket(new FloatSocketModel("G"));
	addOutputSocket(new FloatSocketModel("B"));
	addOutputSocket(new FloatSocketModel("A"));
}


QJsonValue ColorPickerNode::serializeWidgetValue(int widgetIndex)
{
	auto col = colorWidget->getColor();

	QJsonObject obj;
	obj["r"] = col.redF();
	obj["g"] = col.greenF();
	obj["b"] = col.blueF();
	obj["a"] = col.alphaF();

	return obj;
}

void ColorPickerNode::deserializeWidgetValue(QJsonValue val, int widgetIndex)
{
	auto obj = val.toObject();

	QColor col;
	col.setRedF(obj["r"].toDouble());
	col.setGreenF(obj["g"].toDouble());
	col.setBlueF(obj["b"].toDouble());
	col.setAlphaF(obj["a"].toDouble());

	colorWidget->setColor(col);
}
#endif