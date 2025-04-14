/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "stylesheet.h"
#include <QDebug>
#include <QButtonGroup>
#include <QAbstractButton>


const QString StyleSheet::QPushButtonBlue()
{
	return QString(
		"QPushButton{ background : rgba(50,148,213,0.9); color: #DEDEDE; border : 0; padding: 4px 16px; }"
		"QPushButton:hover{ background-color: rgba(60,158,223,0.9); }"
		"QPushButton:pressed{ background-color: rgba(40,138,203,0.9); }"
		"QPushButton:checked{border: 0px solid rgba(0,0,0,.3); background: rgba(50,148,213,0.9); color: rgba(255,255,255,.9); }"
	);
}

const QString StyleSheet::QPushButtonBlueBig()
{
	return QString(
		"QPushButton{ background : rgba(50,148,213,0.9); color: #DEDEDE; border : 0; padding: 8px 22px; }"
		"QPushButton:hover{ background-color: rgba(60,158,223,0.9); }"
		"QPushButton:pressed{ background-color: rgba(40,138,203,0.9); }"
		"QPushButton:checked{border: 0px solid rgba(0,0,0,.3); background: rgba(50,148,213,0.9); color: rgba(255,255,255,.9); }"
	);
}

const QString StyleSheet::QPushButtonInvisible() {
    return QString(
        "QPushButton{background : rgba(0,0,0,0); border : 0px; }"
    );
}

const QString StyleSheet::QSpinBox() {
    return QString(
		"QAbstractSpinBox { background: rgba(21,21,21,1); color:rgba(255,255,255,.8); padding: 4px; padding-right: 0px; margin-right: 0px; border : 0; }"
		"QAbstractSpinBox::down-button, QAbstractSpinBox::up-button { background: rgba(0,0,0,0); border : 1px solid rgba(0,0,0,0); }"
        
    );
}

const QString StyleSheet::QSlider() {
    return QString(
        "QSlider::sub-page {    border: 0px solid transparent;    height: 2px;    background: #3498db;    margin: 2px 0;}"
        "QSlider::groove:horizontal { border: 0px solid transparent; height: 4px; background: #1e1e1e;   margin: 1px 0; border-radius: .5px; }"
        "QSlider::handle:horizontal {    background-color: #CCC;    width: 12px;    border: 1px solid #1e1e1e;    margin: -5px 0px;   border-radius:7px;}"
        "QSlider::handle:horizontal:pressed {    background-color: #AAA;    width: 12px;   border: 1px solid #1e1e1e;    margin: -5px 0px;    border-radius: 7px;}"
        "QSlider::handle:horizontal:disabled {    background-color: #bbbbbb;    width: 12px;    border: 0px solid transparent;    margin: -1px -1px;    border-radius: 4px;}"
        "QSlider::groove:vertical {background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,stop: 0 white, stop: 1 black); border-radius: 2px; width: 13px; }"
        " QSlider::handle:vertical {height: 3px; width:1px; margin: -2px 0px; background: rgba(50,148,213,0.9); }"
        " QSlider::add-page:vertical, QSlider::sub-page:vertical {background: rgba(0,0,0,0); border-radius: 1px;}"
        
    );
}

const QString StyleSheet::QLineEdit() {
    return QString(
        "QLineEdit { color: rgba(255,255,255,.9); background: rgba(51,51,51,0.5); border: 0px solid rgba(0,0,0,0); selection-background-color: #808080; padding : 5px;}"

    );
}

const QString StyleSheet::QPushButtonGreyscale() {
    return QString(
        "QPushButton{ background-color: #333; color: #DEDEDE; border : 0; padding: 4px 16px; }"
        "QPushButton:hover{ background-color: #555; }"
        "QPushButton:pressed{ background-color: #444; }"
        "QPushButton:checked{border: 0px solid rgba(0,0,0,.3); background: rgba(50,148,213,0.9); color: rgba(255,255,255,.9); }"
    );
}

const QString StyleSheet::QPushButtonGreyscaleBig()
{
	return QString(
		"QPushButton{ background-color: #333; color: #DEDEDE; border : 0; padding: 8px 22px; }"
		"QPushButton:hover{ background-color: #555; }"
		"QPushButton:pressed{ background-color: #444; }"
		"QPushButton:checked{border: 0px solid rgba(0,0,0,.3); background: rgba(50,148,213,0.9); color: rgba(255,255,255,.9); }"
	);
}

const QString StyleSheet::QWidgetDark() {
    return QString(
        "QWidget{ background: rgba(26,26,26,1);border: 1px solid rgba(0,0,0,0); padding:0px; spacing : 0px;}"
    );
}

const QString StyleSheet::QWidgetTransparent()
{
	return QString(
		"QWidget{ background: rgba(26,26,26,0);border: 1px solid rgba(0,0,0,0); padding:0px; spacing : 0px;}"
	);
}

const QString StyleSheet::QLabelWhite() {
    return QString(
        "QLabel{ color : rgba(255, 255,255, .9); }"
    );
}

const QString StyleSheet::QLabelBlack()
{
	return QString(
		"QLabel{ color : rgba(20, 20, 20, .9); }"
	);
}

