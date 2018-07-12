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

#include <QListView>
#include <QStyledItemDelegate>

ComboBoxWidget::ComboBoxWidget(QWidget* parent) : QWidget(parent), ui(new Ui::ComboBoxWidget)
{
    ui->setupUi(this);

    ui->comboBox->setItemDelegate(new QStyledItemDelegate(ui->comboBox));   

    auto lv = new QListView();
    ui->comboBox->setView(lv); 

    connect(ui->comboBox, SIGNAL(currentTextChanged(QString)), SLOT(onDropDownTextChanged(QString)));
    connect(ui->comboBox, SIGNAL(currentIndexChanged(int)), SLOT(onDropDownIndexChanged(int)));
}

ComboBoxWidget::~ComboBoxWidget()
{
    delete ui;
}

void ComboBoxWidget::setLabel(const QString &label)
{
    ui->label->setText(label);
}

void ComboBoxWidget::addItem(const QString &text, const QVariant &data)
{
    ui->comboBox->addItem(text, data);
}

int ComboBoxWidget::findData(const QVariant & data)
{
    return ui->comboBox->findData(data);
}

QString ComboBoxWidget::getCurrentItem()
{
    return ui->comboBox->currentText();
}

QString ComboBoxWidget::getCurrentItemData()
{
    return ui->comboBox->itemData(ui->comboBox->currentIndex()).toString();
}

QVariant ComboBoxWidget::getItemData(int index)
{
    return ui->comboBox->itemData(index);
}

void ComboBoxWidget::setCurrentItem(const QString &item)
{
	ui->comboBox->blockSignals(true);
    ui->comboBox->setCurrentIndex(ui->comboBox->findData(item, Qt::DisplayRole));
	ui->comboBox->blockSignals(false);
}

void ComboBoxWidget::setCurrentItemData(const QString &item)
{
    ui->comboBox->blockSignals(true);
    ui->comboBox->setCurrentIndex(findData(item));
    ui->comboBox->blockSignals(false);
}

void ComboBoxWidget::setCurrentIndex(const int &index)
{
    ui->comboBox->blockSignals(true);
    ui->comboBox->setCurrentIndex(index);
    ui->comboBox->blockSignals(false);
}

void ComboBoxWidget::clear()
{
    ui->comboBox->clear();
}

void ComboBoxWidget::setCurrentText(const QString &text)
{
    ui->comboBox->blockSignals(true);
    ui->comboBox->setCurrentText(text);
    ui->comboBox->blockSignals(false);
}

QComboBox *ComboBoxWidget::getWidget() const
{
    return ui->comboBox;
}

void ComboBoxWidget::onDropDownTextChanged(const QString &text)
{
    emit currentIndexChanged(text);
}

void ComboBoxWidget::onDropDownIndexChanged(int index)
{
    emit currentIndexChanged(index);
}

