/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "basepropertywidget.h"
#include <QPushButton>
#include <QDebug>
#include <QPainter>
#include <QtMath>
#include <QMessageBox>
#include <QGraphicsEffect>
#include <QPainterPath>
#include <QEnterEvent>
#include "ui/style/stylesheet.h"


BasePropertyWidget::BasePropertyWidget(QWidget * parent) : QWidget(parent)
{

	setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);

	fontIcons = new QtAwesome;
	fontIcons->initFontAwesome();
	
	displayName = new QLineEdit;
	displayName->setText("Display");

	button = new QPushButton;
	button->setMaximumSize(12, 12);
	button->setIcon(QIcon(":/icons/delete-26.png"));
	button->setCursor(Qt::PointingHandCursor);

	auto minimize = new QPushButton;
	minimize->setMaximumSize(button->maximumSize());
	minimize->setIcon(QIcon(":/icons/contract.png"));
	minimize->setCursor(Qt::PointingHandCursor);

	auto btn = new QPushButton;
	btn->setIcon(QIcon(":/icons/up.png"));
	btn->setIconSize(QSize(14, 14));

	displayWidget = new HeaderObject;
	displayWidget->setStyleSheet(StyleSheet::MaterialsTransparent());
	auto displayLayout = new QHBoxLayout;
	displayWidget->setLayout(displayLayout);
	displayLayout->setSpacing(0);
	displayLayout->addWidget(displayName);
	displayLayout->addStretch();
	displayLayout->addWidget(minimize);
	displayLayout->addSpacing(8 * devicePixelRatio());
	displayLayout->addWidget(button);
	displayLayout->addSpacing(4);
	displayLayout->setContentsMargins(0, 1, 2, 1);

	layout = new QVBoxLayout; 
	auto mainLayout = new QVBoxLayout;
	mainLayout->addWidget(displayWidget);
	mainLayout->addLayout(layout);
	mainLayout->setContentsMargins(5, 0, 5, 0);
	

	setLayout(mainLayout);

	connect(displayName, &QLineEdit::textChanged, [=](QString text) {
		emit TitleChanged(text);
	});

	connect(button, &QPushButton::clicked, [=]() {
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, "Confirmation", QString("Are you sure you wish to delete %1 ?").arg(displayName->text()),
			QMessageBox::Yes | QMessageBox::No);
		if (reply == QMessageBox::Yes) {
			emit buttonPressed(true);
		}
		else {

		}
	});

	connect(minimize, &QPushButton::clicked, [=]() {
		if (minimized) {
			// maximize
			minimize->setIcon(QIcon(":/icons/contract.png"));
			minimized = !minimized;
			contentWidget->setVisible(true);
		}
		else {
			// minimize
			minimize->setIcon(QIcon(":/icons/expand.png"));
			minimized = !minimized;
			contentWidget->setVisible(false);
		}
	});
	

	displayName->setStyleSheet(StyleSheet::MaterialsPropertyName());

	setStyleSheet(StyleSheet::MaterialsPropertyMenu());


}


BasePropertyWidget::~BasePropertyWidget()
{
}


void BasePropertyWidget::setWidget(QWidget * widget)
{
	contentWidget = widget;
	layout->addWidget(contentWidget);

}


void BasePropertyWidget::paintEvent(QPaintEvent * event)
{
	QWidget::paintEvent(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
    // painter.setRenderHint(QPainter::HighQualityAntialiasing);

	// draw title border
	QPainterPath path;
	path.addRect(0, 0, width(), displayWidget->height());
	painter.fillPath(path, QColor(22, 22, 22));

	//draw outline
	painter.setPen(QPen(QColor(22,22,22), 4));
	painter.drawRect(0, 0, width(), height());

}

void BasePropertyWidget::mouseMoveEvent(QMouseEvent * event)
{
	pressed = true;
	QWidget::mouseMoveEvent(event);
}

void BasePropertyWidget::mousePressEvent(QMouseEvent * event)
{
	emit currentWidget(this);
	QWidget::mousePressEvent(event);
}

void BasePropertyWidget::mouseReleaseEvent(QMouseEvent * event)
{
	pressed = false;
	QWidget::mouseReleaseEvent(event);
}

HeaderObject::HeaderObject() : QWidget()
{
	setCursor(Qt::OpenHandCursor);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void HeaderObject::mousePressEvent(QMouseEvent * event)
{
	QWidget::mousePressEvent(event);
	setCursor(Qt::ClosedHandCursor);
}

void HeaderObject::mouseReleaseEvent(QMouseEvent * event)
{
	QWidget::mouseReleaseEvent(event);
	setCursor(Qt::OpenHandCursor);
}

void HeaderObject::enterEvent(QEvent * event)
{
    QWidget::enterEvent(static_cast<QEnterEvent*>(event));
	setCursor(Qt::OpenHandCursor);
}

WideRangeSpinBox::WideRangeSpinBox(QWidget *parent) : QDoubleSpinBox(parent)
{
	setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    setRange(-1000, 1000);
	setStyleSheet(StyleSheet::MaterialsPropertyDoubleSpin());
}


WideRangeIntBox::WideRangeIntBox(QWidget *parent) : QSpinBox(parent)
{
	setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
	setRange(-INT32_MAX, INT32_MAX);
	setRange(-1000, 1000);
	setStyleSheet(StyleSheet::MaterialsPropertySpin());
}


