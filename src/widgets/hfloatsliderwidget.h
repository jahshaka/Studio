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

#include "basewidget.h"
#include <QProxyStyle>

namespace Ui {
    class HFloatSliderWidget;
}

// this allows jumping to the clicked position on the slider
// https://stackoverflow.com/a/26281608/996468
class CustomStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(QStyle::StyleHint hint,
                  const QStyleOption* option = 0,
                  const QWidget* widget = 0,
                  QStyleHintReturn* returnData = 0) const
    {
        if (hint == QStyle::SH_Slider_AbsoluteSetButtons) {
            return (Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
        }

        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

class HFloatSliderWidget : public BaseWidget
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

    void valueChangeStart(float);
    void valueChangeEnd(float);

private slots:
    void onValueSliderChanged(int);
    void onValueSpinboxChanged(double);

    void sliderPressed();
    void sliderReleased();
};

#endif // HFLOATSLIDERWIDGET_H
