/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "custompopup.h"
#include "../misc/stylesheet.h"
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QDebug>

CustomPopup::CustomPopup(QPoint point, Qt::Edge ori) : CustomDialog(Qt::Vertical)
{
	
	holder->setFixedSize(200, 240);
	calculatePoints(ori);

	int x = holder->width() / 2;

	auto tri = new Triangle({x, 0});
	mainLayout->insertWidget(0, tri);
	mainLayout->setSpacing(0);
	auto effect = new QGraphicsDropShadowEffect;
	effect->setOffset(0);
	effect -> setYOffset(-4);
	effect->setBlurRadius(10);
	effect->setColor(QColor(0, 0, 0, 100));
	tri->setGraphicsEffect(effect);

	//calculates geometry of item
	mainLayout->update();
	mainLayout->activate();

	move(point.x() - x -10 , point.y());
}


CustomPopup::~CustomPopup()
{
}

void CustomPopup::addConfirmButton(QString text)
{
	CustomDialog::addConfirmButton(text);
	okBtn->setStyleSheet(StyleSheet::QPushButtonBlueBig());
}

void CustomPopup::addCancelButton(QString text)
{
	CustomDialog::addCancelButton(text);
}

void CustomPopup::calculatePoints(Qt::Edge side)
{
	switch (side) {
	case Qt::TopEdge:
		
		break;
	case Qt::LeftEdge:
		break;
	case Qt::RightEdge:
		break;
	case Qt::BottomEdge:
		break;
	default:
		break;
	}

}




Triangle::Triangle(QPoint point, QWidget *parent) : QWidget(parent)
{

	points[0] = QPoint(point.x() - dist, point.y() + dist );
	points[1] = point;
	points[2] = QPoint(point.x() + dist, point.y() + dist);
	setFixedHeight(dist);
}

void Triangle::paintEvent(QPaintEvent * event)
{
	QPainter painter(this);
	painter.setBrush(QColor(26, 26, 26));
	painter.drawPolygon(points, 3);
}
