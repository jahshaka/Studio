/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "test.h"
#include "../models/library.h"
#include "../core/texturemanager.h"
#include "../propertywidgets/vectorpropertywidget.h"
#include "ui/style/stylesheet.h"
#include <QFileDialog>
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

void SurfaceMasterNode::process(ModelContext* ctx)
{
	QString code = "";
	auto context = (ShaderContext*)ctx;
	//context->addCodeChunk(this, "void surface(inout Material material){\n");

	auto diffVar = this->getValueFromInputSocket(0);
	auto specVar = this->getValueFromInputSocket(1);
	auto shininessVar = this->getValueFromInputSocket(2);
	auto normVar = this->getValueFromInputSocket(3);
	auto ambientVar = this->getValueFromInputSocket(4);
	auto emissionVar = this->getValueFromInputSocket(5);
	auto alphaVar = this->getValueFromInputSocket(6);
	auto alphaCutoffVar = this->getValueFromInputSocket(7);

	code += "material.diffuse = " + diffVar + ";\n";
	code += "material.specular = " + specVar + ";\n";
	code += "material.shininess = " + shininessVar + ";\n";
	code += "material.normal = " + normVar + ";\n";
	code += "material.ambient = " + ambientVar + ";\n";
	code += "material.emission = " + emissionVar + ";\n";
	code += "material.alpha = " + alphaVar + ";\n";
	code += "material.alphaCutoff = " + alphaCutoffVar + ";\n";
	//context->addCodeChunk(this, "material.diffuse = " + diffVar + ";");

	//context->clear();
	context->addCodeChunk(this, code);
	
}

FloatNodeModel::FloatNodeModel() :
	NodeModel()
{
	setNodeType(NodeCategory::Constants);

	auto wid = new QWidget;
	auto label = new QLabel(" ");
	auto layout = new QVBoxLayout;
	auto layoutH = new QHBoxLayout;
	auto layoutH1 = new QHBoxLayout;

	auto min = new QDoubleSpinBox;
	auto max = new QDoubleSpinBox;
	auto stepBox = new QDoubleSpinBox;
	auto valueBox = new QDoubleSpinBox;
	wid->setMaximumWidth(164);

	min->setValue(0.0);
	max->setValue(1.0);
	stepBox->setValue(0.1);

	double step = 0.1;
	double upModifier = (step * 1000);

	auto slider = new QSlider(Qt::Horizontal);
	slider->setMinimum(min->value());
	slider->setMaximum(max->value()*upModifier);
	slider->setSingleStep(stepBox->value());

	min->setAlignment(Qt::AlignCenter);
	max->setAlignment(Qt::AlignCenter);
	valueBox->setAlignment(Qt::AlignCenter);
	slider->setMaximumWidth(150);
	

	wid->setLayout(layout);

	layoutH->addWidget(min);
	layoutH->addWidget(max);
	layoutH->addWidget(valueBox);

	auto holdem = new QWidget;
	holdem->setLayout(layoutH);

	auto minl = new QLabel("min");
	auto maxl = new QLabel(" max");
	auto vall = new QLabel("value");

	minl->setAlignment(Qt::AlignCenter);
	maxl->setAlignment(Qt::AlignCenter);
	vall->setAlignment(Qt::AlignCenter);

	auto textHolder = new QWidget;
	textHolder->setLayout(layoutH1);
	layoutH1->setContentsMargins(10, 0, 10, 0);


	layoutH1->addWidget(minl);
		layoutH1->addWidget(maxl);
		layoutH1->addWidget(vall);

	layout->addWidget(textHolder);

	layout->addWidget(holdem);
	layout->addWidget(slider);
	
	this->widget = wid;

	typeName = "float";
	title = "Float";

	// add output socket
	valueSock = new FloatSocketModel("value");
	addOutputSocket(valueSock);
	

	connect(min, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		slider->setMinimum(val);
	});
	connect(max, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		slider->setMaximum(val*upModifier);
	}); 
	connect(valueBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		slider->blockSignals(true);
		slider->setValue(val*step);
		slider->blockSignals(false);
	});
	connect(stepBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [&step, &upModifier](double val) {
		step = val;
		upModifier = (step * 100);
	});
	connect(slider, &QSlider::valueChanged, [=](int value) {
		editTextChanged(QString::number(value/upModifier, 'g', 2));
		valueBox->blockSignals(true);
		valueBox->setValue(qreal(value/upModifier));
		valueBox->blockSignals(false);

	});


	wid->setStyleSheet(""
		"QWidget{ background: rgba(0,0,0,0); color: rgba(250,250,250,1); }"

		"");
	min->setStyleSheet(
		"QDoubleSpinBox{border: 2px solid rgba(200, 200, 200, .4); padding: 2px; background: rgba(0, 0, 0, 0.0);}"
		"QWidget{ background: rgba(0,0,0,0); color: rgba(250,250,250,1); }"
		"QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}");
	max->setStyleSheet(min->styleSheet());
	stepBox->setStyleSheet(min->styleSheet());
	valueBox->setStyleSheet(min->styleSheet());

	slider->setStyleSheet(
		"QSlider::sub-page {	border: 0px solid transparent;	height: 2px;	background: #3498db;	margin: 2px 0;}"
		"QSlider::groove:horizontal {    border: 0px solid transparent;    height: 4px;   background: #1e1e1e;   margin: 2px 0;}"
		"QSlider::handle:horizontal {    background-color: #CCC;    width: 12px;    border: 1px solid #1e1e1e;    margin: -5px 0px;   border-radius:7px;}"
		"QSlider::handle:horizontal:pressed {    background-color: #AAA;    width: 12px;   border: 1px solid #1e1e1e;    margin: -5px 0px;    border-radius: 7px;}"
		"QSlider::handle:horizontal:disabled {    background-color: #bbbbbb;    width: 12px;    border: 0px solid transparent;    margin: -1px -1px;    border-radius: 4px;}"
		"QSlider::groove:vertical {background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,stop: 0 white, stop: 1 black); border-radius: 2px; width: 13px; }"
		" QSlider::handle:vertical {height: 3px; width:1px; margin: -2px 0px; background: rgba(50,148,213,0.9); }"
		" QSlider::add-page:vertical, QSlider::sub-page:vertical {background: rgba(0,0,0,0); border-radius: 1px;}"
	);

}

