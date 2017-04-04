/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef COMBOBOXWIDGET_H
#define COMBOBOXWIDGET_H

#include <QWidget>

namespace Ui {
    class ComboBoxWidget;
}

class ComboBoxWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ComboBoxWidget(QWidget *parent = 0);
    ~ComboBoxWidget();

    int index;
    void setLabel(const QString&);
    void addItem(const QString&);
    QString getCurrentItem();
    void setCurrentItem(const QString&);

signals:
    void currentIndexChanged(const QString&);
    void currentTextChanged(const QString&);

private slots:
    void onDropDownTextChanged(const QString&);

private:
    Ui::ComboBoxWidget* ui;
};

#endif // COMBOBOXWIDGET_H
