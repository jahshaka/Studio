/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "lcdslider.h"
//#include "ui_lcdslider.h"
#include "ui_newlcdslider.h"
#include <QDebug>
#include <QMouseEvent>

LCDSlider::LCDSlider(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::NewLCDSlider)
{
    ui->setupUi(this);
    dragging = false;
    valueScale = 10;
    value = 0;
    this->setValueRange(-100,100);

    connect(ui->horizontalSlider,SIGNAL(valueChanged(int)),this,SLOT(onSliderValueChanged(int)));
    connect(ui->numberValue,SIGNAL(valueChanged(int)),this,SLOT(onEditTextValueChanged(int)));
}

LCDSlider::~LCDSlider()
{
    delete ui;
}

void LCDSlider::setValue(int value)
{
    this->value = value;
    //ui->numberValue->setText(QString("%1").arg(value));
    ui->numberValue->setValue(value);
    ui->horizontalSlider->setValue(value);
    emit valueChanged(value);
}

void LCDSlider::setValueRange(int min,int max)
{
    ui->horizontalSlider->setMinimum(min);
    ui->horizontalSlider->setMaximum(max);
}

void LCDSlider::setLabelText(QString text)
{
    ui->label->setText(text);
}

void LCDSlider::hideLabel(){
    ui->label->hide();
}

void LCDSlider::showLabel()
{
    ui->label->show();
}

void LCDSlider::onSliderValueChanged(int value)
{
    this->value = value;

    ui->numberValue->blockSignals(true);
    ui->numberValue->setValue(value);
    ui->numberValue->blockSignals(false);
    emit valueChanged(value);
}

void LCDSlider::onEditTextValueChanged(int value)
{
    this->value = value;

    ui->horizontalSlider->blockSignals(true);
    //ui->numberValue->setText(QString("%1").arg(value));
    ui->horizontalSlider->setValue(value);
    ui->horizontalSlider->blockSignals(false);

    emit valueChanged(value);
}

void LCDSlider::mousePressEvent(QMouseEvent* evt)
{
    auto pos = evt->localPos();
    auto hitObj = this->childAt(pos.x(),pos.y());

    if(hitObj==ui->numberValue)
    {
        dragging = true;
    }

    lastX = evt->x();
}

void LCDSlider::mouseMoveEvent(QMouseEvent* evt)
{

    int xpos = evt->x();

    int diff = xpos-lastX;

    value += diff*valueScale;
    setValue(value);

    lastX = xpos;
}

void LCDSlider::mouseReleaseEvent(QMouseEvent* evt)
{
    Q_UNUSED(evt);

    dragging = false;
}

void LCDSlider::mouseDoubleClickEvent(QMouseEvent* evt)
{
    Q_UNUSED(evt);
    setValue(0);
}
