/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "cubemapwidget.h"
#include "src/core/thumbnailmanager.h"
#include "widgets/assetpickerwidget.h"
#include "globals.h"
#include "misc/stylesheet.h"
#include <QPainter>
#include <QMimeData>
#include <QSharedPointer>
#include <QGraphicsEffect>
#include <QAction>
#include <QMenu>
#include <QTransform>

CubeMapWidget::CubeMapWidget(QWidget *parent) : QWidget(parent)
{
	layout = new QGridLayout;
	setLayout(layout);
	top = bottom = left = right = front = back = nullptr;
	setMinimumHeight(180);
	layout->setVerticalSpacing(0);
	layout->setHorizontalSpacing(0);
}

void CubeMapWidget::addTopImage(QString topImagePath)
{
	removeCubeMapImageIfPresent(top);
	top = new CubeMapButton(topImagePath, this);
	top->setPosition(CubeMapPosition::top);
	layout->addWidget(top, 0, 1,1,1);
}

void CubeMapWidget::addBottomImage(QString bottomImagePath)
{
	removeCubeMapImageIfPresent(bottom);
	bottom = new CubeMapButton(bottomImagePath, this);
	bottom->setPosition(CubeMapPosition::bottom);
	layout->addWidget(bottom, 3, 1,1,1);
}

void CubeMapWidget::addLeftImage(QString leftImagePath)
{
	removeCubeMapImageIfPresent(left);
	left = new CubeMapButton(leftImagePath, this);
	left->setPosition(CubeMapPosition::left);
	layout->addWidget(left, 1, 0,1,1);
}

void CubeMapWidget::addRightImage(QString rightImagePath)
{
	removeCubeMapImageIfPresent(right);
	right = new CubeMapButton(rightImagePath, this);
	right->setPosition(CubeMapPosition::right);
	layout->addWidget(right, 1, 2,1,1);
}

void CubeMapWidget::addFrontImage(QString frontImagePath)
{
	removeCubeMapImageIfPresent(front);
	front = new CubeMapButton(frontImagePath, this);
	front->setPosition(CubeMapPosition::front);
	layout->addWidget(front, 1, 1,1,1);
}

void CubeMapWidget::addBackImage(QString backImagePath)
{
	removeCubeMapImageIfPresent(back);
	back = new CubeMapButton(backImagePath, this);
	back->setPosition(CubeMapPosition::back);
	layout->addWidget(back, 1, 3,1,1);
}

void CubeMapWidget::addCubeMapImages(QString top, QString bottom, QString left, QString front, QString right, QString back)
{
	addTopImage(top);
	addBottomImage(bottom);
	addLeftImage(left);
	addFrontImage(front);
	addRightImage(right);
	addBackImage(back);
}

void CubeMapWidget::addCubeMapImages(QStringList list)
{
	if (list.length() < 6) return;
	addCubeMapImages(list[0], list[1], list[2], list[3], list[4], list[5]);
}

void CubeMapWidget::removeCubeMapImageIfPresent(CubeMapButton* btn)
{
	if (btn != nullptr) {
		layout->removeWidget(btn);
		delete btn;
	}
}

CubeMapButton::CubeMapButton(QString imagePath, CubeMapWidget* parent) : QPushButton()
{
	path = imagePath;
	this->parent = parent;
	setImage(imagePath);
	configureUi();
	setMinimumHeight(60);
	shouldEmit = true;
	setAcceptDrops(true);
}

void CubeMapButton::setStringPosition(QString string)
{
	positionLabel->setText(string);
}

void CubeMapButton::setPosition(CubeMapPosition pos)
{
	position = pos;
	switch (pos) {
	case CubeMapPosition::top:
		setStringPosition("top");
		break;
	case CubeMapPosition::left:
		setStringPosition("left");
		break;
	case CubeMapPosition::front:
		setStringPosition("front");
		break;
	case CubeMapPosition::right:
		setStringPosition("right");
		break;
	case CubeMapPosition::back:
		setStringPosition("back");
		break;
	case CubeMapPosition::bottom:
		setStringPosition("bottom");
		break;
	}
}

