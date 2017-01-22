/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef LCDSLIDER_H
#define LCDSLIDER_H

#include <QWidget>

namespace Ui {
class LCDSlider;
class NewLCDSlider;
}

class LCDSlider : public QWidget
{
    Q_OBJECT

public:
    explicit LCDSlider(QWidget *parent = 0);
    ~LCDSlider();

    void setValue(int value);
    void setValueRange(int min,int max);
    void setLabelText(QString text);

    void hideLabel();
    void showLabel();

signals:
    void valueChanged(int value);

private slots:
    void onSliderValueChanged(int value);
    void onEditTextValueChanged(int value);

private:
    void mousePressEvent(QMouseEvent* evt);
    void mouseMoveEvent(QMouseEvent* evt);
    void mouseReleaseEvent(QMouseEvent* evt);
    void mouseDoubleClickEvent(QMouseEvent* evt);

    //Ui::LCDSlider* ui;
    Ui::NewLCDSlider* ui;
    bool dragging;
    int lastX;
    int valueScale;
    int value;
};

#endif // LCDSLIDER_H
