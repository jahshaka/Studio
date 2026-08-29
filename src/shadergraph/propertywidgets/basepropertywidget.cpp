/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

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
	displayWidget->setStyleSheet("background: rgba(0,0,0,0);");
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
	

	displayName->setStyleSheet("QLineEdit{ background : rgba(29,29,29,0); border-bottom : 2px solid rgba(18,18,18,1); }"
								"QLineEdit:hover{ background: rgba(32,32,32,1); border: 1px solid rgba(0,0,0,1);}"
	);

	setStyleSheet("QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9);}"
		"QMenu::item{padding: 2px 5px 2px 20px;	}"
		"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
		"QMenu::item:selected{	background: rgba(40,128, 185,.9);}"
		"QCheckBox {   spacing: 2px 5px;}"
		"QCheckBox::indicator {   width: 28px;   height: 28px; }"
		"QCheckBox::indicator::unchecked {	image: url(:/icons/check-unchecked.png);}"
		"QCheckBox::indicator::checked {		image: url(:/icons/check-checked.png);}"
		"QLineEdit {	border: 0;	background: #292929;	padding: 6px;	margin: 0;}"
		"QToolButton {	background: #1E1E1E;	border: 0;	padding: 6px;}"
		"QToolButton:pressed {	background: #111;}"
		"QToolButton:hover {	background: #404040;}"
		"QDoubleSpinBox, QSpinBox {	border-radius: 1px;	padding: 6px;	background: #292929;}"
		"QDoubleSpinBox::up-arrow, QSpinBox::up-arrow {	width:0;}"
		"QDoubleSpinBox::up-button, QSpinBox::up-button, QDoubleSpinBox::down-button, QSpinBox::down-button {	width:0;}"
		"QComboBox:editable {}"
		"QComboBox QAbstractItemView::item {    show-decoration-selected: 1;}"
		"QComboBox QAbstractItemView::item {    padding: 6px;}"
		"QListView::item:selected {    background: #404040;}"
		"QComboBox:!editable, QComboBox::drop-down:editable {     background: #1A1A1A;}"
		"QComboBox:!editable:on, QComboBox::drop-down:editable:on {    background: #1A1A1A;}"
		"QComboBox QAbstractItemView {    background-color: #1A1A1A;    selection-background-color: #404040;    border: 0;    outline: none;}"
		"QComboBox QAbstractItemView::item {    border: none;    padding-left: 5px;}"
		"QComboBox QAbstractItemView::item:selected {    background: #404040;    padding-left: 5px;}"
		"QComboBox::drop-down {    subcontrol-origin: padding;    subcontrol-position: top right;    width: 18px;    border-left-width: 1px;}"
		"QComboBox::down-arrow {    image: url(:/icons/down_arrow_check.png);	width: 18px;	height: 14px;} "
		"QComboBox::down-arrow:!enabled {    image: url(:/icons/down_arrow_check_disabled.png);    width: 18px;    height: 14px;}"
		"QLabel{}"
		"QPushButton{padding : 3px; }"
	);


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
	setStyleSheet("QDoubleSpinBox{ border-radius : 1px; padding : 7px; background: #292929; }"
		"QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}"
	);
}


WideRangeIntBox::WideRangeIntBox(QWidget *parent) : QSpinBox(parent)
{
	setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
	setRange(-INT32_MAX, INT32_MAX);
	setRange(-1000, 1000);
	setStyleSheet("QSpinBox{ border-radius : 1px; padding : 7px; background: #292929; }"
		"QSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}"
	);
}


