/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "materialsettingswidget.h"
#include <QDebug>
#include <QJsonObject>
#include <QJsonValue>
#include <QUuid>


MaterialSettingsWidget::MaterialSettingsWidget(QWidget *parent):
	QWidget(parent)
{
	
//	font.setPointSizeF(font.pointSize() * devicePixelRatioF());
//	font.setPixelSize(7 * devicePixelRatioF());
//	setFont(font);
	if (this->objectName().isEmpty())
		this->setObjectName(QStringLiteral("MaterialSettingsWidget"));

	
	this->resize(330, 433);
	gridLayout = new QGridLayout(this);
	gridLayout->setObjectName(QStringLiteral("gridLayout"));
	gridLayout->setHorizontalSpacing(0);
	gridLayout->setContentsMargins(9, 3, 9, 0);
	formLayout = new QFormLayout();
	formLayout->setObjectName(QStringLiteral("formLayout"));
	formLayout->setContentsMargins(5, 5, 5, 5);
	label = new QLabel("Name",this);
	label->setObjectName(QStringLiteral("label"));
	label->setFont(font);
	formLayout->setWidget(0, QFormLayout::LabelRole, label);
	lineEdit = new QLineEdit(this);
	lineEdit->setObjectName(QStringLiteral("lineEdit"));
	lineEdit->setFont(font);
	formLayout->setWidget(0, QFormLayout::FieldRole, lineEdit);
	label_2 = new QLabel("Z Write",this);
	label_2->setObjectName(QStringLiteral("label_2"));
	label_2->setFont(font);
	formLayout->setWidget(1, QFormLayout::LabelRole, label_2);
	checkBox = new QCheckBox(this);
	checkBox->setObjectName(QStringLiteral("checkBox"));
	checkBox->setLayoutDirection(Qt::RightToLeft);
	formLayout->setWidget(1, QFormLayout::FieldRole, checkBox);
	label_3 = new QLabel("Depth Test",this);
	label_3->setObjectName(QStringLiteral("label_3"));
	formLayout->setWidget(2, QFormLayout::LabelRole, label_3);
	checkBox_2 = new QCheckBox(this);
	checkBox_2->setObjectName(QStringLiteral("checkBox_2"));
	checkBox_2->setLayoutDirection(Qt::RightToLeft);
	formLayout->setWidget(2, QFormLayout::FieldRole, checkBox_2);
	label_4 = new QLabel("Blend Mode",this);
	label_4->setObjectName(QStringLiteral("label_4"));
	formLayout->setWidget(3, QFormLayout::LabelRole, label_4);
	comboBox = new QComboBox(this);
	comboBox->setObjectName(QStringLiteral("comboBox"));
	formLayout->setWidget(3, QFormLayout::FieldRole, comboBox);
	label_5 = new QLabel("Cull Mode", this);
	label_5->setObjectName(QStringLiteral("label_5"));
	formLayout->setWidget(4, QFormLayout::LabelRole, label_5);
	comboBox_2 = new QComboBox(this);
	comboBox_2->setObjectName(QStringLiteral("comboBox_2"));
	formLayout->setWidget(4, QFormLayout::FieldRole, comboBox_2);
	label_6 = new QLabel("Render Layer", this);
	label_6->setObjectName(QStringLiteral("label_6"));
	formLayout->setWidget(5, QFormLayout::LabelRole, label_6);
	comboBox_3 = new QComboBox(this);
	comboBox_3->setObjectName(QStringLiteral("comboBox_3"));
	formLayout->setWidget(5, QFormLayout::FieldRole, comboBox_3);
	label_7 = new QLabel("Fog", this);
	label_7->setObjectName(QStringLiteral("label_7"));
	formLayout->setWidget(6, QFormLayout::LabelRole, label_7);
	checkBox_3 = new QCheckBox(this);
	checkBox_3->setObjectName(QStringLiteral("checkBox_3"));
	checkBox_3->setLayoutDirection(Qt::RightToLeft);
	formLayout->setWidget(6, QFormLayout::FieldRole, checkBox_3);
	label_8 = new QLabel("Cast shadows" , this);
	label_8->setObjectName(QStringLiteral("label_8"));
	formLayout->setWidget(7, QFormLayout::LabelRole, label_8);
	label_9 = new QLabel("Receive shadows",this);
	label_9->setObjectName(QStringLiteral("label_9"));
	formLayout->setWidget(8, QFormLayout::LabelRole, label_9);
	label_10 = new QLabel("Accept Light", this);
	label_10->setObjectName(QStringLiteral("label_10"));
	formLayout->setWidget(9, QFormLayout::LabelRole, label_10);
	checkBox_4 = new QCheckBox(this);
	checkBox_4->setObjectName(QStringLiteral("checkBox_4"));
	checkBox_4->setLayoutDirection(Qt::RightToLeft);
	formLayout->setWidget(7, QFormLayout::FieldRole, checkBox_4);
	checkBox_5 = new QCheckBox(this);
	checkBox_5->setObjectName(QStringLiteral("checkBox_5"));
	checkBox_5->setLayoutDirection(Qt::RightToLeft);
	formLayout->setWidget(8, QFormLayout::FieldRole, checkBox_5);
	checkBox_6 = new QCheckBox(this);
	checkBox_6->setObjectName(QStringLiteral("checkBox_6"));
	checkBox_6->setLayoutDirection(Qt::RightToLeft);
	formLayout->setWidget(9, QFormLayout::FieldRole, checkBox_6);
	gridLayout->addLayout(formLayout, 0, 0, 1, 1);
	formLayout->setSpacing(5);
	setLayout(gridLayout);

	label_3->setFont(font);
	label_4->setFont(font);
	label_5->setFont(font);
	label_6->setFont(font);
	label_7->setFont(font);
	label_8->setFont(font);
	label_9->setFont(font);
	label_10->setFont(font);

	comboBox->setFont(font);
	comboBox_2->setFont(font);
	comboBox_3->setFont(font);



	QStringList list;
	list << "Opaque" << "Blend" << "Additive";
	comboBox->addItems(list);

	list.clear();
	list << "Front" << "Back" << "None";
	comboBox_2->addItems(list);

	list.clear();
	list << "Opaque" << "AlphaTested" << "Transparent" << "Overlay";
	comboBox_3->addItems(list);


	setStyleSheet("QCheckBox {   spacing: 2px 5px; width: 12px; height :12px;}"
		"QCheckBox::indicator {   width: 18px;   height: 18px; }"
		"QCheckBox::indicator::unchecked {	image: url(:/icons/check-unchecked.png);}"
		"QCheckBox::indicator::checked {		image: url(:/icons/check-checked.png);}"
		"QLineEdit {	border: 0;	background: #292929;	padding: 6px;	margin: 0;}"
		"QToolButton {	background: #1E1E1E;	border: 0;	padding: 6px;}"
		"QToolButton:pressed {	background: #111;}"
		"QToolButton:hover {	background: #404040;}"
		"QDoubleSpinBox {	border-radius: 1px;	padding: 6px;	background: #292929;}"
		"QListView::item:selected {    background: #404040;}"
		"QComboBox:editable {}"
		"QComboBox QAbstractItemView::item {    show-decoration-selected: 1;}"
		"QComboBox QAbstractItemView::item {    padding: 6px;}"
		"QComboBox  {    background-color: #1A1A1A;   border: 0;    outline: none; padding: 3px 10px; }"
		"QComboBox:!editable, QComboBox::drop-down:editable {     background: #1A1A1A;}"
		"QComboBox:!editable:on, QComboBox::drop-down:editable:on {    background: #1A1A1A;}"
		"QComboBox QAbstractItemView {    background-color: #1A1A1A;    selection-background-color: #404040;    border: 0;    outline: none; padding: 4px 10px; }"
		"QComboBox QAbstractItemView::item {    border: none; padding: 4px 10px;}"
		"QComboBox QAbstractItemView::item:selected {    background: #404040;    padding-left: 5px;}"
		"QComboBox::drop-down {    subcontrol-origin: padding;    subcontrol-position: top right;    width: 18px;    border-left-width: 1px;}"
		"QComboBox::down-arrow {    image: url(:/icons/down_arrow_check.png);	width: 18px;	height: 14px;} "
		"QComboBox::down-arrow:!enabled {    image: url(:/icons/down_arrow_check_disabled.png);    width: 18px;    height: 14px;}");
	checkBox_6->setStyleSheet(styleSheet());
	checkBox_5->setStyleSheet(styleSheet());
	checkBox_4->setStyleSheet(styleSheet());
	checkBox_3->setStyleSheet(styleSheet());
	checkBox_2->setStyleSheet(styleSheet());
	checkBox->setStyleSheet(styleSheet());
	comboBox_3->setStyleSheet(styleSheet());
	comboBox_2->setStyleSheet(styleSheet());
	comboBox->setStyleSheet(styleSheet());

    setConnections();
}

