/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "checkboxproperty.h"
#include "ui_checkboxproperty.h"
#include <QCheckBox>

CheckBoxProperty::CheckBoxProperty(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::checkboxproperty)
{
    ui->setupUi(this);
    connect(ui->checkBox,SIGNAL(stateChanged(bool)),SLOT(onCheckboxValueChanged(bool)));
    connect(ui->checkBox,SIGNAL(clicked(bool)),SLOT(onCheckboxValueChanged(bool)));
}

CheckBoxProperty::~CheckBoxProperty()
{
    delete ui;
}


void CheckBoxProperty::setValue(bool val)
{
    ui->checkBox->setChecked(val);
}

void CheckBoxProperty::setLabel(QString label)
{
    ui->label->setText(label);
}

void CheckBoxProperty::onCheckboxValueChanged(bool val)
{
    emit valueChanged(val);
}