const QString StyleSheet::QComboBox() {
    return QString(
		"QComboBox  {    background-color: #1A1A1A;   border: 0;    outline: none; padding: 3px 10px; color: rgba(255,255,255,.9); }"
		"QComboBox:editable {}"
		"QComboBox QAbstractItemView::item {    show-decoration-selected: 1;  padding: 6px; }"
		"QComboBox:!editable, QComboBox::drop-down:editable {     background: #1A1A1A;}"
		"QComboBox:!editable:on, QComboBox::drop-down:editable:on {    background: #1A1A1A;}"
		"QComboBox QAbstractItemView {    background-color: #1A1A1A; color: rgba(255,255,255,.9);    selection-background-color: #404040;    border: 0;    outline: none; padding: 4px 10px; }"
		"QComboBox QAbstractItemView::item {    border: none; padding: 4px 10px;}"
		"QComboBox QAbstractItemView::item:selected {    background: #404040;    padding-left: 5px;}"
		"QComboBox::drop-down {    subcontrol-origin: padding;    subcontrol-position: top right;    width: 18px;    border-left-width: 1px;}"
		"QComboBox::down-arrow {    image: url(:/icons/down_arrow_check.png);	width: 18px;	height: 14px;} "
		"QComboBox::down-arrow:!enabled {    image: url(:/icons/down_arrow_check_disabled.png);    width: 18px;    height: 14px;}"
	);
}

const QString StyleSheet::QPushButtonRounded(int size) {
    return QString(
        "QPushButton{border : 0px; radius : "+ QString::number(size/2)+" }"
    );
}


const QString StyleSheet::QPushButtonGrouped() {
	return QString(
		"QAbstractButton{ background:rgba(51,51,51,.5); color: rgba(190,190,190,1); border : 0 ; padding: 4px 16px;} "
		"QAbstractButton:checked{ background : rgba(50,150,250,1);}"
		"QAbstractButton:hover{background: rgba(50,50,50,1);}"
		"QAbstractButton:pressed{background:rgba(61,61,61,.9);}"
	);
}

const QString StyleSheet::QPushButtonGroupedBig() {
	return QString(
		"QAbstractButton{ background:rgba(51,51,51,.5); color: rgba(190,190,190,1); border : 0 ; padding: 12px 30px;} "
		"QAbstractButton:checked{ background : rgba(50,150,250,1);}"
		"QAbstractButton:hover{background: rgba(50,50,50,1);}"
		"QAbstractButton:pressed{background:rgba(61,61,61,.9);}"
	);
}

const QString StyleSheet::QPushButtonDanger()
{
	return QPushButtonGrouped() + QString(
		"QAbstractButton{background : rgba(200,40,40,1);}"
	);
}

const QString StyleSheet::QCheckBox()
{
	return QString(
		//"QCheckBox { width: 16px; height :16px; }"
		"QCheckBox::indicator {   width: 18px;   height: 18px; margin : 0px; padding :0px; right : -5px; }"
		"QCheckBox::indicator::unchecked {	image: url(:/icons/check-unchecked.png);}"
		"QCheckBox::indicator::checked {image: url(:/icons/check-checked.png);}"
	);
}

const QString StyleSheet::QSplitter()
{
	return QString(
	"QSplitter::handle:horizontal{}"
	);
}

const QString StyleSheet::QAbstractScrollArea()
{
	return QString(
		"QAbstractScrollArea{ background : rgba(51,51,51,.5); color : rgba(255,255,255,.9); border : 0; padding : 0px 12px;  }"
		"QScrollBar:vertical {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 16px; padding: 4px;}"
		"QScrollBar::handle:vertical{ background: rgba(72, 72, 72, 1);	border-radius : 4px; width: 10px; }"
		"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		
		"QScrollBar:horizontal {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 24px; padding: 4px;}"
		"QScrollBar::handle:horizontal{ background: rgba(72, 72, 72, 1);	border-radius: 8px; height: 10px; }"
		"QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:horizontal, QScrollBar::down-arrow:horizontal {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		
		"QScrollBar::sub-line, QScrollBar::add-line {	background: rgba(10, 0, 0, .0);}"
	);
}

const QString StyleSheet::QMenu()
{
	return QString(
		"QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9); border-radius : 2px; }"
		"QMenu::item{padding: 6px 20px 6px 14px;	}"
		"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
		"QMenu::item:selected{	background: rgba(40,128, 185,.9);}"
	);
}

void StyleSheet::setStyle(QWidget *widget)
{
	auto name = widget->metaObject()->className();

	if (name == QStringLiteral("QPushButton")) widget->setStyleSheet(QPushButtonGreyscale());
	if (name == QStringLiteral( "QLineEdit")) widget->setStyleSheet(QLineEdit());
	if (name == QStringLiteral( "QLabel")) widget->setStyleSheet(QLabelWhite());
	if (name == QStringLiteral( "QComboBox")) widget->setStyleSheet(QComboBox());
	if (name == QStringLiteral( "QCheckBox")) widget->setStyleSheet(QCheckBox());
	if (name == QStringLiteral( "QSpinBox") || name == QStringLiteral("QDoubleSpinBox")) widget->setStyleSheet(QSpinBox());
	if (name == QStringLiteral( "QSplitter")) widget->setStyleSheet(QSpinBox());
	if (name == QStringLiteral( "QTextBrowser")) widget->setStyleSheet(QAbstractScrollArea());
}

void StyleSheet::setStyle(QObject *obj)
{
	auto name = obj->metaObject()->className();
	if (name == QStringLiteral("QButtonGroup")) {
		auto bg = static_cast<QButtonGroup*>(obj);
		for (auto btn : bg->buttons()) {
			btn->setCheckable(true);
			btn->setStyleSheet(QPushButtonGrouped());
		}
	}
}

void StyleSheet::setStyle(QList<QWidget*> list)
{
	for (auto wid : list) setStyle(wid);
}