void FloatNodeModel::editTextChanged(const QString& text)
{
	valueSock->setValue(text);
	emit valueChanged(this, 0);
}

void FloatNodeModel::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = " + valueSock->getValue() + ";";
	valueSock->setVarName(valueSock->getValue());
}

QJsonValue FloatNodeModel::serializeWidgetValue(int widgetIndex)
{
	return valueSock->getValue().toDouble();
}

void FloatNodeModel::deserializeWidgetValue(QJsonValue val, int widgetIndex)
{
	auto value = val.toDouble();
	valueSock->setValue(QString("%1").arg(value));
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

void VectorMultiplyNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto valB = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = " + valA + " * " + valB + ";";
	ctx->addCodeChunk(this, code);
}

WorldNormalNode::WorldNormalNode()
{
	setNodeType(NodeCategory::Input);

	title = "World Normal";
	typeName = "worldNormal";

	addOutputSocket(new Vector3SocketModel("World Normal", "v_normal"));
}

void WorldNormalNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = v_normal;";
	outSockets[0]->setVarName("v_normal");
}

LocalNormalNode::LocalNormalNode()
{
	setNodeType(NodeCategory::Input);

	title = "Local Normal";
	typeName = "localNormal";

	addOutputSocket(new Vector3SocketModel("Local Normal", "v_locaNormal"));
}

void LocalNormalNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto res = this->getOutputSocketVarName(0);

	outSockets[0]->setVarName("v_localNormal");
}

TimeNode::TimeNode()
{
	setNodeType(NodeCategory::Input);

	title = "Time";
	typeName = "time";

	addOutputSocket(new FloatSocketModel("Seconds", "u_time"));
}

