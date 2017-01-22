/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "texturepicker.h"
#include "ui_texturepicker.h"
#include "qfiledialog.h"
#include <Qt>
#include "../core/thumbnailmanager.h"

TexturePicker::TexturePicker(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TexturePicker)
{
    ui->setupUi(this);
    connect(ui->load, SIGNAL(pressed()), SLOT(changeTextureMap()));

    this->ui->texture->installEventFilter(this);
}

TexturePicker::~TexturePicker()
{
    delete ui;
}

void TexturePicker::changeTextureMap()
{
    auto file = loadTexture();

    if (file.isEmpty() || file.isNull()) {
        return;
    }

    this->setLabelImage(ui->texture, file);
}

QString TexturePicker::loadTexture()
{
    QString dir = QApplication::applicationDirPath();
    return QFileDialog::getOpenFileName(this, "Open Texture File",
                                        dir, "Image Files (*.png *.jpg *.bmp)");
}

void TexturePicker::setLabelImage(QLabel* label, QString file, bool emitSignal)
{
    if (file.isNull() || file.isEmpty()) {
        ui->texture->clear();
    } else {
        auto thumb = ThumbnailManager::createThumbnail(file,
                                                       ui->texture->width(),
                                                       ui->texture->height());
        QPixmap pixmap = QPixmap::fromImage(*thumb->thumb);
        ui->texture->setPixmap(pixmap);

        QFileInfo fileInfo(file);
        filename = fileInfo.fileName();
        ui->imagename->setText(filename);
        ui->imagename->setMaximumWidth(200);
        ui->imagename->setWordWrap(true);

        QString dimension_H = QString::number(thumb->originalSize.width());
        QString dimension_W = QString::number(thumb->originalSize.height());
        ui->dimensions->setText(dimension_H + " X " + dimension_W);
    }

    if (emitSignal) emit valueChanged(file);
}

bool TexturePicker::eventFilter(QObject *object, QEvent *ev)
{
    if (object == ui->texture && ev->type() == QEvent::MouseButtonRelease) {
        changeTextureMap();
    }

    return false;
}

void TexturePicker::on_pushButton_clicked()
{
    ui->imagename->clear();
    ui->dimensions->clear();
    ui->texture->clear();

    emit valueChanged(QString::null);
}

void TexturePicker::setTexture(QString path)
{
    if (path.isNull() || path.isEmpty()) {
        ui->texture->clear();
    } else {
        setLabelImage(ui->texture, path, false);
    }
}
