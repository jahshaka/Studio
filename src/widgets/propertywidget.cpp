/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "propertywidget.h"
#include "ui_propertywidget.h"
#include "hfloatsliderwidget.h"
#include "ui_hfloatsliderwidget.h"
#include "colorvaluewidget.h"
#include "ui_colorvaluewidget.h"
#include "checkboxwidget.h"
#include "ui_checkboxwidget.h"
#include "texturepickerwidget.h"
#include "ui_texturepickerwidget.h"
#include "filepickerwidget.h"
#include "ui_filepickerwidget.h"
#include "globals.h"
#include <QDir>
#include "core/database/database.h"

PropertyWidget::PropertyWidget(QWidget *parent) : QWidget(parent), ui(new Ui::PropertyWidget)
{
    ui->setupUi(this);
    progressiveHeight = stretch = 0;
}

PropertyWidget::~PropertyWidget()
{
    delete ui;
}

HFloatSliderWidget* PropertyWidget::addFloatValueSlider(const QString& name, float min, float max)
{
    auto slider = new HFloatSliderWidget();
    slider->ui->label->setText(name);
    slider->setRange(min, max);
    progressiveHeight += slider->height() + stretch;

    return slider;
}

ColorValueWidget* PropertyWidget::addColorPicker(const QString& name)
{
    auto colorpicker = new ColorValueWidget();
    colorpicker->setLabel(name);
    progressiveHeight += colorpicker->height() + stretch;

    return colorpicker;
}

CheckBoxWidget* PropertyWidget::addCheckBox(const QString& title)
{
    auto checkbox = new CheckBoxWidget();
    checkbox->setLabel(title);
    progressiveHeight += checkbox->height() + stretch;

    return checkbox;
}

TexturePickerWidget* PropertyWidget::addTexturePicker(const QString& name)
{
    auto texpicker = new TexturePickerWidget();
    texpicker->ui->label->setText(name);
    progressiveHeight += texpicker->height() + stretch;

    return texpicker;
}

FilePickerWidget* PropertyWidget::addFilePicker(const QString &name, const QString &suffix)
{
    FilePickerWidget *filePicker = new FilePickerWidget();
    filePicker->ui->label->setText(name);
    filePicker->suffix = suffix;
    progressiveHeight += filePicker->height() + stretch;

    return filePicker;
}

void PropertyWidget::addFloatProperty(iris::Property *prop)
{
    auto fltProp = static_cast<iris::FloatProperty*>(prop);
    auto fltWidget = addFloatValueSlider(fltProp->displayName, fltProp->minValue, fltProp->maxValue);

    fltWidget->index = prop->id;
    fltWidget->setValue(fltProp->getValue().toFloat());
    ui->contentpane->layout()->addWidget(fltWidget);
    properties.append(prop);

    connect(fltWidget, &HFloatSliderWidget::valueChanged, this, [this, fltProp](float value) {
        fltProp->value = value;

        if (listener) {
            listener->onPropertyChanged(fltProp);
        }

        emit onPropertyChanged(fltProp);
    });

    connect(fltWidget, &HFloatSliderWidget::valueChangeStart, this, [this, fltProp](float value) {
        fltProp->value = value;

        if (listener) {
            listener->onPropertyChangeStart(fltProp);
        }

        emit onPropertyChanged(fltProp);
    });

    connect(fltWidget, &HFloatSliderWidget::valueChangeEnd, this, [this, fltProp](float value) {
        fltProp->value = value;

        if (listener) {
            listener->onPropertyChangeEnd(fltProp);
        }

        emit onPropertyChanged(fltProp);
    });
}

void PropertyWidget::addIntProperty(iris::Property *prop)
{
    auto intProp = static_cast<iris::IntProperty*>(prop);
    auto intWidget = addFloatValueSlider(intProp->displayName, intProp->minValue, intProp->maxValue);

    intWidget->index = prop->id;
    ui->contentpane->layout()->addWidget(intWidget);
    properties.append(prop);

}

void PropertyWidget::addColorProperty(iris::Property *prop)
{
    auto colorProp = static_cast<iris::ColorProperty*>(prop);
    auto colorWidget = addColorPicker(colorProp->displayName);

    colorWidget->index = prop->id;
    colorWidget->setColorValue(colorProp->getValue().value<QColor>());
    ui->contentpane->layout()->addWidget(colorWidget);
    properties.append(prop);

    connect(colorWidget->getPicker(), &ColorPickerWidget::onColorChanged, this,
           [this, colorProp](QColor value)
    {
        colorProp->value = value;

        if (listener) {
            listener->onPropertyChanged(colorProp);
        }

        emit onPropertyChanged(colorProp);
    });
}

void PropertyWidget::addBoolProperty(iris::Property *prop)
{
    auto boolProp = static_cast<iris::BoolProperty*>(prop);
    auto boolWidget = addCheckBox(boolProp->displayName);

    boolWidget->index = prop->id;
    boolWidget->setValue(boolProp->getValue().toBool());
    ui->contentpane->layout()->addWidget(boolWidget);
    properties.append(prop);

    connect(boolWidget, &CheckBoxWidget::valueChanged, this, [this, boolProp](bool value) {
        boolProp->value = value;

        if (listener) {
            listener->onPropertyChanged(boolProp);
        }

        emit onPropertyChanged(boolProp);
    });
}

void PropertyWidget::addTextureProperty(iris::Property *prop)
{
    auto textureProp = static_cast<iris::TextureProperty*>(prop);
    auto textureWidget = addTexturePicker(textureProp->displayName);

    textureWidget->index = prop->id;

	auto texturePath = prop->getValue().toString();

    textureWidget->setTexture(texturePath);
    ui->contentpane->layout()->addWidget(textureWidget);
    properties.append(prop);

    connect(textureWidget, &TexturePickerWidget::valueChanged, this,
           [this, textureProp](QString value)
    {
        textureProp->value = value;

        if (listener) {
            listener->onPropertyChanged(textureProp);
        }

        emit onPropertyChanged(textureProp);
    });
}