void CubeMapButton::setImage(QString path)
{
	if (path.isNull() || path.isEmpty()) {
		image = QImage();
	}
	else {
		
		auto thumb = ThumbnailManager::createThumbnail(path, 60, height());
		image = *thumb->thumb;
		//the path may be invalid returning an invalid image;
		//if (!image.isNull()) image = image.scaled(QSize(28, 28));
	}
	this->path = path;

	if (shouldEmit) {
		emit parent->valueChanged(path, position);
		emit parent->valuesChanged(path, textureGuid, position);
	}
}

void CubeMapButton::configureUi()
{
	layout = new QVBoxLayout;
	positionLabel = new QLabel();
	container = new QWidget;
	select = new QPushButton("Select");
	clear = new QPushButton("Clear");
	auto containerLayout = new QVBoxLayout;
	container->setLayout(containerLayout);

	containerLayout->addWidget(positionLabel);
	layout->addWidget(container);

	if(!image.isNull()) container->setVisible(false);
	setCursor(Qt::PointingHandCursor);
	setLayout(layout);

	container->setStyleSheet(StyleSheet::QWidgetTransparent());
	positionLabel->setStyleSheet(StyleSheet::QLabelWhite());
	select->setStyleSheet(StyleSheet::QPushButtonGreyscale());
	clear->setStyleSheet(StyleSheet::QPushButtonGreyscale());

	//add dropShadow to QLabel to stand out from backgrounds
	auto effect = new QGraphicsDropShadowEffect(positionLabel);
	effect->setBlurRadius(10);
	effect->setOffset(0);
	effect->setColor(QColor(0, 0, 0, 200));
	positionLabel->setGraphicsEffect(effect);
	positionLabel->setAlignment(Qt::AlignCenter);

	QFont f = positionLabel->font();
    f.setWeight(QFont::Bold);
	positionLabel->setFont(f);

	layout->setContentsMargins(0, 0, 0, 0);
	containerLayout->setContentsMargins(2, 2, 2, 2);
}

void CubeMapButton::paintEvent(QPaintEvent* event)
{
	QPushButton::paintEvent(event);
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	if (!image.isNull()) {
		//rotate image and then draw it 
		QPoint center = image.rect().center();
        QTransform  matrix;
		matrix.translate(center.x(), center.y());
		matrix.rotate((int)this->rotation);
		auto intermedaryImage = image.transformed(matrix);

		if (flipedHorizontal) intermedaryImage = intermedaryImage.mirrored(true, false);
		if (flipedVertical) intermedaryImage = intermedaryImage.mirrored(false, true);
	
		painter.drawImage(QRect(0, 0, width(), height()), intermedaryImage);
	}
	else {
		painter.fillRect(0, 0, width(), height(), QColor(80, 80, 80));
		painter.setPen(QPen(QColor(0, 0, 0, 50), 2));
		painter.drawRect(0, 0, width(), height());
	}
	if (container->isVisible()) {
		painter.setPen(QPen(QColor(20,20,20, 40), 2));
		painter.drawRect(0, 0, width(), height());
	}
}

void CubeMapButton::enterEvent(QEvent* event)
{
    QPushButton::enterEvent(static_cast<QEnterEvent*>(event));
	container->setVisible(true);
}

void CubeMapButton::leaveEvent(QEvent* event)
{
	QPushButton::leaveEvent(event);
	if(!image.isNull()) container->setVisible(false);
}

void CubeMapButton::dragEnterEvent(QDragEnterEvent* event)
{
	if (event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
		event->acceptProposedAction();
	}
	else {
		event->ignore();
	}
}

void CubeMapButton::dropEvent(QDropEvent* event)
{
	// http://stackoverflow.com/a/2747369/996468
	QByteArray encoded = event->mimeData()->data("application/x-qabstractitemmodeldatalist");
	QDataStream stream(&encoded, QIODevice::ReadOnly);
	QMap<int, QVariant> roleDataMap;
	while (!stream.atEnd()) stream >> roleDataMap;

	// extend getting guid to filepicker dialog...
	if (roleDataMap.value(0).toInt() == static_cast<int>(ModelTypes::Texture)) {
		textureGuid = roleDataMap.value(3).toString();
		setImage(IrisUtils::join(Globals::project->getProjectFolder(), roleDataMap.value(1).toString()));
	}

	event->acceptProposedAction();
}

