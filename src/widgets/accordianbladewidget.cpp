/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "accordianbladewidget.h"
#include "ui_accordianbladewidget.h"

#include "hfloatsliderwidget.h"
#include "ui_hfloatsliderwidget.h"
#include "colorvaluewidget.h"
#include "ui_colorvaluewidget.h"
#include "texturepickerwidget.h"
#include "ui_texturepickerwidget.h"
#include "transformeditor.h"
#include "ui_transformeditor.h"
#include "checkboxwidget.h"
#include "ui_checkboxwidget.h"
#include "comboboxwidget.h"
#include "ui_comboboxwidget.h"
#include "textinputwidget.h"
#include "ui_textinputwidget.h"
#include "ui_labelwidget.h"
#include "labelwidget.h"
#include "ui_labelwidget.h"
#include "filepickerwidget.h"
#include "ui_filepickerwidget.h"
#include "propertywidget.h"
#include "ui_propertywidget.h"

#include "src/widgets/propertywidgets/cubemapwidget.h"


// TODO - omit height calculation
AccordianBladeWidget::AccordianBladeWidget(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::AccordianBladeWidget)
{
    ui->setupUi(this);

    stretch = 0;
    setMinimumHeight(ui->bg->height());
    minimum_height = minimumHeight();

    connect(ui->toggle, SIGNAL(toggled(bool)), SLOT(onPanelToggled()));

    ui->toggle->setIconSize(QSize(24, 24));
	collapse();
}

AccordianBladeWidget::~AccordianBladeWidget()
{
    delete ui;
}

void AccordianBladeWidget::clearPanel(QLayout *layout)
{
    if (ui->contentpane->layout() == nullptr) return;

    while (auto item = ui->contentpane->layout()->takeAt(0)) {
        if (auto widget = item->widget()) widget->deleteLater();

        if (auto childLayout = item->layout()) {
            this->clearPanel(childLayout);
        }

        delete item;
    }
}

void AccordianBladeWidget::onPanelToggled()
{
    if (ui->contentpane->isVisible()) {
		collapse();
    } else {
        expand();
    }
}

void AccordianBladeWidget::setPanelTitle(const QString& title)
{
    ui->content_title->setText(title);
}

TransformEditor* AccordianBladeWidget::addTransformControls()
{
    auto transformEditor = new TransformEditor();

    int height = transformEditor->height();
    int spacing = ui->contentpane->layout()->spacing();

    minimum_height += height;

    ui->contentpane->layout()->addWidget(transformEditor);
    ui->contentpane->layout()->setContentsMargins(0, 0, 0,0);

    return transformEditor;
}

ColorValueWidget* AccordianBladeWidget::addColorPicker(const QString& name)
{
    auto colorpicker = new ColorValueWidget();
    colorpicker->setLabel(name);

    minimum_height += colorpicker->height() + stretch;

    ui->contentpane->layout()->addWidget(colorpicker);
    return colorpicker;
}

TexturePickerWidget* AccordianBladeWidget::addTexturePicker(const QString& name)
{
    auto texpicker = new TexturePickerWidget();
    texpicker->ui->label->setText(name);

    minimum_height += texpicker->height() + stretch;

    ui->contentpane->layout()->addWidget(texpicker);
    return texpicker;
}

FilePickerWidget* AccordianBladeWidget::addFilePicker(const QString &name)
{
    FilePickerWidget *filePicker = new FilePickerWidget();
    filePicker->ui->label->setText(name);
//    filePicker->suffix = suffix;

    minimum_height += filePicker->height() + stretch;

    ui->contentpane->layout()->addWidget(filePicker);
    return filePicker;
}

Widget2D * AccordianBladeWidget::addVector2Widget(const QString &, float xValue, float yValue)
{
	auto widget = new Widget2D;
	widget->setValues(xValue, yValue);
	minimum_height += widget->height() + stretch;
	ui->contentpane->layout()->addWidget(widget);
	return widget;
}

Widget3D * AccordianBladeWidget::addVector3Widget(const QString &, float xValue, float yValue, float zValue)
{
	auto widget = new Widget3D;
	widget->setValues(xValue, yValue, zValue);
	minimum_height += widget->height() + stretch;
	ui->contentpane->layout()->addWidget(widget);
	return widget;
}

