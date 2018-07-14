/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/


#include "mswitch.h"

MSwitch::MSwitch(QWidget *parent) : QSlider(parent), wHeight(28)
{
	wWidth = wHeight * 2;
	//setWindowFlags(Qt::FramelessWindowHint |Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
	setFixedSize(wWidth, wHeight);
	setRange(height() / (offset * 2)*factor, (width() - height() + wHeight / (offset * 2 + 1))*factor);

	startColor = QColor(230, 230, 230);
	endColor = QColor(150, 150, 200);
	onColor = QColor(250, 250, 250);

	boundingRect = QRect(offset, offset, width() - offset * 2, height() - offset * 2);
	setCursor(Qt::PointingHandCursor);
}

void MSwitch::setSizeOfSwitch(int a) {
	if (a == wHeight) return;
	wHeight = a;
	wWidth = 2 * a;
	setFixedSize(wWidth, wHeight);
	setGeometry(0, 0, wWidth, wHeight);
	boundingRect = QRect(offset, offset, width() - offset * 2, height() - offset * 2);

	setRange(height() / (offset * 2)*factor, (width() - height() + wHeight / (offset * 2 + 1))*factor);
	setValue(minimum());
}

void MSwitch::simulateClick() {

	QPropertyAnimation *animation = new QPropertyAnimation(this, "sliderPosition");
	QPropertyAnimation *animation1 = new QPropertyAnimation(this, "onColor1");
	if (value() == minimum()) {
		animation->setDuration(300);
		animation->setStartValue(minimum());
		animation->setEndValue(maximum());
		animation->setEasingCurve(QEasingCurve::OutBounce);
		animation->start();
		animation1->setDuration(150);
		animation1->setStartValue(startColor);
		animation1->setEndValue(endColor.darker(120));
		animation1->setEasingCurve(QEasingCurve::OutCurve);
		animation1->start();
		isOn = true;
	}
	else {
		animation->setDuration(300);
		animation->setStartValue(maximum());
		animation->setEndValue(minimum());
		animation->setEasingCurve(QEasingCurve::OutCurve);
		animation->start();
		animation1->setDuration(250);
		animation1->setEndValue(startColor);
		animation1->setStartValue(endColor.darker(120));
		animation1->setEasingCurve(QEasingCurve::OutCurve);
		animation1->start();
		isOn = false;
	}
	emit switchPressed(isOn);

}

void MSwitch::paintEvent(QPaintEvent *event) {
	Q_UNUSED(event);
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	//draw groove
	painter.setPen(QPen(QColor(0, 0, 0, 20), 2));
	painter.drawRoundedRect(boundingRect, height() / 2 - offset, 100);
	//draw on color
	painter.setBrush(onColor1());
	painter.drawRoundedRect(boundingRect, height() / 2 - offset, 100);
	//draw handle
	painter.setBrush(isOn ? endColor : startColor);
	painter.drawEllipse(sliderPosition() / factor + offset, offset * 2, height() - (offset * 4 + 1), height() - (offset * 4 + 1));
}