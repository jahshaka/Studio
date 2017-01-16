/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "filepickerwidget.h"
#include "ui_filepickerwidget.h"
#include "qfiledialog.h"
#include <Qt>
#include <QDebug>

FilePickerWidget::FilePickerWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FilePickerWidget)
{
    ui->setupUi(this);
    connect(ui->load, SIGNAL(pressed()), this, SLOT(filePicker()));

}

FilePickerWidget::~FilePickerWidget()
{
    delete ui;
}

void FilePickerWidget::filePicker(){
    auto file = loadTexture();

    if(file.isNull() || file.isEmpty())
    {
        return;
    }
    else
    {
        QFileInfo fileInfo(file);
        filename = fileInfo.fileName();
        filepath = fileInfo.filePath();
        ui->filename->setText(filename);
        ui->filename->setToolTip(filepath);

    }
}

QString FilePickerWidget::loadTexture()
{
    QString dir = QApplication::applicationDirPath();
    return QFileDialog::getOpenFileName(this,"Open Texture File",dir,file_extentions);
}
