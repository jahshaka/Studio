/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "skymapwidget.h"
#include "src/core/thumbnailmanager.h"
#include <QPainter>

SkyMapWidget::SkyMapWidget() : QWidget()
{
	layout = new QGridLayout;
	setLayout(layout);
	top = bottom = left = right = front = back = nullptr;
	setMinimumHeight(180);

	layout->setVerticalSpacing(0);
	layout->setHorizontalSpacing(0);
}

void SkyMapWidget::addTopImage(QString topImagePath)
{
	if (top != nullptr) {
		layout->removeWidget(top);
		delete top;
	}
	top = new CubeMapButton(topImagePath);
	layout->addWidget(top, 0, 1,1,1);
}

void SkyMapWidget::addBottomImage(QString bottomImagePath)
{
	if (bottom != nullptr) {
		layout->removeWidget(bottom);
		delete bottom;
	}
	bottom = new CubeMapButton(bottomImagePath);
	layout->addWidget(bottom, 3, 1,1,1);
}

void SkyMapWidget::addLeftImage(QString leftImagePath)
{
	if (left != nullptr) {
		layout->removeWidget(left);
		delete left;
	}
	left = new CubeMapButton(leftImagePath);
	layout->addWidget(left, 1, 0,1,1);
}

void SkyMapWidget::addRightImage(QString rightImagePath)
{
	if (right != nullptr) {
		layout->removeWidget(right);
		delete right;
	}
	right = new CubeMapButton(rightImagePath);
	layout->addWidget(right, 1, 2,1,1);
}

void SkyMapWidget::addFrontImage(QString frontImagePath)
{
	if (front != nullptr) {
		layout->removeWidget(front);
		delete front;
	}
	front = new CubeMapButton(frontImagePath);
	layout->addWidget(front, 1, 1,1,1);
}

void SkyMapWidget::addBackImage(QString backImagePath)
{
	if (back != nullptr) {
		layout->removeWidget(back);
		delete back;
	}
	back = new CubeMapButton(backImagePath);
	layout->addWidget(back, 1, 3,1,1);
}

void SkyMapWidget::addCubeMapImages(QString top, QString bottom, QString left, QString front, QString right, QString back)
{
	addTopImage(top);
	addBottomImage(bottom);
	addLeftImage(left);
	addFrontImage(front);
	addRightImage(right);
	addBackImage(back);
}

void SkyMapWidget::addCubeMapImages(QStringList list)
{
	if (list.length() < 6) return;
	addCubeMapImages(list[0], list[1], list[2], list[3], list[4], list[5]);
}

CubeMapButton::CubeMapButton(QString imagePath) : QPushButton()
{
	auto thumb = ThumbnailManager::createThumbnail(imagePath, 60, height());
	pixmap = QPixmap::fromImage(*thumb->thumb).scaled(QSize(28, 28));

	setMinimumHeight(60);
}

void CubeMapButton::paintEvent(QPaintEvent* event)
{
	QPushButton::paintEvent(event);

	if (!pixmap.isNull()) {
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.drawPixmap(0, 0, width(), height(), pixmap);
	}
}
