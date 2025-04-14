/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "getnamedialog.h"
#include "ui_getnamedialog.h"

GetNameDialog::GetNameDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::GetNameDialog)
{
    ui->setupUi(this);
}

GetNameDialog::~GetNameDialog()
{
    delete ui;
}

void GetNameDialog::setName(QString name)
{
    ui->lineEdit->setText(name);
    ui->lineEdit->selectAll();
}

QString GetNameDialog::getName()
{
    return ui->lineEdit->text();
}