MaterialSettingsWidget::MaterialSettingsWidget(MaterialSettings settings , QWidget *parent)
{
	MaterialSettingsWidget();
	setMaterialSettings(settings);
}

MaterialSettingsWidget::~MaterialSettingsWidget()
{
	
}

void MaterialSettingsWidget::setMaterialSettings(MaterialSettings settings)
{
	setName(settings.name);
	setZWrite(settings.zwrite);
	setDepthText(settings.depthTest);
	setFog(settings.fog);
	setCastShadows(settings.castShadow);
	setReceiveShadows(settings.receiveShadow);
	setAcceptLighting(settings.acceptLighting);
	setBlendMode(settings.blendMode);
	setCullMode(settings.cullMode);
	setRenderLayer(settings.renderLayer);
    this->settings = settings;
}

void MaterialSettingsWidget::updateMaterialSettingsWidget(MaterialSettings &set)
{
	checkBox->blockSignals(true);
	checkBox_6->blockSignals(true);
	checkBox_2->blockSignals(true);
	checkBox_3->blockSignals(true);
	checkBox_4->blockSignals(true);
	checkBox_5->blockSignals(true);
	lineEdit->blockSignals(true);
	comboBox->blockSignals(true);
	comboBox_2->blockSignals(true);
	comboBox_3->blockSignals(true);

	setName(set.name);
	setZWrite(set.zwrite);
	setDepthText(set.depthTest);
	setFog(set.fog);
	setCastShadows(set.castShadow);
	setReceiveShadows(set.receiveShadow);
	setAcceptLighting(set.acceptLighting);
	setBlendMode(set.blendMode);
	setCullMode(set.cullMode);
	setRenderLayer(set.renderLayer);
	this->settings = set;

	checkBox->blockSignals(false);
	checkBox_6->blockSignals(false);
	checkBox_2->blockSignals(false);
	checkBox_3->blockSignals(false);
	checkBox_4->blockSignals(false);
	checkBox_5->blockSignals(false);
	lineEdit->blockSignals(false);
	comboBox->blockSignals(false);
	comboBox_2->blockSignals(false);
	comboBox_3->blockSignals(false);
}


