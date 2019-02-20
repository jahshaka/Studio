//
//  SS.cpp
//  color_picker
//
//  Created by will on 2/3/19.
//

#include "ss.h"



QString SS::QPushButtonInvisible() {
    return {
        "QPushButton{background : rgba(0,0,0,0); border : 0px; }"
    };
}

QString SS::QSpinBox() {
    return {
        "QAbstractSpinBox { padding-right: 0px;    background: #292929; margin-right: -6px; }"
        "QAbstractSpinBox, QLabel { color:rgba(255,255,255,.8); padding: 3px } QAbstractSpinBox::down-button, QAbstractSpinBox::up-button { background: rgba(0,0,0,0); border : 1px solid rgba(0,0,0,0); }"
        
    };
}

QString SS::QSlider() {
    return {
        "QSlider::sub-page {    border: 0px solid transparent;    height: 2px;    background: #3498db;    margin: 2px 0;}"
        "QSlider::groove:horizontal { border: 0px solid transparent; height: 4px; background: #1e1e1e;   margin: 1px 0; border-radius: .5px; }"
        "QSlider::handle:horizontal {    background-color: #CCC;    width: 12px;    border: 1px solid #1e1e1e;    margin: -5px 0px;   border-radius:7px;}"
        "QSlider::handle:horizontal:pressed {    background-color: #AAA;    width: 12px;   border: 1px solid #1e1e1e;    margin: -5px 0px;    border-radius: 7px;}"
        "QSlider::handle:horizontal:disabled {    background-color: #bbbbbb;    width: 12px;    border: 0px solid transparent;    margin: -1px -1px;    border-radius: 4px;}"
        "QSlider::groove:vertical {background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,stop: 0 white, stop: 1 black); border-radius: 2px; width: 13px; }"
        " QSlider::handle:vertical {height: 3px; width:1px; margin: -2px 0px; background: rgba(50,148,213,0.9); }"
        " QSlider::add-page:vertical, QSlider::sub-page:vertical {background: rgba(0,0,0,0); border-radius: 1px;}"
        
    };
}

QString SS::QLineEdit() {
    return {
        "QLineEdit { color: rgba(255,255,255,.9); background: rgba(51,51,51,0.5); border: 1px solid rgba(0,0,0,.4); selection-background-color: #808080; padding : 5px;}"

    };
}

QString SS::QPushButtonGreyscale() {
    return{
        "QPushButton{ background-color: #333; color: #DEDEDE; border : 0; padding: 4px 16px; }"
        "QPushButton:hover{ background-color: #555; }"
        "QPushButton:pressed{ background-color: #444; }"
        "QPushButton:checked{border: 0px solid rgba(0,0,0,.3); background: rgba(50,148,213,0.9); color: rgba(255,255,255,.9); }"
    };
}

QString SS::QWidgetDark() {
    return {
        "QWidget{ background: rgba(26,26,26,1);border: 1px solid rgba(0,0,0,0); padding:0px; spacing : 0px;}"

    };
}

QString SS::QLabelWhite() {
    return {
        "QLabel{ color : rgba(255, 255,255, .9); }"
    };
}

QString SS::QComboBox() {
    return {
        "QComboBox{background: rgba(0,0,0,0); border :0px; border-bottom: 1px solid black; padding: 5px; margin-left : 5px; color : rgba(255,255,255,.9);} "
        "QComboBox:drop-down {   border :0px solid black;}"
        "QComboBox QAbstractItemView { background-color: rgba(33,33,33,1);  border :0px; border-bottom: 1px solid black;padding: 5px; padding-left: 1px; margin-left : 0px; outline: 0px; selection-background-color: rgba(40,128, 185,0); }"
        "QComboBox QAbstractItemView::item:hover { background-color: red; border :0px solid rgba(0,0,0,0);}"
    };
}

QString SS::QPushButtonRounded(int size) {
    return {
        "QPushButton{border : 0px; radius : "+QString::number(size/2)+" }"
    };
}

QString SS::QPushButtonGrouped() {
    return QString(
    "QPushButton{ background:rgba(51,51,51,.5); color: rgba(190,190,190,1); border : 1px solid rgba(20,20,20,.4); padding: 5px 16px;} "
    "QPushButton:checked{ color : rgba(50,150,250,1);}"
    "QPushButton:hover{background: rgba(50,50,50,1);}"
    "QPushButton:pressed{background:rgba(61,61,61,.9);}"
                                                                                                 
                                                                                                 );
}









