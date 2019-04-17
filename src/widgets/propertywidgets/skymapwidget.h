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

enum class CubeMapPosition {
	top = 4,
	left = 2,
	front = 0,
	right = 3,
	back = 1,
	bottom = 5
};

class SkyMapWidget;
class CubeMapButton : public QPushButton {

	enum class Rotation {
		Zero = 0,
		Ninety = 90,
		OneEighty = 180,
		TwoSeventy = 270,
		ThreeSixty = 0
	};

public:
	CubeMapButton(QString imagePath, SkyMapWidget* parent);
	void setStringPosition(QString string);
	void setPosition(CubeMapPosition pos);
	void setImage(QString path);

	void selectImage();
	void clearImage();
	void rotateImage(int degrees);
	void flipImage(Qt::Orientation orientation);

	bool flipedHorizontal = false;
	bool flipedVertical = false;
	bool shouldEmit = false;

	QString textureGuid;
	QString path;
	CubeMapPosition position;
	QVBoxLayout* layout;
	QImage image;
	QPixmap pixmap;
	Rotation rotation = Rotation::Zero;
	QLabel* positionLabel;
	QWidget* container;
	QPushButton* select;
	QPushButton* clear;
	SkyMapWidget* parent;
private:
	void configureConnections();
	void configureUi();
protected:
	void paintEvent(QPaintEvent* event) override;
	virtual void enterEvent(QEvent* event) override;
	virtual void leaveEvent(QEvent* event) override;
};

class SkyMapWidget : public QWidget {
	Q_OBJECT
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
	void removeCubeMapImageIfPresent(CubeMapButton* btn);

	CubeMapButton * top, * left, * front, * right, * back, * bottom;

signals:
	void valueChanged(QString value, CubeMapPosition pos);
	void valuesChanged(QString value, QString guid, CubeMapPosition pos);
	void rotationChanged(int degree, CubeMapPosition pos);
	void orientationFlipChanged(Qt::Orientation orientation, CubeMapPosition pos);
};


#endif
