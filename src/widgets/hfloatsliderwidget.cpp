/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "hfloatsliderwidget.h"
#include "ui_hfloatsliderwidget.h"

HFloatSliderWidget::HFloatSliderWidget(QWidget* parent) :
    BaseWidget(parent),
    ui(new Ui::HFloatSliderWidget)
{
    ui->setupUi(this);
    connect(ui->spinbox,    SIGNAL(valueChanged(double)),   SLOT(onValueSpinboxChanged(double)));
    connect(ui->slider,     SIGNAL(valueChanged(int)),      SLOT(onValueSliderChanged(int)));
    connect(ui->slider,     SIGNAL(sliderPressed()),        SLOT(sliderPressed()));
    connect(ui->slider,     SIGNAL(sliderReleased()),       SLOT(sliderReleased()));

    setStyle(new CustomStyle(this->style()));

    ui->spinbox->setButtonSymbols(QAbstractSpinBox::NoButtons);

    precision = 1000.f;
    ui->slider->setRange(0, precision);

    this->setRange(0, 100.f);

    type = WidgetType::FloatWidget;
}

HFloatSliderWidget::~HFloatSliderWidget()
{
    delete ui;
}

/**
 * Sets the range for the slider and also keeps the slider's value within the said range
 * @param minVal
 * @param maxVal
 */
void HFloatSliderWidget::setRange(float minVal, float maxVal)
{
    if (value < minVal) {
        value = minVal;
    }

    if (value > maxVal) {
        value = maxVal;
    }

    this->minVal = minVal;
    this->maxVal = maxVal;

    ui->spinbox->setRange(minVal, maxVal);
}

void  HFloatSliderWidget::setValue( float value )
{
    if (this->value != value) {
        this->value = value;
        ui->spinbox->setValue(value);

        float mappedValue = (value - minVal) / (maxVal - minVal);
        ui->slider->setValue((int) (mappedValue * precision));

        emit valueChanged(value);
    }
}

float HFloatSliderWidget::getValue()
{
    return value;
}

void HFloatSliderWidget::onValueSliderChanged(int val)
{

    float range = (float) val / precision;
    this->value = minVal + (maxVal - minVal) * range;

    ui->slider->blockSignals(true);
    ui->spinbox->setValue(this->value);
    ui->slider->blockSignals(false);

    emit valueChanged(this->value);
}

void HFloatSliderWidget::onValueSpinboxChanged(double val)
{
    this->value = val;

    float mappedValue = (value - minVal) / (maxVal - minVal);

    ui->slider->blockSignals(true);
    ui->slider->setValue((int) (mappedValue * precision));
    ui->slider->blockSignals(false);

    emit valueChanged(this->value);
}

void HFloatSliderWidget::sliderPressed()
{
    emit valueChangeStart(this->value);
}

void HFloatSliderWidget::sliderReleased()
{
    emit valueChangeEnd(this->value);
}
