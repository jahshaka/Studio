/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "skymapwidget.h"
#include <QPainter>

SkyMapWidget::SkyMapWidget() : QWidget()
{
	layout = new QGridLayout;
	setLayout(layout);
}

void SkyMapWidget::addTopImage(QString topImagePath)
{
	auto btn = layout->takeAt(1);
	if (btn != 0) delete btn;
	auto top = new CubeMapButton(topImagePath);
	layout->addWidget(top, 0, 1);
}

void SkyMapWidget::addBottomImage(QString bottomImagePath)
{
	auto btn = layout->takeAt(9);
	if (btn != 0) delete btn;
	auto bottom = new CubeMapButton(bottomImagePath);
	layout->addWidget(bottom, 2, 1);
}

void SkyMapWidget::addLeftImage(QString leftImagePath)
{
	auto btn = layout->takeAt(4);
	if (btn != 0) delete btn;
	auto left = new CubeMapButton(leftImagePath);
	layout->addWidget(left, 1, 0);
}

void SkyMapWidget::addRightImage(QString rightImagePath)
{
	auto btn = layout->takeAt(6);
	if (btn != 0) delete btn;
	auto right = new CubeMapButton(rightImagePath);
	layout->addWidget(right, 1, 2);
}

void SkyMapWidget::addFrontImage(QString frontImagePath)
{
	auto btn = layout->takeAt(5);
	if (btn != 0) delete btn;

	auto front = new CubeMapButton(frontImagePath);
	layout->addWidget(front, 1, 1);
}

void SkyMapWidget::addBackImage(QString backImagePath)
{
	auto btn = layout->takeAt(7);
	if (btn != 0) delete btn;
	auto back = new CubeMapButton(backImagePath);
	layout->addWidget(back, 1, 3);
}

void SkyMapWidget::addCubeMapImages(QString top, QString bottom, QString left, QString front, QString right, QString back)
{
	addTopImage(top);
	addBackImage(bottom);
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
	image = new QImage(imagePath);
}

void CubeMapButton::paintEvent(QPaintEvent* event)
{
	QPushButton::paintEvent(event);

	if (image)
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.drawImage(0, 0, *image);
	}
}
