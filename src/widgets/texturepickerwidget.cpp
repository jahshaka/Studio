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

TexturePickerWidget::TexturePickerWidget(QWidget *parent) :
	BaseWidget(parent),
	ui(new Ui::TexturePickerWidget)
{
	ui->setupUi(this);
	this->ui->texture->installEventFilter(this);
	type = WidgetType::TextureWidget;

	connect(ui->clear, &QPushButton::pressed, this, &TexturePickerWidget::clear);
	connect(ui->load, &QPushButton::pressed, this, &TexturePickerWidget::pickTextureMap);
	ui->load->hide(); // hide this till new widget

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
	if (event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
		event->acceptProposedAction();
	}
	else {
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
		textureGuid = roleDataMap.value(3).toString();
		changeMap(IrisUtils::join(Globals::project->getProjectFolder(), roleDataMap.value(1).toString()));
	}

	event->acceptProposedAction();
}

void TexturePickerWidget::pickTextureMap()
{
	auto widget = new AssetPickerWidget(ModelTypes::Texture);
	connect(widget, &AssetPickerWidget::itemDoubleClicked, [=](QListWidgetItem *item) {
		changeMap(item);
		});
}

void TexturePickerWidget::setLabelImage(QLabel *label, QString file, bool emitSignal)
{
	if (file.isNull() || file.isEmpty()) {
		ui->texture->clear();
	}
	else {
		auto thumb = ThumbnailManager::createThumbnail(file, ui->texture->width(), ui->texture->height());
		QPixmap pixmap = QPixmap::fromImage(*thumb->thumb).scaled(QSize(28, 28));
		ui->texture->setPixmap(pixmap);
		filePath = file;
	}

	if (emitSignal) {
		emit valueChanged(file);
		emit valuesChanged(file, textureGuid);
	}
}

bool TexturePickerWidget::eventFilter(QObject *object, QEvent *ev)
{
	if (object == ui->texture && ev->type() == QEvent::MouseButtonRelease) pickTextureMap();
	return false;
}

void TexturePickerWidget::clear()
{
	ui->texture->clear();
	filePath.clear();
	textureGuid.clear();
    emit valueChanged(QString());
	emit valuesChanged(QString(), QString());
}

void TexturePickerWidget::changeMap(QListWidgetItem *item)
{
	textureGuid = item->data(MODEL_GUID_ROLE).toString();
	setLabelImage(ui->texture, item->data(Qt::UserRole).toString());
}

void TexturePickerWidget::changeMap(const QString &texturePath)
{
	setLabelImage(ui->texture, texturePath);
}

void TexturePickerWidget::setTexture(QString path)
{
	if (!path.isEmpty() && QFileInfo(path).isFile()) {
		setLabelImage(ui->texture, path, false);
	}
	else {
		ui->texture->clear();
	}
}
