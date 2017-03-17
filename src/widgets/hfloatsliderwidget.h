/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef HFLOATSLIDERWIDGET_H
#define HFLOATSLIDERWIDGET_H

#include <QWidget>

namespace Ui {
    class HFloatSliderWidget;
}

class HFloatSliderWidget : public QWidget
{
    Q_OBJECT

    int precision;
    float value;
    float minVal, maxVal;

public:
    int index;
    HFloatSliderWidget(QWidget *parent = 0);
    ~HFloatSliderWidget();

    float getValue();
    void setValue(float);
    void setRange(float min, float max);

    Ui::HFloatSliderWidget* ui;

signals:
    void valueChanged(float);

private slots:
    void onValueSliderChanged(int);
    void onValueSpinboxChanged(double);
};

#endif // HFLOATSLIDERWIDGET_H
