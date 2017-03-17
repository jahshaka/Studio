/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "textinputwidget.h"
#include "ui_textinputwidget.h"

TextInputWidget::TextInputWidget(QWidget* parent) : QWidget(parent), ui(new Ui::TextInputWidget)
{
    ui->setupUi(this);
}

TextInputWidget::~TextInputWidget()
{
    delete ui;
}

void TextInputWidget::setLabel(const QString& label)
{
    ui->label->setText(label);
}

void TextInputWidget::setText(const QString& text)
{
    ui->text->setText(text);
}

void TextInputWidget::clearText()
{
    ui->text->clear();
}

QString TextInputWidget::getText()
{
    return ui->text->text();
}

void TextInputWidget::onTextInputChanged(const QString& val)
{

}
