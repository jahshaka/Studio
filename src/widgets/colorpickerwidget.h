/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef COLORPICKERWIDGET_H
#define COLORPICKERWIDGET_H

#include <QWidget>
//#include <QColorDialog>

namespace Ui {
class ColorPickerWidget;
}

class QColorDialog;
class ColorPickerWidget : public QWidget
{
    Q_OBJECT

    QColor color;
    QColorDialog* dialog;

public:
    int index;
    explicit ColorPickerWidget(QWidget *parent = 0);
    ~ColorPickerWidget();

    void paintEvent(QPaintEvent* evt);
    void mouseReleaseEvent(QMouseEvent * event);
    void mousePressEvent(QMouseEvent * event);
    QColor getColor()
    {
        return color;
    }

public slots:
    void setColor(QColor col);

private slots:
    void colorChanged(QColor color);

signals:
    void onSetColor(QColor col);
    void onColorChanged(QColor color);

private:
    Ui::ColorPickerWidget *ui;
};

#endif // COLORPICKERWIDGET_H
