/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "texturepickerwidget.h"
#include "ui_texturepickerwidget.h"
#include "qfiledialog.h"
#include <Qt>
#include "../core/thumbnailmanager.h"
#include "../widgets/assetpickerwidget.h"

#include <QMimeData>
#include <QDrag>
#include <QStandardItemModel>
#include <QDragEnterEvent>

TexturePickerWidget::TexturePickerWidget(QWidget* parent) :
    BaseWidget(parent),
    ui(new Ui::TexturePickerWidget)
{
    ui->setupUi(this);
    this->ui->texture->installEventFilter(this);
    type = WidgetType::TextureWidget;

    connect(ui->load, SIGNAL(pressed()), SLOT(pickTextureMap()));
    setAcceptDrops(true);
}

TexturePickerWidget::~TexturePickerWidget()
{
    delete ui;
}

QString TexturePickerWidget::getTexturePath()
{
    return filePath;
}

void TexturePickerWidget::dragEnterEvent(QDragEnterEvent *event)
{
//    const QString mimeType = "application/x-qabstractitemmodeldatalist";
    if (event->mimeData()->hasText()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TexturePickerWidget::dropEvent(QDropEvent *event)
{
    // http://stackoverflow.com/a/2747369/996468
    const QString mimeType = "application/x-qabstractitemmodeldatalist";
//    QByteArray encoded = event->mimeData()->data(mimeType);
//    QDataStream stream(&encoded, QIODevice::ReadOnly);
//    QMap<int, QVariant> roleDataMap;
//    while (!stream.atEnd()) {
//        int row, col;
//        stream >> row >> col >> roleDataMap;
//    }

    changeMap(event->mimeData()->text());

    event->acceptProposedAction();
}

void TexturePickerWidget::changeTextureMap()
{
    auto file = loadTexture();
    if (file.isEmpty() || file.isNull()) return;
    this->setLabelImage(ui->texture, file);
}

void TexturePickerWidget::pickTextureMap()
{
    auto widget = new AssetPickerWidget(AssetType::Texture);
    connect(widget, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(changeMap(QListWidgetItem*)));
}

QString TexturePickerWidget::loadTexture()
{
    QString dir = QApplication::applicationDirPath();
    return QFileDialog::getOpenFileName(this, "Open Texture File",
                                        dir, "Image Files (*.png *.jpg *.bmp)");
}

void TexturePickerWidget::setLabelImage(QLabel* label, QString file, bool emitSignal)
{
    if (file.isNull() || file.isEmpty()) {
        ui->texture->clear();
    } else {
        auto thumb = ThumbnailManager::createThumbnail(file,
                                                       ui->texture->width(),
                                                       ui->texture->height());
        QPixmap pixmap = QPixmap::fromImage(*thumb->thumb);
        ui->texture->setPixmap(pixmap);

        filePath = file;
        QFileInfo fileInfo(file);
        filename = fileInfo.fileName();
        ui->imagename->setText(filename);
        ui->imagename->setMaximumWidth(200);
        ui->imagename->setWordWrap(true);

        QString dimension_H = QString::number(thumb->originalSize.width());
        QString dimension_W = QString::number(thumb->originalSize.height());
        ui->dimensions->setText(dimension_H + " * " + dimension_W);
    }

    if (emitSignal) emit valueChanged(file);
}

bool TexturePickerWidget::eventFilter(QObject *object, QEvent *ev)
{
    if (object == ui->texture && ev->type() == QEvent::MouseButtonRelease) {
        pickTextureMap();
    }

    return false;
}

void TexturePickerWidget::on_pushButton_clicked()
{
    ui->imagename->clear();
    ui->dimensions->clear();
    ui->texture->clear();

    // new
    filePath.clear();

    emit valueChanged(QString::null);
}

void TexturePickerWidget::changeMap(QListWidgetItem *item)
{
    setLabelImage(ui->texture, item->data(Qt::UserRole).toString());
}

void TexturePickerWidget::changeMap(const QString &texturePath)
{
    setLabelImage(ui->texture, texturePath);
}

void TexturePickerWidget::setTexture(QString path)
{
    if (path.isNull() || path.isEmpty()) {
        ui->texture->clear();
        ui->imagename->clear();
        ui->dimensions->clear();
    } else {
        setLabelImage(ui->texture, path, false);
    }
}