void TimeNode::process(ModelContext* context)
{
	outSockets[0]->setVarName("u_time");
}

SineNode::SineNode()
{
	setNodeType(NodeCategory::Math);

	title = "Sine";
	typeName = "sine";

	addInputSocket(new Vector3SocketModel("Value"));
	addOutputSocket(new Vector3SocketModel("Result"));
}

void SineNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = sin(" + valA + ");";
	ctx->addCodeChunk(this, code);
}



MakeColorNode::MakeColorNode() {
	setNodeType(NodeCategory::Utility);


	title = "Color";
	typeName = "makeColor";

	addInputSocket(new FloatSocketModel("R"));
	addInputSocket(new FloatSocketModel("G"));
	addInputSocket(new FloatSocketModel("B"));

	addOutputSocket(new Vector4SocketModel("Color"));
}

void MakeColorNode::process(ModelContext *context)
{
	auto ctx = (ShaderContext*)context;
	auto valr = this->getValueFromInputSocket(0);
	auto valg = this->getValueFromInputSocket(1);
	auto valb = this->getValueFromInputSocket(2);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = vec4(" + valr + "," + valg + "," + valb + ", 1);";
	ctx->addCodeChunk(this, code);
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

void TextureCoordinateNode::process(ModelContext* context)
{
	outSockets[0]->setVarName(uv);
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

void TextureSamplerNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;

	auto tex = this->getValueFromInputSocket(0);
	auto uv = this->getValueFromInputSocket(1);
	auto rgba = this->getOutputSocketVarName(0);

	QString code = "";

	if (tex.isEmpty())
		code = rgba + " = vec4(0.0,0.0,0.0,0.0);";
	else
		code = rgba + " = texture("+tex+","+uv+");";

	ctx->addCodeChunk(this, code);
}

PropertyNode::PropertyNode()
{
	this->typeName = "property";
	setNodeType(NodeCategory::Input);
}

// doesnt own property
void PropertyNode::setProperty(Property* property)
{
	this->prop = property;
	this->title = property->displayName;

	// add output based on property type
	switch (property->type) {
	case PropertyType::Int: {
		this->addOutputSocket(new FloatSocketModel("int"));
	}
		break;
	case PropertyType::Float:
		this->addOutputSocket(new FloatSocketModel("float"));
		break;

	case PropertyType::Vec2:
		this->addOutputSocket(new Vector2SocketModel("vector2"));
		break;

	case PropertyType::Vec3:
		this->addOutputSocket(new Vector3SocketModel("vector3"));
		break;

	case PropertyType::Vec4:
		this->addOutputSocket(new Vector4SocketModel("vector4"));
		break;

	case PropertyType::Texture:
		this->addOutputSocket(new TextureSocketModel("texture"));
		this->addOutputSocket(new Vector4SocketModel("rgba"));
		this->addOutputSocket(new Vector3SocketModel("normal"));
		this->addInputSocket(new Vector2SocketModel("uv","v_texCoord"));
		
		break;

	default:
		//todo: throw error or something
		Q_ASSERT(false);
		break;
	}
}

QJsonValue PropertyNode::serializeWidgetValue(int widgetIndex)
{
	if (this->prop) {
		return this->prop->id;
	}

	return "";
}

void PropertyNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	ctx->addUniform(prop->getUniformString());

	if (this->outSockets.count() > 0)
		outSockets[0]->setVarName(prop->getUniformName());

	if (prop->type == PropertyType::Texture) {
		auto uv = this->getValueFromInputSocket(0);
		auto rgba = this->getOutputSocketVarName(1);
		auto normal = this->getOutputSocketVarName(2);

		QString code = "";
		code += rgba + " = texture(" + prop->getUniformName() + "," + uv + ");\n";
		//if (this->outSockets[2]->hasConnection())
			code += normal + " = " + rgba + ".xyz * vec3(2) - vec3(1);";
		ctx->addCodeChunk(this, code);
	}
}


TexturePropertyNode::TexturePropertyNode()
{

}

// doesnt own property
void TexturePropertyNode::setProperty(Property* property)
{

}

