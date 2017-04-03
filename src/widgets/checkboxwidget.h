/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef CHECKBOXPROPERTY_H
#define CHECKBOXPROPERTY_H

#include <QWidget>
#include <QCheckBox>

namespace Ui {
    class CheckBoxWidget;
}

class CheckBoxWidget : public QWidget
{
    Q_OBJECT

public:
    int index;
    explicit CheckBoxWidget(QWidget *parent = 0);
    ~CheckBoxWidget();

    /**
     * Sets the value of the checkbox
     * Does not emit valueChanged signal
     * @param val
     */
    void setValue(bool val);
    bool getValue();

    /**
     * Sets the label for the property
     * @param lable
     */
    void setLabel(QString label);

signals:
    void valueChanged(bool val);

private slots:
    void onCheckboxValueChanged(int val);

private:
    Ui::CheckBoxWidget *ui;
};

#endif // CHECKBOXPROPERTY_H
