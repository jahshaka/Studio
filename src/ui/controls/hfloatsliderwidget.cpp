/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/hfloatsliderwidget.h"

#include <cmath>
#include "ui_hfloatsliderwidget.h"

#include <QSignalBlocker>

HFloatSliderWidget::HFloatSliderWidget(QWidget* parent) :
    BaseWidget(parent),
    ui(new Ui::HFloatSliderWidget)
{
    ui->setupUi(this);
    connect(ui->spinbox,    SIGNAL(valueChanged(double)),   SLOT(onValueSpinboxChanged(double)));
    connect(ui->spinbox,    SIGNAL(editingFinished()),      SLOT(onSpinboxEditingFinished()));
    connect(ui->slider,     SIGNAL(valueChanged(int)),      SLOT(onValueSliderChanged(int)));
    connect(ui->slider,     SIGNAL(sliderPressed()),        SLOT(sliderPressed()));
    connect(ui->slider,     SIGNAL(sliderReleased()),       SLOT(sliderReleased()));

    // QWidget::setStyle does NOT take ownership of the style — it only stores
    // the pointer. Every slider row therefore leaked one CustomStyle (a
    // QProxyStyle, ~210 bytes with its private) for the life of the process:
    // 176 of them in a single run of ui.material_panel, which is one properties
    // panel. Parent it to the widget so the row frees it with everything else.
    // (Found by the LeakSanitizer lane, scripts/sanitize.sh lsan.)
    auto *sliderStyle = new CustomStyle(this->style());
    sliderStyle->setParent(this);
    setStyle(sliderStyle);

    ui->spinbox->setButtonSymbols(QAbstractSpinBox::NoButtons);

    precision = 1000.f;
    ui->slider->setRange(0, precision);

    // Initialize BEFORE setRange: the range clamp reads `value`, and every
    // row is built fresh on each panel rebuild - an uninitialized read here
    // handed the row an arbitrary starting value (proved by valgrind on
    // ui.material_panel), which setValue() below then mistook for "already
    // showing that number".
    value = 0.0f;
    minVal = 0.0f;
    maxVal = 100.0f;

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

void HFloatSliderWidget::setDecimals(int decimals)
{
    ui->spinbox->setDecimals(decimals);
    ui->spinbox->setSingleStep(std::pow(10.0, -decimals));
}

/**
 * Sets the value AND pushes it onto the controls.
 *
 * The push is unconditional: a freshly built row's spinbox sits at its .ui
 * default (0.00) no matter what `value` holds, so an early return whenever the
 * member already matched left a wrong number on screen for a correct material
 * (the image-plane Roughness anomaly - a row rebuilt into a recycled block).
 * Only the SIGNAL stays conditional - panels connect valueChanged before
 * populating their rows, and an unchanged value must not write back.
 */
void  HFloatSliderWidget::setValue( float value )
{
    const bool changed = this->value != value;
    this->value = value;

    // Programmatic sync: the controls echo each other through the slots, which
    // would emit a second valueChanged. Set both, silently, and emit once.
    {
        const QSignalBlocker blockSpinbox(ui->spinbox);
        const QSignalBlocker blockSlider(ui->slider);
        ui->spinbox->setValue(value);

        const float span = maxVal - minVal;
        const float mappedValue = span > 0.0f ? (value - minVal) / span : 0.0f;
        ui->slider->setValue((int) (mappedValue * precision));
    }

    if (changed) emit valueChanged(value);
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
    // A keyboard edit (typing, arrow steps, wheel) starts an editing session:
    // announce the pre-edit value so the listener can record it for undo.
    // Slider drags echo into the spinbox programmatically (onValueSliderChanged
    // calls spinbox->setValue) - the slider-down and focus guards keep those,
    // and programmatic setValue() from the panel, out of session bookkeeping.
    if (!spinboxEditing && ui->spinbox->hasFocus() && !ui->slider->isSliderDown()) {
        spinboxEditing = true;
        emit valueChangeStart(this->value);
    }

    this->value = val;

    float mappedValue = (value - minVal) / (maxVal - minVal);

    ui->slider->blockSignals(true);
    ui->slider->setValue((int) (mappedValue * precision));
    ui->slider->blockSignals(false);

    emit valueChanged(this->value);
}

void HFloatSliderWidget::onSpinboxEditingFinished()
{
    // Return pressed or focus left the field: the typed edit is committed.
    if (spinboxEditing) {
        spinboxEditing = false;
        emit valueChangeEnd(this->value);
    }
}

void HFloatSliderWidget::sliderPressed()
{
    emit valueChangeStart(this->value);
}

void HFloatSliderWidget::sliderReleased()
{
    emit valueChangeEnd(this->value);
}
