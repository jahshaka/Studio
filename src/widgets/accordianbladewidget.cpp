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
#include "hfloatslider.h"
#include "ui_hfloatslider.h"
#include "colorvaluewidget.h"
#include "ui_colorvaluewidget.h"
#include "texturepicker.h"
#include "ui_texturepicker.h"
#include "transformeditor.h"
#include "ui_transformeditor.h"
#include "checkboxproperty.h"
#include "ui_checkboxproperty.h"
#include "combobox.h"
#include "ui_combobox.h"
#include "textinput.h"
#include "ui_textinput.h"
#include "ui_labelwidget.h"
#include "labelwidget.h"
#include "ui_labelwidget.h"

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
    TransformEditor *transform = new TransformEditor();

    int height = transform->height();
    int spacing = ui->contentpane->layout()->spacing();

    minimum_height += height;

    ui->contentpane->layout()->addWidget(transform);
    ui->contentpane->layout()->setMargin(0);

    return transform;
}

ColorValueWidget* AccordianBladeWidget::addColorPicker(const QString& name)
{
    ColorValueWidget *colorpicker = new ColorValueWidget();
    colorpicker->setLabel(name);

    minimum_height += colorpicker->height() + stretch;

    ui->contentpane->layout()->addWidget(colorpicker);
    return colorpicker;
}

TexturePicker* AccordianBladeWidget::addTexturePicker(const QString& name)
{
    TexturePicker *texpicker = new TexturePicker();
    texpicker->ui->label->setText(name);

    minimum_height += texpicker->height() + stretch;

    ui->contentpane->layout()->addWidget(texpicker);
    return texpicker;
}

HFloatSlider* AccordianBladeWidget::addFloatValueSlider(const QString& name, float start, float end)
{
    HFloatSlider *slider = new HFloatSlider();
    slider->ui->label->setText(name);
    slider->setRange(start, end);

    minimum_height += slider->height() + stretch;

    ui->contentpane->layout()->addWidget(slider);
    return slider;
}

CheckBoxProperty* AccordianBladeWidget::addCheckBox(const QString& title, bool value)
{
    auto checkbox = new CheckBoxProperty();
    checkbox->setLabel(title);

    minimum_height += checkbox->height() + stretch;

    ui->contentpane->layout()->addWidget(checkbox);
    return checkbox;
}

ComboBox* AccordianBladeWidget::addComboBox(const QString& title)
{
    ComboBox* combobox = new ComboBox();
    combobox->setLabel(title);

    minimum_height += combobox->height() + stretch;

    ui->contentpane->layout()->addWidget(combobox);
    return combobox;
}

TextInput* AccordianBladeWidget::addTextInput(const QString& title)
{
    TextInput* textInput = new TextInput();
    textInput->setLabel(title);

    minimum_height += textInput->height() + stretch;

    ui->contentpane->layout()->addWidget(textInput);
    return textInput;
}

LabelWidget* AccordianBladeWidget::addLabel(const QString& title, const QString& text)
{
    LabelWidget* label = new LabelWidget();
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

