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
#include "../core/project.h"
#include "../core/thumbnailmanager.h"
#include "../widgets/assetpickerwidget.h"
#include "irisgl/src/core/irisutils.h"
#include "globals.h"
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

    connect(ui->clear, &QPushButton::pressed, this, &TexturePickerWidget::clear);
    connect(ui->load, &QPushButton::pressed, this, &TexturePickerWidget::changeTextureMap);

    ui->clear->setIcon(QIcon(":/icons/icons8-synchronize-26.png"));

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
	if (event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
		event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TexturePickerWidget::dropEvent(QDropEvent *event)
{
    // http://stackoverflow.com/a/2747369/996468
	QByteArray encoded = event->mimeData()->data("application/x-qabstractitemmodeldatalist");
	QDataStream stream(&encoded, QIODevice::ReadOnly);
	QMap<int, QVariant> roleDataMap;
	while (!stream.atEnd()) stream >> roleDataMap;

	if (roleDataMap.value(0).toInt() == static_cast<int>(ModelTypes::Texture)) {
		changeMap(IrisUtils::join(Globals::project->getProjectFolder(), "Textures", roleDataMap.value(1).toString()));
	}

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
    auto widget = new AssetPickerWidget(ModelTypes::Texture);
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
        QPixmap pixmap = QPixmap::fromImage(*thumb->thumb).scaled(QSize(28, 28));
        ui->texture->setPixmap(pixmap);

        filePath = file;

		QString dimension_H = QString::number(thumb->originalSize.width());
        QString dimension_W = QString::number(thumb->originalSize.height());
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

void TexturePickerWidget::clear()
{
    ui->texture->clear();
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
        //ui->dimensions->clear();
    } else {
        setLabelImage(ui->texture, path, false);
    }
}
