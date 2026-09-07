/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

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
	label_4 = new QLabel("Blend Mode",this);
	label_4->setObjectName(QStringLiteral("label_4"));
	formLayout->setWidget(1, QFormLayout::LabelRole, label_4);
	comboBox = new QComboBox(this);
	comboBox->setObjectName(QStringLiteral("comboBox"));
	formLayout->setWidget(1, QFormLayout::FieldRole, comboBox);
	gridLayout->addLayout(formLayout, 0, 0, 1, 1);
	formLayout->setSpacing(5);
	setLayout(gridLayout);

	label_4->setFont(font);
	comboBox->setFont(font);



	QStringList list;
	list << "Opaque" << "Masked" << "Translucent" << "Additive" << "Modulate";
	comboBox->addItems(list);


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
	setBlendMode(settings.blendMode);
    this->settings = settings;
}

void MaterialSettingsWidget::updateMaterialSettingsWidget(MaterialSettings &set)
{
	lineEdit->blockSignals(true);
	comboBox->blockSignals(true);

	setName(set.name);
	setBlendMode(set.blendMode);
	this->settings = set;

	lineEdit->blockSignals(false);
	comboBox->blockSignals(false);
}


void MaterialSettingsWidget::setName(QString name)
{
	lineEdit->setText(name);
}

void MaterialSettingsWidget::setBlendMode(BlendMode index)
{
	// combo items are declared in BlendMode enum order
	comboBox->setCurrentIndex(static_cast<int>(index));
}


void MaterialSettingsWidget::setConnections()
{
    connect(lineEdit, &QLineEdit::textChanged, [=](QString string) {settings.name = string;  emit settingsChanged(settings); }); // Name
    connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {settings.blendMode = static_cast<BlendMode>(index); emit settingsChanged(settings); }); //blendmode
}


