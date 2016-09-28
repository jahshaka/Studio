/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "renamelayerdialog.h"
#include "ui_renamelayerdialog.h"

RenameLayerDialog::RenameLayerDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::RenameLayerDialog)
{
    ui->setupUi(this);
}

void RenameLayerDialog::setName(QString name)
{
    ui->nameEdit->setText(name);
    ui->nameEdit->selectAll();
}

QString RenameLayerDialog::getName()
{
    return ui->nameEdit->text();
}

RenameLayerDialog::~RenameLayerDialog()
{
    delete ui;
}
