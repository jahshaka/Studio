/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/textinputwidget.h"
#include "ui_textinputwidget.h"
#include "ui/style/thememanager.h"

TextInputWidget::TextInputWidget(QWidget* parent) : QWidget(parent), ui(new Ui::TextInputWidget)
{
    ui->setupUi(this);
    // Qlementine owns this subtree: drop the .ui-embedded classic sheets right
    // here, before any runtime sheet is applied, so the QStyle paints instead of
    // dark-on-dark #212121 blocks nothing can reach (VISUAL_PARITY re-audit F3;
    // no-op under the Classic theme, which those sheets ARE).
    ThemeManager::clearClassicSheets(this);
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
