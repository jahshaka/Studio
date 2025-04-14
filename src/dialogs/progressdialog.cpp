/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "progressdialog.h"
#include "ui_progressdialog.h"

#include <QApplication>

ProgressDialog::ProgressDialog(QDialog *parent) : QDialog(parent), ui(new Ui::ProgressDialog)
{
    ui->setupUi(this);
    this->setWindowFlags(Qt::FramelessWindowHint);
	setWindowModality(Qt::WindowModal);
}

ProgressDialog::~ProgressDialog()
{
    delete ui;
}

void ProgressDialog::setLabelText(const QString &text)
{
    ui->label->setText(text);
    QApplication::processEvents();
}

void ProgressDialog::reset()
{
    ui->progressBar->reset();
}

void ProgressDialog::setRange(int min, int max)
{
    ui->progressBar->setRange(min, max);
}

void ProgressDialog::setValue(int val)
{
    ui->progressBar->setValue(val);
	QApplication::processEvents();
}

void ProgressDialog::setValueAndText(int value, QString text)
{
	setValue(value);
	setLabelText(text);
}

