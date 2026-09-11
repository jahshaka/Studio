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
#include "ui/style/stylesheet.h"


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


	setStyleSheet(StyleSheet::MaterialSettingsPanel());
	comboBox->setStyleSheet(StyleSheet::MaterialSettingsPanel());

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


