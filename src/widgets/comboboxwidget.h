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

#include <QComboBox>
#include <QVariant>
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
    void addItem(const QString&, const QVariant &data = QVariant());
    int findData(const QVariant &data);
    QString getCurrentItem();
    QString getCurrentItemData();
    QVariant getItemData(int index);
    void setCurrentItem(const QString&);
    void setCurrentText(const QString&);
    void setCurrentItemData(const QString&);
    void setCurrentIndex(const int&);
    void clear();

    QComboBox *getWidget() const;

signals:
    void currentIndexChanged(const QString&);
    void currentTextChanged(const QString&);
    void currentIndexChanged(int);

private slots:
    void onDropDownTextChanged(const QString&);
    void onDropDownIndexChanged(int);

private:
    Ui::ComboBoxWidget* ui;
};

#endif // COMBOBOXWIDGET_H
