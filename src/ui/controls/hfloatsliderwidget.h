/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef HFLOATSLIDERWIDGET_H
#define HFLOATSLIDERWIDGET_H

#include "ui/controls/basewidget.h"
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
    // A keyboard-editing session on the spinbox (typing, arrow steps, wheel):
    // starts on the first focused spinbox change, ends on editingFinished
    // (Return or focus-out). Brackets the edit with valueChangeStart/End so a
    // typed value commits an undo entry exactly like a slider drag does.
    bool spinboxEditing = false;

public:
    int index;
    HFloatSliderWidget(QWidget *parent = 0);
    ~HFloatSliderWidget();

    float getValue();
    void setValue(float);
    void setRange(float min, float max);
    /// Digits after the decimal point in the spinbox (Qt's default is 2, which
    /// rounds small-magnitude rows -- fog density lives around 0.024 -- to
    /// uselessness). Also scales the keyboard/wheel step to match.
    void setDecimals(int decimals);

    Ui::HFloatSliderWidget* ui;

signals:
    void valueChanged(float);

    void valueChangeStart(float);
    void valueChangeEnd(float);

private slots:
    void onValueSliderChanged(int);
    void onValueSpinboxChanged(double);
    void onSpinboxEditingFinished();

    void sliderPressed();
    void sliderReleased();
};

#endif // HFLOATSLIDERWIDGET_H
