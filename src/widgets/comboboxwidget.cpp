/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "comboboxwidget.h"
#include "ui_comboboxwidget.h"

#include <QDebug>

ComboBoxWidget::ComboBoxWidget(QWidget* parent) : QWidget(parent), ui(new Ui::ComboBoxWidget)
{
    ui->setupUi(this);

    connect(ui->comboBox, SIGNAL(currentTextChanged(QString)), SLOT(onDropDownTextChanged(QString)));
}

ComboBoxWidget::~ComboBoxWidget()
{
    delete ui;
}

void ComboBoxWidget::setLabel(const QString &label)
{
    ui->label->setText(label);
}

void ComboBoxWidget::addItem(const QString &item)
{
    ui->comboBox->addItem(item);
}

QString ComboBoxWidget::getCurrentItem()
{
    return ui->comboBox->currentText();
}

void ComboBoxWidget::setCurrentItem(const QString &item)
{
    ui->comboBox->setCurrentIndex(ui->comboBox->findData(item, Qt::DisplayRole));
}

void ComboBoxWidget::onDropDownTextChanged(const QString &text)
{
    emit currentIndexChanged(text);
}