QJsonValue TexturePropertyNode::serializeWidgetValue(int widgetIndex)
{
	return QJsonValue();
}

void TexturePropertyNode::process(ModelContext* context)
{

}

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
		QIcon icon(filename);
		texture->setIcon(icon);

		if (graphTexture != nullptr) {
			TextureManager::getSingleton()->removeTexture(graphTexture);
			delete graphTexture;
		}

		graphTexture = TextureManager::getSingleton()->createTexture();
		graphTexture->path = filename;


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

void TextureNode::process(ModelContext * context)
{
	auto ctx = (ShaderContext*)context;
	auto texName = this->getOutputSocketVarName(0);
	ctx->addUniform("uniform sampler2D "+texName+"");
	if (graphTexture != nullptr) {
		graphTexture->uniformName = texName;
	}
}

PulsateNode::PulsateNode()
{
	setNodeType(NodeCategory::Utility);

	title = "Pulsate";
	typeName = "pulsate";

	addInputSocket(new FloatSocketModel("Speed", "1.0"));
	addOutputSocket(new FloatSocketModel("Result"));
}

void PulsateNode::process(ModelContext * context)
{
	auto ctx = (ShaderContext*)context;
	auto valA = this->getValueFromInputSocket(0);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = sin(u_time *" + valA + ") * 0.5 + 0.5;";
	ctx->addCodeChunk(this, code);
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

void PannerNode::process(ModelContext * context)
{
	auto ctx = (ShaderContext*)context;
	auto uv = this->getValueFromInputSocket(0);
	auto speed = this->getValueFromInputSocket(1);
	auto time = this->getValueFromInputSocket(2);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = " + uv + " + " + speed + " * "+ time + ";";
	ctx->addCodeChunk(this, code);
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

void NormalIntensityNode::process(ModelContext* context)
{
	auto ctx = (ShaderContext*)context;
	auto normal = this->getValueFromInputSocket(0);
	auto intensity = this->getValueFromInputSocket(1);
	auto res = this->getOutputSocketVarName(0);

	auto code = res + " = normalize(mix(vec3(0,0,1)," + normal + "," + intensity + "));";
	ctx->addCodeChunk(this, code);
}


Vector2Node::Vector2Node()
{
	setNodeType(NodeCategory::Constants);
	title = "Vector 2 Node";
	typeName = "vector2";

	x = y = 0;

	auto wid = new QWidget;
	auto layout = new QHBoxLayout;
	wid->setLayout(layout);
	wid->setMaximumWidth(164);

	xSpinBox = new QDoubleSpinBox;
	ySpinBox = new QDoubleSpinBox;
	xSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	ySpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	xSpinBox->setAlignment(Qt::AlignCenter);
	ySpinBox->setAlignment(Qt::AlignCenter);

	layout->addWidget(xSpinBox);
	layout->addWidget(ySpinBox);

	widget = wid;

	connect(xSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		x = val;
		value = QVector2D(x, y);
		emit valueChanged(this, 0);

	});

	connect(ySpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		y = val;
		value = QVector2D(x, y);
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

void Vector2Node::process(ModelContext * context)
{
	auto ctx = (ShaderContext*)context;
	outSockets[0]->setVarName("vec2(" + QString::number(value.x()) + "," + QString::number(value.y()) + ")");
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
	title = "Vector 3 Node";
	typeName = "vector3";

	x = y = z = 0;

	auto wid = new QWidget;
	auto layout = new QHBoxLayout;
	wid->setLayout(layout);
	wid->setMaximumWidth(164);

	xSpinBox = new QDoubleSpinBox;
	ySpinBox = new QDoubleSpinBox;
	zSpinBox = new QDoubleSpinBox;
	xSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	ySpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	zSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	xSpinBox->setAlignment(Qt::AlignCenter);
	ySpinBox->setAlignment(Qt::AlignCenter);
	zSpinBox->setAlignment(Qt::AlignCenter);

	layout->addWidget(xSpinBox);
	layout->addWidget(ySpinBox);
	layout->addWidget(zSpinBox);

	widget = wid;

	connect(xSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		x = val;
		value = QVector3D(x, y, z);
		emit valueChanged(this, 0);

	});

	connect(ySpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		y = val;
		value = QVector3D(x, y, z);
		emit valueChanged(this, 0);
	});

	connect(zSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		z = val;
		value = QVector3D(x, y, z);
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

void Vector3Node::process(ModelContext * context)
{
	auto ctx = (ShaderContext*)context;
	outSockets[0]->setVarName("vec3(" + QString::number(value.x()) + "," + QString::number(value.y()) + "," + QString::number(value.z()) + ")");
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
	title = "Vector 4 Node";
	typeName = "vector4";

	x = y = z = w = 0;

	auto wid = new QWidget;
	auto layout = new QHBoxLayout;
	wid->setLayout(layout);
	wid->setMaximumWidth(154);

	xSpinBox = new QDoubleSpinBox;
	ySpinBox = new QDoubleSpinBox;
	zSpinBox = new QDoubleSpinBox;
	wSpinBox = new QDoubleSpinBox;
	xSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	ySpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	zSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	wSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	xSpinBox->setAlignment(Qt::AlignCenter);
	ySpinBox->setAlignment(Qt::AlignCenter);
	zSpinBox->setAlignment(Qt::AlignCenter);
	wSpinBox->setAlignment(Qt::AlignCenter);

	layout->addWidget(xSpinBox);
	layout->addWidget(ySpinBox);
	layout->addWidget(zSpinBox);
	layout->addWidget(wSpinBox);

	widget = wid;

	connect(xSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		x = val;
		value = QVector4D(x, y, z, w);
		emit valueChanged(this, 0);

	});

	connect(ySpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		y = val;
		value = QVector4D(x, y, z, w);
		emit valueChanged(this, 0);
	});

	connect(zSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		z = val;
		value = QVector4D(x, y, z, w);
		emit valueChanged(this, 0);
	});

	connect(zSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double val) {
		z = val;
		value = QVector4D(x, y, z, w);
		emit valueChanged(this, 0);
	});

	widget->setStyleSheet(
		"QDoubleSpinBox{border: 2px solid rgba(200, 200, 200, .4); padding: 2px; background: rgba(0, 0, 0, 0.2);}"
		"QWidget{ background: rgba(0,0,0,0); color: rgba(250,250,250,1); }"
		"QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}");


	addOutputSocket(new Vector4SocketModel("Result"));
}

