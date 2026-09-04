/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/qtinterop.h"
#include "irisgl/core/math/vec.h"
#include "ui/panels/propertywidget.h"
#include "ui_propertywidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui_hfloatsliderwidget.h"
#include "ui/controls/colorvaluewidget.h"
#include "ui_colorvaluewidget.h"
#include "ui/controls/checkboxwidget.h"
#include "ui_checkboxwidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "ui_texturepickerwidget.h"
#include "ui/controls/filepickerwidget.h"
#include "ui_filepickerwidget.h"
#include "ui/controls/comboboxwidget.h"
#include <QDir>
#include "data/database/database.h"

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
    texpicker->project = project;
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

    // A material's Alpha Mode is an enum wearing an IntProperty: a slider row
    // reads as a meaningless 0..5. Render it as a labeled dropdown instead —
    // combo index == stored int value (PbrMaterial's alphaMode contract).
    if (intProp->name == "alphaMode") {
        auto combo = new ComboBoxWidget();
        combo->setLabel(intProp->displayName);
        // Unreal-familiar names; Glass stays value 3 (the engine's
        // realistic-transparency mode — shipped scenes/presets depend on it).
        for (const QString &label : { tr("Opaque"), tr("Masked"), tr("Translucent"),
                                      tr("Glass"), tr("Additive"), tr("Modulate") })
            combo->addItem(label);
        combo->index = prop->id;
        combo->setCurrentIndex(intProp->getValue().toInt());
        progressiveHeight += combo->height() + stretch;
        ui->contentpane->layout()->addWidget(combo);
        properties.append(prop);
        connect(combo, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
                this, [this, intProp](int idx) {
            if (intProp->getValue().toInt() == idx) return;
            // Start must see the OLD value (it records the undo baseline),
            // End the new one — a combo pick is a complete one-shot gesture.
            if (listener) listener->onPropertyChangeStart(intProp);
            intProp->value = idx;
            if (listener) {
                listener->onPropertyChanged(intProp);
                listener->onPropertyChangeEnd(intProp);
            }
            emit onPropertyChanged(intProp);
        });
        return;
    }
    auto intWidget = addFloatValueSlider(intProp->displayName, intProp->minValue, intProp->maxValue);

    intWidget->index = prop->id;
    intWidget->setValue(float(intProp->getValue().toInt()));
    ui->contentpane->layout()->addWidget(intWidget);
    properties.append(prop);

    // Same wiring as the float rows (this row had none at all - the panel's int
    // properties, e.g. a material's Alpha Mode, silently did nothing).
    connect(intWidget, &HFloatSliderWidget::valueChanged, this, [this, intProp](float value) {
        intProp->value = qRound(value);

        if (listener) {
            listener->onPropertyChanged(intProp);
        }

        emit onPropertyChanged(intProp);
    });

    connect(intWidget, &HFloatSliderWidget::valueChangeStart, this, [this, intProp](float value) {
        intProp->value = qRound(value);

        if (listener) {
            listener->onPropertyChangeStart(intProp);
        }

        emit onPropertyChanged(intProp);
    });

    connect(intWidget, &HFloatSliderWidget::valueChangeEnd, this, [this, intProp](float value) {
        intProp->value = qRound(value);

        if (listener) {
            listener->onPropertyChangeEnd(intProp);
        }

        emit onPropertyChanged(intProp);
    });
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

    // The popup session brackets the live changes above into one undo entry
    // (start fires before any change, so the listener records the old colour).
    connect(colorWidget->getPicker(), &ColorPickerWidget::pickingStarted, this, [this, colorProp]() {
        if (listener) listener->onPropertyChangeStart(colorProp);
    });
    connect(colorWidget->getPicker(), &ColorPickerWidget::pickingEnded, this, [this, colorProp]() {
        if (listener) listener->onPropertyChangeEnd(colorProp);
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
        // A checkbox toggle is one discrete gesture - one undo entry.
        if (listener) listener->onPropertyChangeStart(boolProp);

        boolProp->value = value;

        if (listener) {
            listener->onPropertyChanged(boolProp);
            listener->onPropertyChangeEnd(boolProp);
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
        // Picking a texture is a single discrete gesture: bracket it with
        // change start/end so it lands as one undo entry. Start must run
        // BEFORE the write - the listener records the old value from the prop.
        if (listener) listener->onPropertyChangeStart(textureProp);

        textureProp->value = value;

        if (listener) {
            listener->onPropertyChanged(textureProp);
            listener->onPropertyChangeEnd(textureProp);
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

	connect(widget, &Widget2D::valueChanged, [=](iris::Vec2 value) {
		vecProp->value = iris::toQt(value);
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

	connect(widget, &Widget3D::valueChanged, [=](iris::Vec3 value) {
		vecProp->value = iris::toQt(value);
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

	connect(widget, &Widget4D::valueChanged, [=](iris::Vec4 value) {
		vecProp->value = iris::toQt(value);
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