void MaterialSettingsWidget::setName(QString name)
{
	lineEdit->setText(name);
}

void MaterialSettingsWidget::setZWrite(bool val)
{
	checkBox->setChecked(val);
}

void MaterialSettingsWidget::setDepthText(bool val)
{
	checkBox_2->setChecked(val);
}

void MaterialSettingsWidget::setFog(bool val)
{
	checkBox_3->setChecked(val);
}

void MaterialSettingsWidget::setCastShadows(bool val)
{
	checkBox_4->setChecked(val);
}

void MaterialSettingsWidget::setReceiveShadows(bool val)
{
	checkBox_5->setChecked(val);
}

void MaterialSettingsWidget::setAcceptLighting(bool val)
{
	checkBox_6->setChecked(val);
}


void MaterialSettingsWidget::setBlendMode(BlendMode index)
{
	int i = 0;
	switch (index) {
	case BlendMode::Opaque:
		i = 0;
		break;
	case BlendMode::Blend:
		i = 1;
		break;
	case BlendMode::Additive:
		i = 2;
	}
	comboBox->setCurrentIndex(i);
}


void MaterialSettingsWidget::setCullMode(CullMode index)
{
	int i = 0;
	switch (index) {
	case CullMode::Front:
		i = 0;
		break;
	case CullMode::Back:
		i = 1;
		break;
	case CullMode::None:
		i = 2;
	}
	comboBox_2->setCurrentIndex(i);
}


void MaterialSettingsWidget::setRenderLayer(RenderLayer index)
{
	int i = 0;
	switch (index) {
	case RenderLayer::Opaque:
		i = 0;
		break;
	case RenderLayer::AlphaTested:
		i = 1;
		break;
	case RenderLayer::Transparent:
		i = 2;
		break;
	case RenderLayer::Overlay:
		i = 3;
		break;
	}
	comboBox_3->setCurrentIndex(i);
}

void MaterialSettingsWidget::setConnections()
{
    connect(checkBox, &QCheckBox::stateChanged, [=](int state) { settings.zwrite = checkBox->isChecked(); emit settingsChanged(settings); }); // Z Write
    connect(checkBox_2, &QCheckBox::stateChanged, [=](int state) { settings.depthTest = checkBox_2->isChecked(); emit settingsChanged(settings); }); // Depth Test
    connect(checkBox_3, &QCheckBox::stateChanged, [=](int state) { settings.fog = checkBox_3->isChecked(); emit settingsChanged(settings); }); // Fog
    connect(checkBox_4, &QCheckBox::stateChanged, [=](int state) { settings.castShadow = checkBox_4->isChecked(); emit settingsChanged(settings); }); // Cast Shadow
    connect(checkBox_5, &QCheckBox::stateChanged, [=](int state) { settings.receiveShadow = checkBox_5->isChecked(); emit settingsChanged(settings); }); // Recieve shadows
    connect(checkBox_6, &QCheckBox::stateChanged, [=](int state) { settings.acceptLighting = checkBox_6->isChecked(); emit settingsChanged(settings); }); // Accept light

    connect(lineEdit, &QLineEdit::textChanged, [=](QString string) {settings.name = string;  emit settingsChanged(settings); }); // Name
    connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {settings.blendMode = static_cast<BlendMode>(index); emit settingsChanged(settings); }); //blendmode
    connect(comboBox_2, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {settings.cullMode = static_cast<CullMode>(index);   emit settingsChanged(settings); }); // cull mode
    connect(comboBox_3, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) { settings.renderLayer = static_cast<RenderLayer>(index);  emit settingsChanged(settings); }); // render layer
}


