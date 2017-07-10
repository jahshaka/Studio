/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <Qt>
#include <QFileDialog>

#include "filepickerwidget.h"
#include "ui_filepickerwidget.h"

#include "assetpickerwidget.h"

FilePickerWidget::FilePickerWidget(QWidget *parent) :
    BaseWidget(parent),
    ui(new Ui::FilePickerWidget)
{
    ui->setupUi(this);

    connect(ui->load, SIGNAL(pressed()), this, SLOT(filePicker()));

    type = WidgetType::FileWidget;
}

FilePickerWidget::~FilePickerWidget()
{
    delete ui;
}

void FilePickerWidget::filePicker()
{
    auto picker = new AssetPickerWidget(AssetType::Object);
    connect(picker, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(pickFile(QListWidgetItem*)));
//    auto file = openFile();

//    if (file.isNull() || file.isEmpty()) return;
//    else {
//        QFileInfo fileInfo(file);
//        filename = fileInfo.fileName();
//        filepath = fileInfo.filePath();
//        ui->filename->setText(filename);
//        ui->filename->setToolTip(filepath);
//        ui->filename->scroll(ui->filename->width(), 0);

//        emit onPathChanged(filepath);
    //    }
}

void FilePickerWidget::pickFile(QListWidgetItem *item)
{
    QFileInfo fileInfo(item->data(Qt::UserRole).toString());
    filename = fileInfo.fileName();
    filepath = fileInfo.filePath();
    ui->filename->setText(filename);
    ui->filename->setToolTip(filepath);
    ui->filename->scroll(ui->filename->width(), 0);

    emit onPathChanged(filepath);
}

QString FilePickerWidget::getFilepath() const
{
    return filepath;
}

void FilePickerWidget::setFilepath(const QString &value)
{
    QFileInfo fileInfo(value);
    filepath = value;
    filename = fileInfo.fileName();
    ui->filename->setText(fileInfo.fileName());
}

QString FilePickerWidget::openFile()
{
    QString dir = QApplication::applicationDirPath();
    return QFileDialog::getOpenFileName(this, "Open File", dir, suffix);
}