Widget4D * AccordianBladeWidget::addVector4Widget(const QString &, float xValue, float yValue, float zValue, float wValue)
{
	auto widget = new Widget4D;
	widget->setValues(xValue, yValue, zValue, wValue);
	minimum_height += widget->height() + stretch;
	ui->contentpane->layout()->addWidget(widget);
	return widget;
}

CubeMapWidget* AccordianBladeWidget::addCubeMapWidget(QStringList list)
{
	auto widget = new CubeMapWidget();
	widget->addCubeMapImages(list);
	minimum_height += widget->height() + stretch;
	ui->contentpane->layout()->addWidget(widget);
	return widget;
}

CubeMapWidget* AccordianBladeWidget::addCubeMapWidget()
{
	auto widget = new CubeMapWidget(this);
	minimum_height += widget->height() + stretch;
	ui->contentpane->layout()->addWidget(widget);
	return widget;
}

CubeMapWidget* AccordianBladeWidget::addCubeMapWidget(QString top, QString bottom, QString left, QString front, QString right, QString back)
{
	return addCubeMapWidget({top,bottom,left,front,right,back});
}

PropertyWidget *AccordianBladeWidget::addPropertyWidget()
{
    PropertyWidget *props = new PropertyWidget;
    ui->contentpane->layout()->addWidget(props);
    return props;
}

HFloatSliderWidget* AccordianBladeWidget::addFloatValueSlider(
        const QString& name,
        float start,
        float end,
        float value)
{
    auto slider = new HFloatSliderWidget();
    slider->ui->label->setText(name);
    slider->setRange(start, end);
    slider->setValue(value);

    minimum_height += slider->height() + stretch;

    ui->contentpane->layout()->addWidget(slider);
    return slider;
}

CheckBoxWidget* AccordianBladeWidget::addCheckBox(const QString& title, bool value)
{
    auto checkbox = new CheckBoxWidget();
    checkbox->setLabel(title);

    minimum_height += checkbox->height() + stretch;

    ui->contentpane->layout()->addWidget(checkbox);
    return checkbox;
}

ComboBoxWidget* AccordianBladeWidget::addComboBox(const QString& title)
{
    auto combobox = new ComboBoxWidget();
    combobox->setLabel(title);

    minimum_height += combobox->height() + stretch;

    ui->contentpane->layout()->addWidget(combobox);
    return combobox;
}

TextInputWidget* AccordianBladeWidget::addTextInput(const QString& title)
{
    auto textInput = new TextInputWidget();
    textInput->setLabel(title);

    minimum_height += textInput->height() + stretch;

    ui->contentpane->layout()->addWidget(textInput);
    return textInput;
}

LabelWidget* AccordianBladeWidget::addLabel(const QString& title, const QString& text)
{
    auto label = new LabelWidget();
    label->setLabel(title);
    label->setText(text);

    minimum_height += label->height() + stretch;

    ui->contentpane->layout()->addWidget(label);
    return label;
}

void AccordianBladeWidget::collapse()
{
	ui->toggle->setIcon(QIcon(":/icons/right-chevron.svg"));
	ui->contentpane->setVisible(false);
	this->setMinimumHeight(ui->bg->height());
}

void AccordianBladeWidget::expand()
{
    // this is a tad bit hacky and there is definitely a better way to do this automatically
    // for now, we calculate and set the accordion height including spacing and margins
    // int widgetCount = ui->contentpane->layout()->count();
    // int topMargin, bottomMargin;
    // int spacing = ui->contentpane->layout()->spacing();
    // ui->contentpane->layout()->getContentsMargins(nullptr, &topMargin, nullptr, &bottomMargin);
    // int finalHeight = minimum_height + (widgetCount * spacing) + topMargin + bottomMargin;

    this->setMinimumHeight(0);
    // this->setMaximumHeight(finalHeight);
	ui->toggle->setIcon(QIcon(":/icons/chevron-arrow-down.svg"));
    ui->contentpane->setVisible(true);
}