void Vector4Node::process(ModelContext * context)
{
	auto ctx = (ShaderContext*)context;
	outSockets[0]->setVarName("vec4(" + QString::number(value.x()) + "," + QString::number(value.y()) + "," + QString::number(value.z()) + "," + QString::number(value.w()) + ")");
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
	title = "Color Node";
	typeName = "color";

	colorWidget = new ColorPickerWidget();
	colorWidget->setMaximumWidth(154);
	this->widget = colorWidget;
	//colorWidget->setGeometry(0, 0, 60, 140);
	connect(colorWidget, &ColorPickerWidget::onColorChanged, [=](QColor color) {
		emit valueChanged(this, 0);
	});

	addOutputSocket(new Vector4SocketModel("R G B A"));
	addOutputSocket(new FloatSocketModel("R"));
	addOutputSocket(new FloatSocketModel("G"));
	addOutputSocket(new FloatSocketModel("B"));
	addOutputSocket(new FloatSocketModel("A"));
}

void ColorPickerNode::process(ModelContext * context)
{
	auto col = colorWidget->getColor();
	outSockets[0]->setVarName("vec4(" + QString::number(col.redF()) + "," + QString::number(col.greenF()) + "," + QString::number(col.blueF()) + "," + QString::number(col.alphaF()) + ")");
	outSockets[1]->setVarName(QString::number(col.redF()));
	outSockets[2]->setVarName(QString::number(col.greenF()));
	outSockets[3]->setVarName(QString::number(col.blueF()));
	outSockets[4]->setVarName(QString::number(col.alphaF()));

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