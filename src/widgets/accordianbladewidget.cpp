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

AccordianBladeWidget::AccordianBladeWidget(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::AccordianBladeWidget)
{
    ui->setupUi(this);

    stretch = 0;
    setMinimumHeight( ui->bg->height());
    minimum_height = minimumHeight();

    connect(ui->toggle, SIGNAL(toggled(bool)), SLOT(onPanelToggled()));

    ui->toggle->setStyleSheet("QPushButton { border-image: url(:/app/icons/right-chevron.svg); }"
                              "QPushButton:hover { background-color: rgb(30, 144, 255); border: none;}");
    ui->contentpane->setVisible(false);
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

//    delete layout;
}

void AccordianBladeWidget::onPanelToggled()
{
    if (ui->contentpane->isVisible()) {
        ui->toggle->setStyleSheet("QPushButton { border-image: url(:/app/icons/right-chevron.svg); }"
                                  "QPushButton:hover { background-color: rgb(30, 144, 255); border: none;}");

        ui->contentpane->setVisible(false);
        this->setMinimumHeight(ui->bg->height());
    } else {
        ui->toggle->setStyleSheet("QPushButton { border-image: url(:/app/icons/chevron-arrow-down.svg); }"
                                  "QPushButton:hover { background-color: rgb(30, 144, 255); border: none;}");

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
    ui->contentpane->layout()->setMargin(0);

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

HFloatSliderWidget* AccordianBladeWidget::addFloatValueSlider(
        const QString& name,
        float start,
        float end)
{
    auto slider = new HFloatSliderWidget();
    slider->ui->label->setText(name);
    slider->setRange(start, end);

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

void AccordianBladeWidget::expand()
{
    // this is a tad bit hacky and there is definitely a better way to do this automatically
    // for now, we calculate and set the accordion height including spacing and margins
    int widgetCount = ui->contentpane->layout()->count();
    int topMargin, bottomMargin;
    int spacing = ui->contentpane->layout()->spacing();
    ui->contentpane->layout()->getContentsMargins(nullptr, &topMargin, nullptr, &bottomMargin);
    int finalHeight = minimum_height + (widgetCount * spacing) + topMargin + bottomMargin;

    this->setMinimumHeight(finalHeight);
    this->setMaximumHeight(finalHeight);

    ui->contentpane->setVisible(true);
}

