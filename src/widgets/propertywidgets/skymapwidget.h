/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SKYMAPWIDGET_H
#define SKYMAPWIDGET_H

#include <QWidget>
#include <QPushButton>
#include <QGridLayout>


class SkyMapWidget : public QWidget {

public:
	SkyMapWidget();

	QGridLayout* layout;
	void addTopImage(QString topImagePath);
	void addBottomImage(QString bottomImagePath);
	void addLeftImage(QString leftImagePath);
	void addRightImage(QString rightImagePath);
	void addFrontImage(QString frontImagePath);
	void addBackImage(QString backImagePath);
	void addCubeMapImages(QString top, QString bottom, QString left, QString front, QString right, QString back);
	void addCubeMapImages(QStringList list);
};

class CubeMapButton : public QPushButton {

	enum class Rotation {
		zero = 0,
		ninety = 90,
		OneEighty = 180,
		TwoSeventy = 270,
		ThreeSixty = 0
	};

public:
	CubeMapButton(QString imagePath);
	QHBoxLayout* layout;
	QImage* image;
	Rotation rotation;

protected:
	void paintEvent(QPaintEvent* event) override;
};
#endif
