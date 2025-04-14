/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "colorpickerwidget.h"
#include "ui_colorpickerwidget.h"
#include "colorview.h"
#include <QColorDialog>
#include <QPainter>


ColorPickerWidget::ColorPickerWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ColorPickerWidget)
{
    ui->setupUi(this);
    color = QColor::fromRgb(255,255,255);

}

ColorPickerWidget::~ColorPickerWidget()
{
    delete ui;
}

void ColorPickerWidget::paintEvent(QPaintEvent* evt)
{
    Q_UNUSED(evt);
    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();
    QPainter paint(this);

    paint.fillRect(0,0,widgetWidth,widgetHeight,color);
}

void ColorPickerWidget::setColor(QColor col)
{
    if(color!=col)
    {
        color = col;
        emit onSetColor(col);
        this->repaint();
    }
}

void ColorPickerWidget::colorChanged(QColor col)
{
    if(color!=col)
    {
        color = col;
        emit onColorChanged(col);
        this->repaint();
    }
}

void ColorPickerWidget::mouseReleaseEvent(QMouseEvent* event)
{
	view = ColorView::getSingleston();
	connect(view, SIGNAL(onColorChanged(QColor)), this, SLOT(colorChanged(QColor)));
	connect(view, &ColorView::exiting, [=]() {
		view->disconnect();
	});
	view->showAtPosition(event, color);
    this->repaint();
}

void ColorPickerWidget::mousePressEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
}
