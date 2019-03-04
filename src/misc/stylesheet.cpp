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


const QString StyleSheet::QPushButtonInvisible() {
    return QString(
        "QPushButton{background : rgba(0,0,0,0); border : 0px; }"
    );
}

const QString StyleSheet::QSpinBox() {
    return QString(
        "QAbstractSpinBox {  color:rgba(255,255,255,.8); padding: 3px; padding-right: 0px;    background: #292929; margin-right: -6px; }"
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
        "QLineEdit { color: rgba(255,255,255,.9); background: rgba(51,51,51,0.5); border: 1px solid rgba(0,0,0,.4); selection-background-color: #808080; padding : 5px;}"

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

const QString StyleSheet::QWidgetDark() {
    return QString(
        "QWidget{ background: rgba(26,26,26,1);border: 1px solid rgba(0,0,0,0); padding:0px; spacing : 0px;}"

    );
}

const QString StyleSheet::QLabelWhite() {
    return QString(
        "QLabel{ color : rgba(255, 255,255, .9); }"
    );
}

const QString StyleSheet::QComboBox() {
    return QString(
		"QComboBox:editable {}"
		"QComboBox QAbstractItemView::item {    show-decoration-selected: 1;}"
		"QComboBox QAbstractItemView::item {    padding: 6px;}"
		"QComboBox  {    background-color: #1A1A1A;   border: 0;    outline: none; padding: 3px 10px; }"
		"QComboBox:!editable, QComboBox::drop-down:editable {     background: #1A1A1A;}"
		"QComboBox:!editable:on, QComboBox::drop-down:editable:on {    background: #1A1A1A;}"
		"QComboBox QAbstractItemView {    background-color: #1A1A1A;    selection-background-color: #404040;    border: 0;    outline: none; padding: 4px 10px; }"
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
    "QAbstractButton{ background:rgba(51,51,51,.5); color: rgba(190,190,190,1); border : 1px solid rgba(20,20,20,.4); padding: 9px 16px;} "
    "QAbstractButton:checked{ background : rgba(50,150,250,1);}"
    "QAbstractButton:hover{background: rgba(50,50,50,1);}"
    "QAbstractButton:pressed{background:rgba(61,61,61,.9);}"
    );
}

const QString StyleSheet::QCheckBox()
{
	return QString(
		"QCheckBox {   spacing: 2px 5px; width: 12px; height :12px;}"
		"QCheckBox::indicator {   width: 18px;   height: 18px; }"
		"QCheckBox::indicator::unchecked {	image: url(:/icons/check-unchecked.png);}"
		"QCheckBox::indicator::checked {		image: url(:/icons/check-checked.png);}"
	);
}

void StyleSheet::setStyle(QWidget *widget)
{
	auto name = widget->metaObject()->className();

	if (std::strcmp(name, "QPushButton") == 0) widget->setStyleSheet(QPushButtonGreyscale());
	if (std::strcmp(name, "QLineEdit") == 0) widget->setStyleSheet(QLineEdit());
	if (std::strcmp(name, "QLabel") == 0) widget->setStyleSheet(QLabelWhite());
	if (std::strcmp(name, "QComboBox") == 0) widget->setStyleSheet(QComboBox());
	if (std::strcmp(name, "QCheckBox") == 0) widget->setStyleSheet(QCheckBox());


	
}

void StyleSheet::setStyle(QObject *obj)
{
	auto name = obj->metaObject()->className();
	if (std::strcmp(name,"QButtonGroup")==0) {
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









