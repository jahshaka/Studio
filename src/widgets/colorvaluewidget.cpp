/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "colorvaluewidget.h"
#include "ui_colorvaluewidget.h"

ColorValueWidget::ColorValueWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ColorValueWidget)
{
    ui->setupUi(this);

    //connect(ui->widget,SIGNAL())
}

ColorValueWidget::~ColorValueWidget()
{
    delete ui;
}

QString ColorValueWidget::getTitle()
{
    return _title;
}

void ColorValueWidget::setTitle(QString title)
{
    _title = title;
}

void ColorValueWidget::onColorChanged(QColor color)
{
    Q_UNUSED(color);
}

void ColorValueWidget::onColorSet(QColor color)
{
    Q_UNUSED(color);
}