void PropertyWidget::addFileProperty(iris::Property *prop)
{
    auto fileProp = static_cast<iris::FileProperty*>(prop);
    auto fileWidget = addFilePicker(fileProp->displayName, fileProp->suffix);

    fileWidget->index = prop->id;
    fileWidget->setFilepath(fileProp->getValue().toString());
    ui->contentpane->layout()->addWidget(fileWidget);
    properties.append(prop);

    connect(fileWidget, &FilePickerWidget::onPathChanged, this, [this, fileProp](QString value) {
        fileProp->value = value;

        if (listener) {
            listener->onPropertyChanged(fileProp);
        }

        emit onPropertyChanged(fileProp);
    });
}

void PropertyWidget::addVector2Property(iris::Property *prop)
{
	auto vecProp = static_cast<iris::Vec2Property*>(prop);
	auto widget = addVector2Widget(vecProp->displayName, vecProp->value.x(), vecProp->value.y());
	auto holder = addWidgetHolder(vecProp->displayName, widget);
	ui->contentpane->layout()->addWidget(holder);
	properties.append(vecProp);

	connect(widget, &Widget2D::valueChanged, [=](QVector2D value) {
		vecProp->value = value;
		if (listener) listener->onPropertyChanged(vecProp);
		emit onPropertyChanged(vecProp);
	});

}

void PropertyWidget::addVector3Property(iris::Property *prop)
{
	auto vecProp = static_cast<iris::Vec3Property*>(prop);
	auto widget = addVector3Widget(vecProp->displayName, vecProp->value.x(), vecProp->value.y(), vecProp->value.z());
	auto holder = addWidgetHolder(vecProp->displayName, widget);
	ui->contentpane->layout()->addWidget(holder);
	properties.append(vecProp);

	connect(widget, &Widget3D::valueChanged, [=](QVector3D value) {
		vecProp->value = value;
		if (listener) listener->onPropertyChanged(vecProp);
		emit onPropertyChanged(vecProp);
	});

}

void PropertyWidget::addVector4Property(iris::Property *prop)
{
	auto vecProp = static_cast<iris::Vec4Property*>(prop);
	auto widget = addVector4Widget(vecProp->displayName, vecProp->value.x(), vecProp->value.y(), vecProp->value.z(), vecProp->value.w());
	auto holder = addWidgetHolder(vecProp->displayName, widget);
	ui->contentpane->layout()->addWidget(holder);
	properties.append(vecProp);

	connect(widget, &Widget4D::valueChanged, [=](QVector4D value) {
		vecProp->value = value;
		if (listener) listener->onPropertyChanged(vecProp);
		emit onPropertyChanged(vecProp);
	});

}

Widget2D * PropertyWidget::addVector2Widget(const QString &, float xValue, float yValue)
{
	auto widget = new Widget2D;
	widget->setValues(xValue, yValue);
	ui->contentpane->layout()->addWidget(widget);
	progressiveHeight += widget->height() + stretch;

	return widget;
}

Widget3D * PropertyWidget::addVector3Widget(const QString &, float xValue, float yValue, float zValue)
{
	auto widget = new Widget3D;
	widget->setValues(xValue, yValue, zValue);
	ui->contentpane->layout()->addWidget(widget);
	progressiveHeight += widget->height() + stretch;

	return widget;
}

Widget4D * PropertyWidget::addVector4Widget(const QString &, float xValue, float yValue, float zValue, float wValue)
{
	auto widget = new Widget4D;
	widget->setValues(xValue, yValue, zValue, wValue);
	ui->contentpane->layout()->addWidget(widget);
	progressiveHeight += widget->height() + stretch;

	return widget;
}

QWidget * PropertyWidget::addWidgetHolder(QString title, QWidget* widget)
{

	auto holder = new QWidget;
	auto layout = new QHBoxLayout;
	holder->setLayout(layout);

	layout->addWidget(new QLabel(title, holder));
	layout->addStretch();
	layout->addWidget(widget);
	layout->setContentsMargins(0, 0, 0, 0);

	holder->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
	widget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

	return holder;
}




void PropertyWidget::setListener(iris::PropertyListener *listener)
{
    this->listener = listener;
}

void PropertyWidget::updatePane()
{

}

void PropertyWidget::setProperties(QList<iris::Property*> properties)
{
    for (auto prop : properties)
        switch (prop->type) {
            case iris::PropertyType::Float:
                addFloatProperty(prop);
            break;

            case iris::PropertyType::Int:
                addIntProperty(prop);
            break;

            case iris::PropertyType::Color:
                addColorProperty(prop);
            break;

            case iris::PropertyType::Bool:
                addBoolProperty(prop);
            break;

            case iris::PropertyType::Texture:
                addTextureProperty(prop);
            break;

            case iris::PropertyType::File:
                addFileProperty(prop);
            break;

            case iris::PropertyType::List:
            break;

            case iris::PropertyType::Vec2:
				addVector2Property(prop);
            break;

			case iris::PropertyType::Vec3:
				addVector3Property(prop);
			break;
			case iris::PropertyType::Vec4:
				addVector4Property(prop);
			break;

            case iris::PropertyType::None:
            default: break;
        }

    updatePane();

    this->properties = properties;
}

int PropertyWidget::getHeight()
{
    return progressiveHeight + (properties.size() * ui->contentpane->layout()->spacing());
}
