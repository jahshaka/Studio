/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "checkboxwidget.h"
#include "ui_checkboxwidget.h"
#include <QCheckBox>

CheckBoxWidget::CheckBoxWidget(QWidget* parent) : QWidget(parent), ui(new Ui::CheckBoxWidget)
{
    ui->setupUi(this);

    connect(ui->checkBox, SIGNAL(stateChanged(int)), SLOT(onCheckboxValueChanged(int)));
}

CheckBoxWidget::~CheckBoxWidget()
{
    delete ui;
}

void CheckBoxWidget::setValue(bool val)
{
    ui->checkBox->setChecked(val);
}

bool CheckBoxWidget::getValue()
{
    return ui->checkBox->checkState();
}

void CheckBoxWidget::setLabel(QString label)
{
    ui->label->setText(label);
}

void CheckBoxWidget::onCheckboxValueChanged(int val)
{
    emit valueChanged(val);
}
