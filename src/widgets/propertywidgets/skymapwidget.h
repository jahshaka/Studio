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
#include <QLabel>
#include <QGridLayout>
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
	void setStringPosition(QString string);

	QVBoxLayout* layout;
	QImage image;
	QPixmap pixmap;
	Rotation rotation;
	QLabel* positionLabel;
	QWidget* container;

	QPushButton* select;
	QPushButton* clear;


	bool drawPositionString = false;
private:
	void configureConnections();
	void configureUi();
protected:
	void paintEvent(QPaintEvent* event) override;
	virtual void enterEvent(QEvent* event) override;
	virtual void leaveEvent(QEvent* event) override;
};

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

	CubeMapButton * top, * left, * front, * right, * back, * bottom;
};


#endif
