/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/labelwidget.h"
#include "ui_labelwidget.h"

LabelWidget::LabelWidget(QWidget* parent) : QWidget(parent), ui(new Ui::LabelWidget)
{
    ui->setupUi(this);

    // PROSE CANNOT DRIVE THE DOCK WIDTH (root cause of "the World panel shows
    // no controls", 2026-09-08). A QLabel does not wrap by default, so its
    // MINIMUM width is the full width of its text — and this row's display
    // label carries whatever a panel hands it, including the Rayon section's
    // 700-character explanation of the GI update budget. That single row asked
    // for 3674 px, the panel's minimum became 3815 px, and because the dock's
    // scroll area is widgetResizable with the horizontal scrollbar OFF, every
    // row was stretched to that width: the controls were laid out ~1600 px to
    // the right of a 315 px dock, visible and unreachable.
    //
    // Wrapping is the class-level end of it: a wrapped label's minimum width is
    // one word, not one paragraph. (The long text still belongs in a tooltip —
    // see worldgipropertywidget.cpp — but no panel can break the dock this way
    // again.)
    ui->display->setWordWrap(true);
    ui->display->setMinimumWidth(0);
    ui->label->setMinimumWidth(0);

    // for some reason the palette cannot be set and stylesheet changes
    // don't apply after the main app is run, set any styles here... !
    //ui->lineEdit->setStyleSheet("background-color: red; padding: 8px; margin: 0");
}

LabelWidget::~LabelWidget()
{

}

void LabelWidget::setLabel(const QString& label)
{
    ui->label->setText(label);
}

void LabelWidget::setText(const QString& text)
{
    ui->display->setText(text);
}

void LabelWidget::clearText()
{
    ui->display->clear();
}

void LabelWidget::onTextInputChanged(const QString& val)
{

}