void CubeMapButton::mousePressEvent(QMouseEvent* e)
{
	if (e->button() == Qt::LeftButton) {
		selectImage();
	}
	else {
		auto select = new QAction("Select");
		auto clear = new QAction("Clear");

		auto rotate360 = new QAction("Rotate image to 360 degrees");
		auto rotate90 = new QAction("Rotate image to 90 degrees");
		auto rotate180 = new QAction("Rotate image to 180 degrees");
		auto rotate270 = new QAction("Rotate image to 270 degrees");

		auto flipHorizontal = new QAction("Flip Horizontally");
		auto flipVertical = new QAction("Flip Vertically");

		auto rotate = new QMenu("Rotate");
		auto flip = new QMenu("Flip");
		rotate->addActions({ rotate90, rotate180, rotate270, rotate360 });
		flip->addActions({ flipHorizontal, flipVertical });

		auto menu = new QMenu();
		menu->addActions({ select, clear });
		if (!image.isNull()) menu->addMenu(rotate);
		if (!image.isNull()) menu->addMenu(flip);
		menu->setStyleSheet(StyleSheet::QMenu());
		flip->setStyleSheet(StyleSheet::QMenu());
		rotate->setStyleSheet(StyleSheet::QMenu());

		connect(select, &QAction::triggered, [=]() {
			selectImage();
			});
		connect(clear, &QAction::triggered, [=]() {
			clearImage();
			});
		connect(rotate360, &QAction::triggered, [=]() {
			rotateImage(360);
			});
		connect(rotate90, &QAction::triggered, [=]() {
			rotateImage(90);
			});
		connect(rotate180, &QAction::triggered, [=]() {
			rotateImage(180);
			});
		connect(rotate270, &QAction::triggered, [=]() {
			rotateImage(270);
			});
		connect(flipHorizontal, &QAction::triggered, [=]() {
			flipImage(Qt::Orientation::Horizontal);
			});
		connect(flipVertical, &QAction::triggered, [=]() {
			flipImage(Qt::Orientation::Vertical);
			});
		menu->exec(mapToGlobal(QPoint(0, 0)));
	}
}

void CubeMapButton::selectImage()
{
	auto widget = new AssetPickerWidget(ModelTypes::Texture);
	connect(widget, &AssetPickerWidget::itemDoubleClicked, [=](QListWidgetItem * item) {
		setImage(item->data(Qt::UserRole).toString());
	}); 
}

void CubeMapButton::clearImage()
{
	path.clear();
	pixmap = QPixmap();
	image = QImage();
	if (shouldEmit) {
		emit parent->valueChanged(path, position);
		emit parent->valuesChanged(path, textureGuid, position);
	}
}

void CubeMapButton::rotateImage(int degrees)
{
	switch (degrees) {
		case static_cast<int>(Rotation::Ninety) :
			rotation = Rotation::Ninety;
			break;
		case static_cast<int>(Rotation::OneEighty):
			rotation = Rotation::OneEighty;
			break;
		case static_cast<int>(Rotation::TwoSeventy) :
			rotation = Rotation::TwoSeventy;
			break;
		case static_cast<int>(Rotation::ThreeSixty) :
			rotation = Rotation::ThreeSixty;
			break;
		default:
			rotation = Rotation::Zero;
			break;
	}
	emit parent->rotationChanged((int)rotation, position);
}

void CubeMapButton::flipImage(Qt::Orientation orientation)
{
	if (orientation == Qt::Orientation::Horizontal) {
		flipedHorizontal = !flipedHorizontal;
		emit parent->orientationFlipChanged(orientation, position);
	}
	if (orientation == Qt::Orientation::Vertical) {
		flipedVertical = !flipedVertical;
		emit parent->orientationFlipChanged(orientation, position);
	}
}
