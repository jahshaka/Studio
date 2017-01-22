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

namespace Ui {
class checkboxproperty;
}

class CheckBoxProperty : public QWidget
{
    Q_OBJECT

public:
    explicit CheckBoxProperty(QWidget *parent = 0);
    ~CheckBoxProperty();

    /**
     * Sets the value of the checkbox
     * Does not emit valueChanged signal
     * @param val
     */
    void setValue(bool val);

    /**
     * Sets the label for the property
     * @param lable
     */
    void setLabel(QString lable);

signals:
    void valueChanged(bool val);

private slots:
    void onCheckboxValueChanged(bool val);

private:
    Ui::checkboxproperty *ui;
};

#endif // CHECKBOXPROPERTY_H
