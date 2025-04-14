/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "toast.h"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QStyle>
#include <QTimer>
#include <QDebug>

Toast::Toast(QWidget *parent) : QFrame(parent)
{
	setParent(parent);
	setObjectName("Toast");
	toastLayout = new QVBoxLayout;
	setLayout(toastLayout);
	caption = new QLabel;
	caption->setObjectName("Caption");
	info = new QLabel;
	info->setObjectName("Info");
	toastLayout->addWidget(caption);
	toastLayout->addSpacing(6);
	toastLayout->addWidget(info);

	setWindowFlags(
		Qt::Window						| // Add if popup doesn't show up
		Qt::FramelessWindowHint			| // No window border
		Qt::WindowDoesNotAcceptFocus	| // No focus
		Qt::WindowStaysOnTopHint		  // Always on top
	);

	setStyleSheet(
		"QWidget#Toast { background: #1E1E1E; border: 1px solid #3498db; }"
		"QLabel { color: #EEE; }"
		"QLabel#Caption { font-style: bold; font-size: 16px; }"
	);

	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	//setGeometry(QStyle::alignedRect(Qt::RightToLeft, Qt::AlignTop, size(), QDesktopWidget().availableGeometry()));

	//QRect position = frameGeometry();
	//position.moveCenter(parent->rect().center());
	//move(position.topLeft());


	setAttribute(Qt::WA_ShowWithoutActivating);
	//setGeometry(QStyle::alignedRect(
	//	Qt::LeftToRight,
	//	Qt::AlignTop,
	//	size(),
	//	parent->rect())
	//);
		//qApp->desktop()->availableGeometry()));

	setLineWidth(1);
	//QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect();
	//effect->setColor(QColor(32, 32, 32));
	//effect->setBlurRadius(12); 
	//this->setGraphicsEffect(effect);
}

void Toast::showToast(const QString &title, const QString &text, const float &delay)
{
	caption->setText(title);
	info->setText(text);
	show();
}

void Toast::showToast(const QString &title, const QString &text, const float &delay, const QPoint &pos, const QRect &rect)
{
	caption->setText(title);
	info->setText(text);

	QTimer::singleShot(1650, this, &Toast::hide);

	//QRect position = frameGeometry();
	//position.moveBottomRight(QPoint(rect.width() - position.width(), rect.height() - position.height()));
	//move(position.bottomRight());

	//setGeometry(QStyle::alignedRect(Qt::RightToLeft, Qt::AlignBottom, QSize(frameGeometry().width(), frameGeometry().height()), rect));

	show();
}
